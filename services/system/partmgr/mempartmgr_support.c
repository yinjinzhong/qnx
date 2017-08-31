/*
 * $QNXLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 * 
 * You must obtain a written license from and pay applicable license fees to QNX 
 * Software Systems before you may reproduce, modify or distribute this software, 
 * or any work that includes all or part of this software.   Free development 
 * licenses are available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *  
 * This file may contain contributions from others.  Please review this entire 
 * file for other proprietary rights or license notices, as well as the QNX 
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
 * for other information.
 * $
 */

/*==============================================================================
 * 
 * mempartmgr_support
 * 
 * Resource manager miscellaneous support functions
 * 
*/

#include "mempartmgr.h"

static mempartmgr_attr_t *_path_find(mempartmgr_attr_t *root, char *path, mempartmgr_attr_t **parent);
static mempartmgr_attr_t *find_sibling(mempartmgr_attr_t *root, const char *name);
static int _check_access_perms(resmgr_context_t *ctp, iofunc_attr_t *attr, mode_t check, struct _client_info *info);


/*******************************************************************************
 * path_find
 * 
 * search from <root> looking for <name>. If found return a pointer to the
 * mempartmgr_attr_t for <name>, otherwise return NULL.
 * If <parent> is non-NULL, return the parent mempartmgr_attr_t
*/
mempartmgr_attr_t *path_find(mempartmgr_attr_t *root, char *path, mempartmgr_attr_t **parent)
{
	if (*path == '\0')
	{
		if (parent != NULL) *parent = root_mpart->parent;
			return root_mpart;
	}
	else
	{
		if (parent != NULL) *parent = (root != NULL) ? root->parent : NULL;
		return _path_find(root, path, parent);
	}
}

/*******************************************************************************
 * mpmgr_getattr
 * 
 * Return a pointer to the 'mempartmgr_attr_t' associated with <path>.
 * This function hooks the generic partmgr code to the mempartmgr code.
*/
mempartmgr_attr_t *mpmgr_getattr(char *path)
{
	if (*path == '/') ++path;
	return path_find((mempartmgr_attr_t *)LIST_FIRST(root_mpart->children), path, NULL);
}

/*******************************************************************************
 * isStringOfDigits
 * 
 * Return True or False based on whether or not <part_name> is a string of
 * digits between (0-9)
*/
bool isStringOfDigits(char *part_name)
{
	char  c;
	while((c = *part_name++) != '\0')
		if ((c < 0x30) || (c > 0x39))
			return 0;	// FALSE
	
	return 1;	// TRUE;
}

/*******************************************************************************
 * find_pid_in_hierarchy
 * 
 * Determine whether process <pid> is associated with any partition in the
 * partition hierarchy starting at <mp>.
 *
 * If so, a pointer to the 'mempartmgr_attr_t' that the process is associated
 * with will be returned, otherwise NULL will be returned.
 * If an 'mempartmgr_attr_t' structure is returned, it is guaranteed to be a
 * child, grandchild, great-grandchild, etc of <mp>. 
*/
mempartmgr_attr_t *find_pid_in_mpart_hierarchy(pid_t pid, mempartmgr_attr_t *mp)
{
	PROCESS *prp;
	mempart_t *mpart;
	mempart_id_t mpid;
	mempartmgr_attr_t *mp_to_find;

	if (((prp = proc_lookup_pid(pid)) == NULL) ||
		((mpid = mempart_getid(prp, mempart_get_classid(mp->mpid))) == mempart_id_t_INVALID))
		return NULL;

	mpart = MEMPART_ID_TO_T(mpid);
	KASSERT(mpart != NULL);

	mp_to_find = (mempartmgr_attr_t *)mpart->rmgr_attr_p;
	KASSERT(mp_to_find != NULL);

	if (mp == mp_to_find)
		return mp;
	else
	{
		while ((mp_to_find = mp_to_find->parent) != NULL)
			if (mp_to_find == mp)
				return mp;
		return NULL;
	}
}

/*******************************************************************************
 * check_access_perms
 * 
 * Recursively check the hierarchy starting at <mp> for accessibility
 * 
 * Returns: EOK on success, otherwise and errno
*/
int check_access_perms(resmgr_context_t *ctp, mempartmgr_attr_t *mp, mode_t check,
						struct _client_info *info, bool recurse)
{
	int  r;

	if ((r = _check_access_perms(ctp, &mp->attr, check, info)) == EOK)
		if ((mp->parent != NULL) && (mp->parent->type != mpart_type_ROOT) && recurse)
			r = check_access_perms(ctp, mp->parent, check, info, recurse);
	return r;
}

/*******************************************************************************
 * unregister_events
 * 
 * Remove any events attached through this <ocb>.
 * 
 * FIX ME - currently I run all the event lists for the mempart referenced by
 * <ocb>. This is very inefficient. What I should do is also link all events
 * registered through this ocb to the ocb as well so that cleanup will be a
 * breeze. This requires a private ocb type which I currently don't do. 
*/
int unregister_events(iofunc_ocb_t *ocb, int num_evts)
{
	unsigned  i;
	int  r;
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	
	if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
		return r;
	
	for (i=0; i<num_evts; i++)
	{
		mempart_evt_t *event = (mempart_evt_t *)LIST_FIRST(mp->event_list[i]);
		while (event != NULL)
		{
			mempart_evt_t *next_event = (mempart_evt_t *)LIST_NEXT(event);	// grab before we delete
			if (event->ocb == ocb)
			{
				LIST_DEL(mp->event_list[i], event);
				free(event);
			}
			event = next_event;
		} 
	}
	MEMPART_ATTR_UNLOCK(mp);
	return EOK;
}

/*******************************************************************************
 * register_mpart_event
 * 
 * Register memory partition event <evt> to the memory partition identified by
 * <ocb> and <rcvid>.
 * 
 * If successful, a 'mempart_evt_t' structure will be allocated, initialized
 * and added to the queue of events for the event type.
 * 
 * Returns: EOK on success otherwise an errno
 * 
 * 'memsize_t' overflow check
 * external representation of 'memsize_t' can be larger than the internal
 * representation of 'memsize_t' so ckeck for overflows on incoming size
 * attributes
*/
int register_mpart_event(struct mp_evt_info_s *evt, struct evtdest_s *evtdest, void *ocb)
{
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	mempart_evt_t  *event = calloc(1, sizeof(*event));

	KASSERT(mp->type == mpart_type_MEMPART_real);
	KASSERT(evt->info.type >= mempart_evttype_t_first);
	KASSERT(evt->info.type <= mempart_evttype_t_last);

	if (event == NULL)
		return ENOMEM;

	if ((evt->info.type == mempart_evttype_t_THRESHOLD_CROSS_OVER) ||
		(evt->info.type == mempart_evttype_t_THRESHOLD_CROSS_UNDER))
	{
#ifdef _MEMSIZE_and_memsize_are_different_
		if (MEMSIZE_OFLOW(evt->info.val.threshold_cross))
		{
#ifndef NDEBUG
			kprintf("event threshold size %d bit overflow\n", sizeof(_MEMSIZE_T_) * 8);
#endif
			free(event);
			return EOVERFLOW;
		}
#endif	/*  _MEMSIZE_and_memsize_are_different_ */
	}
	else if ((evt->info.type == mempart_evttype_t_DELTA_INCR) ||
		(evt->info.type == mempart_evttype_t_DELTA_DECR))
	{
#ifdef _MEMSIZE_and_memsize_are_different_
		if (MEMSIZE_OFLOW(evt->info.val.delta))
		{
#ifndef NDEBUG
			kprintf("event delta size %d bit overflow\n", sizeof(_MEMSIZE_T_) * 8);
#endif
			free(event);
			return EOVERFLOW;
		}
#endif	/*  _MEMSIZE_and_memsize_are_different_ */

		/* cause the proper value to be initialized once the event is registered */
		event->evt_data.size = memsize_t_INFINITY;
	}
	else if ((evt->info.type == mempart_evttype_t_CONFIG_CHG_ATTR_MIN) ||
		(evt->info.type == mempart_evttype_t_CONFIG_CHG_ATTR_MAX))
	{
#ifdef _MEMSIZE_and_memsize_are_different_
		if (MEMSIZE_OFLOW(evt->info.val.cfg_chg.attr.min))
		{
#ifndef NDEBUG
			kprintf("event attribute size %d bit overflow\n", sizeof(_MEMSIZE_T_) * 8);
#endif
			free(event);
			return EOVERFLOW;
		}
#endif	/*  _MEMSIZE_and_memsize_are_different_ */
	}

	event->evt_reg.mp = *evt;
	event->evt_dest = *evtdest;
	event->ocb = ocb;
	LIST_ADD(mp->event_list[MEMPART_EVTYPE_IDX(evt->info.type)], event);

	return EOK;
}

/*******************************************************************************
 * register_mclass_event
 * 
 * Register memory class event <evt> to the memory class identified by <ocb>
 * and <rcvid>.
 * 
 * If successful, a 'mempart_evt_t' structure will be allocated, initialized
 * and added to the queue of events for the event type.
 * 
 * Returns: EOK on success otherwise an errno
 * 
 * 'memsize_t' overflow check
 * external representation of 'memsize_t' can be larger than the internal
 * representation of 'memsize_t' so ckeck for overflows on incoming size
 * attributes
*/
int register_mclass_event(struct mc_evt_info_s *evt, struct evtdest_s *evtdest, void *ocb)
{
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	mempart_evt_t  *event = calloc(1, sizeof(*event));

	KASSERT(mp->type == mpart_type_MEMCLASS);
	KASSERT(evt->info.type >= memclass_evttype_t_first);
	KASSERT(evt->info.type <= memclass_evttype_t_last);

	if (event == NULL)
		return ENOMEM;

	switch (evt->info.type)
	{
		case memclass_evttype_t_THRESHOLD_CROSS_TF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_TU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_RF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_UF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_RU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_UU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER:
		{
#ifdef _MEMSIZE_and_memsize_are_different_
			if (MEMSIZE_OFLOW(evt->info.val.threshold_cross))
			{
#ifndef NDEBUG
				kprintf("event threshold size %d bit overflow\n", sizeof(_MEMSIZE_T_) * 8);
#endif
				free(event);
				return EOVERFLOW;
			}
#endif	/* _MEMSIZE_and_memsize_are_different_ */
			break;
		}
		
		case memclass_evttype_t_DELTA_TF_INCR:
		case memclass_evttype_t_DELTA_TF_DECR:
		case memclass_evttype_t_DELTA_TU_INCR:
		case memclass_evttype_t_DELTA_TU_DECR:
		case memclass_evttype_t_DELTA_RF_INCR:
		case memclass_evttype_t_DELTA_RF_DECR:
		case memclass_evttype_t_DELTA_UF_INCR:
		case memclass_evttype_t_DELTA_UF_DECR:
		case memclass_evttype_t_DELTA_RU_INCR:
		case memclass_evttype_t_DELTA_RU_DECR:
		case memclass_evttype_t_DELTA_UU_INCR:
		case memclass_evttype_t_DELTA_UU_DECR:
		{
#ifdef _MEMSIZE_and_memsize_are_different_
			if (MEMSIZE_OFLOW(evt->info.val.delta))
			{
#ifndef NDEBUG
				kprintf("event delta size %d bit overflow\n", sizeof(_MEMSIZE_T_) * 8);
#endif
				free(event);
				return EOVERFLOW;
			}
#endif	/* _MEMSIZE_and_memsize_are_different_ */
			/* cause the proper value to be initialized once the event is registered */
			event->evt_data.size = memsize_t_INFINITY;
			break;
		}
		
		default:	/* other type of event */
			break;
	}

	event->evt_reg.mc = *evt;
	event->evt_dest = *evtdest;
	event->ocb = ocb;
	LIST_ADD(mp->event_list[MEMCLASS_EVTYPE_IDX(evt->info.type)], event);

	return EOK;
}

/*******************************************************************************
 * validate_association
 * 
 * This function determines whether or not the partition identified by <mpart>
 * can be associated with based on the 'struct _cred_info' <cred>.
 *
 * if S_IXOTH is set in the attributes for <mpart> then EOK is returned otherwise
 * an appropriate uid/gid match is attempted.
 *  
 * Returns: EOK if association is permitted, otherwise an errno.
 * 
*/
int validate_association(mempartmgr_attr_t *attr, struct _cred_info *cred)
{
	if (attr->attr.mode & S_IXOTH) return EOK;
	if ((attr->attr.gid == cred->rgid) && (attr->attr.mode & S_IXGRP)) return EOK;
	if ((attr->attr.uid == cred->ruid) && (attr->attr.mode & S_IXUSR)) return EOK;
	
	return ((attr->attr.gid == cred->rgid) || (attr->attr.uid == cred->ruid)) ? EACCES : EPERM;
}


/*
 * ===========================================================================
 * 
 * 							Internal support routines
 * 
 * ===========================================================================
*/

/*******************************************************************************
 * find_sibling
 * 
 * Find <name> in <root> or its siblings.
 * 
 * Returns: a mempartmgr_attr_t on success, otherwise NULL
*/
static mempartmgr_attr_t *find_sibling(mempartmgr_attr_t *root, const char *name)
{
	while(root != NULL)
	{
		if (strcmp(name, root->name) == 0)
		{
//			if (parent) *parent = root->parent;
			return(root);
		}
		root = (mempartmgr_attr_t *)LIST_NEXT(root);
	}
	return NULL;
}

/*******************************************************************************
 * _path_find
 * 
 * Recurse down through a hierarchy starting at <root> looking for <path>
 * 
 * Returns: a mempartmgr_attr_t on success, otherwise NULL
*/
static mempartmgr_attr_t *_path_find(mempartmgr_attr_t *root, char *path, mempartmgr_attr_t **parent)
{
	if (root != NULL)
	{
		char *p;

		if (strcmp(root->name, path) == 0)
		{
			if (parent != NULL) *parent = root->parent;
			return root;
		}

		if ((p = strchr(path, '/')) != NULL)
		{
			char name[NAME_MAX + 1];
			unsigned len = p - path;

			KASSERT((len + 1) <= sizeof(name));

			memcpy(name, path, len);
			name[len] = '\0';
			if ((root = find_sibling(root, name)) != NULL)
			{
				if (parent != NULL) *parent = root;
				return _path_find((mempartmgr_attr_t *)LIST_FIRST(root->children), ++p, parent);		// skip over leading '/'
			}
		}
		else
			root = find_sibling(root, path);
	}
	return root; 
}


/*
 * _check_access_perms
 * 
 * local (modified) copy of iofunc_check_access() to eliminate the "root can do
 * anything" pass-through
*/
static int _check_access_perms(resmgr_context_t *ctp, iofunc_attr_t *attr, mode_t check, struct _client_info *info)
{
	mode_t  mode;

	// Must supply an info entry
	if(!info) {
		return ENOSYS;
	}

	// Root can do anything
//	if(info->cred.euid == 0) {
//		return EOK;
//	}

	// Check for matching owner
	if((check & S_ISUID) && info->cred.euid == attr->uid) {
		return EOK;
	}

	// Check for matching group
	if(check & S_ISGID) {
		unsigned  n;

		if(info->cred.egid == attr->gid) {
			return EOK;
		}

		for(n = 0; n < info->cred.ngroups; n++) {
			if(info->cred.grouplist[n] == attr->gid) {
				return EOK;
			}
		}
	}

	// If not checking read/write/exec perms, return error
	if((check &=  S_IRWXU) == 0) {
		return EPERM;
	}

	if(info->cred.euid == attr->uid) {
		mode = attr->mode;
	} else if(info->cred.egid == attr->gid) {
		mode = attr->mode << 3;
	} else {
		unsigned						n;

		mode = attr->mode << 6;
		for(n = 0; n < info->cred.ngroups; n++) {
			if(info->cred.grouplist[n] == attr->gid) {
				mode = attr->mode << 3;
				break;
			}
		}
	}

	// Check if all permissions are true
	if((mode & check) != check) {
		return EACCES;
	}

	return EOK;
}


__SRCVERSION("$IQ: mempartmgr_support.c,v 1.23 $");

