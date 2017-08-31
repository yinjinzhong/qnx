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
 * mempartmgr_read
 * 
 * Provide resource manager read() processing for the memory partitioning module
 * 
*/

#include "mempartmgr.h"


static int _mempartmgr_readdir(PROCESS *prp, mempartmgr_attr_t *attr, off_t *offset,
							void *buf, size_t size);
//static bool_t mpart_in_list(mempart_list_t *mpart_list, mempart_t *mempart);
static bool node_is_in_hierarchy(mempartmgr_attr_t *node, mempart_list_t *mpart_list);

/*******************************************************************************
 * mempartmgr_read
 * 
 * This routine wraps _mempartmgr_read() and provides the resource manager
 * read implementation
 * 
*/
int mempartmgr_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	int  r;
	void *reply_msg;

	if (msg->i.type != _IO_READ)
		return EBADF;
	else if ((r = iofunc_read_verify(ctp, msg, ocb, NULL)) != EOK)
		return r;
	else if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE)
		return ENOSYS;
	else if (mp == NULL)
		return ENOENT;
	else
	{
// FIX ME - why can't we use 'msg' as 'reply_msg' ?
		if ((reply_msg = calloc(1, msg->i.nbytes)) == NULL)
			return ENOMEM;
		
		if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
		{
			free(reply_msg);	// FIX ME - won't be required as per above
			return r;
		}
		if (S_ISDIR(mp->attr.mode))
		{
			if ((r = _mempartmgr_readdir(NULL, mp, (off_t *)&ocb->offset, reply_msg, msg->i.nbytes)) >= 0)
			{
				MsgReply(ctp->rcvid, r, reply_msg, r);
				r = EOK;
			}
		}
		else
			r = EBADF;

		MEMPART_ATTR_UNLOCK(mp);
		free(reply_msg);
		return (r == EOK) ? _RESMGR_NOREPLY : -r;
	}
}

/*******************************************************************************
 * mpmgr_read
 *
 * Used by the generic partition manager to perform memory partitioning specific
 * reads
*/
int mpmgr_read(PROCESS *prp, mempartmgr_attr_t *attr, off_t *offset,
							void *buf, size_t *size)
{
	if (attr == NULL)
		return EINVAL;
	else
	{
		int  r;
		
		if ((r = MEMPART_ATTR_LOCK(attr)) != EOK)
			return r;
		r = _mempartmgr_readdir(prp, attr, offset, buf, *size);
		MEMPART_ATTR_UNLOCK(attr);

		if (r >= 0)
		{
			*size = r;
			return EOK;
		}
		else
			return -r;
	}
}

/*******************************************************************************
 * _mempartmgr_readdir
 * 
 * This routine does the actual work of reading the contents of the entry
 * identified by <attr>. The read is performed starting at the offset pointed
 * to by <offset> the contents are placed into buffer <buf>. The size of <buf>
 * is pointed to by <size>. If <prp> == NULL, no filtering will be done.
 * <offset> is adjusted acordingly.
 * 
 * Returns: the number of bytes placed into <buf> (never more than <size>) or
 * 			a negative errno.
 * 
*/
static int _mempartmgr_readdir(PROCESS *prp, mempartmgr_attr_t *attr, off_t *offset,
							void *buf, size_t size)
{
	size_t space_left;
	struct dirent *dir;
	unsigned dirent_max;
	mempart_list_t *mpart_list = NULL;
#define MEMPART_MAX_PARTLIST_ENTRIES	16	// FIX ME - 16 mempart_id_t's
	union {
		mempart_list_t					mpl;
		char		space[MEMPART_LIST_T_SIZE(MEMPART_MAX_PARTLIST_ENTRIES)];											
	} _mempart_list;

	KASSERT(attr != NULL);
	KASSERT(offset != NULL);
	KASSERT(buf != NULL);
	KASSERT(S_ISDIR(attr->attr.mode));
	
	dirent_max = LIST_COUNT(attr->children);
	if (attr->type == mpart_type_MEMPART_real)
		dirent_max += LIST_COUNT((MEMPART_ID_TO_T(attr->mpid))->prp_list);

	if (*offset >= dirent_max)
		return EOK;

	if (prp != NULL)
	{
		mpart_list = &_mempart_list.mpl;
		mpart_list->num_entries = MEMPART_MAX_PARTLIST_ENTRIES;
#ifndef NDEBUG
		{
		int n = MEMPART_GETLIST(prp, mpart_list, MEMPART_MAX_PARTLIST_ENTRIES, mempart_flags_t_GETLIST_ALL, NULL);
		KASSERT(n == 0);
		}
#else	/* NDEBUG */
		MEMPART_GETLIST(prp, mpart_list, MEMPART_MAX_PARTLIST_ENTRIES, mempart_flags_t_GETLIST_ALL, NULL);
#endif	/* NDEBUG */

	}

	dir = (struct dirent *)buf;
	space_left = size;

	if (*offset < LIST_COUNT(attr->children))
	{
		mempartmgr_attr_t *sibling = (mempartmgr_attr_t *)LIST_FIRST(attr->children);
		off_t  dir_offset = 0;

		/* add all of the child partitions */
		while (sibling != NULL)
		{
			bool exclude = (mpart_list != NULL) &&
							 (sibling->type == mpart_type_MEMPART_real) &&
//							 (mpart_in_list(mpart_list, sibling->mpart) == bool_t_FALSE) &&
							 (node_is_in_hierarchy(sibling, mpart_list) == bool_t_FALSE);
			if (!exclude)
			{					
				if (dir_offset >= *offset)
				{
					if (space_left >= (sizeof(struct dirent) + strlen(sibling->name) + 1))
					{
						dir->d_ino = (int)sibling->mpid;
						dir->d_offset = (*offset)++;
						STRLCPY(dir->d_name, sibling->name, NAME_MAX + 1);
						dir->d_namelen = strlen(dir->d_name);
						dir->d_reclen = (sizeof(*dir) + dir->d_namelen + 1 + 3) & ~3;

						space_left -= dir->d_reclen;
						dir = (struct dirent *)((unsigned int)dir + (unsigned int)dir->d_reclen);
					}
					else
						break;
				}
				++dir_offset;
			}
			sibling = (mempartmgr_attr_t *)LIST_NEXT(sibling);
		}
	}
#ifdef USE_PROC_OBJ_LISTS
	if (prp == NULL)
	{
		if ((attr->type == mpart_type_MEMPART_real) &&
			(*offset < (LIST_COUNT((MEMPART_ID_TO_T(attr->mpid))->prp_list) + LIST_COUNT(attr->children))))
		{
			mempart_t  *mpart = MEMPART_ID_TO_T(attr->mpid);
			prp_node_t  *prp_list = (prp_node_t *)LIST_FIRST(mpart->prp_list);
			off_t  dir_offset = LIST_COUNT(attr->children);

			while (prp_list != NULL)
			{
				if (dir_offset >= *offset)
				{
					if (space_left >= (sizeof(struct dirent) + 10 + 1))	// UINT_MAX is 10 digits
					{
						dir->d_ino = PID_TO_INO(prp_list->prp->pid, 1);
						dir->d_offset = (*offset)++;
						ultoa(prp_list->prp->pid, dir->d_name, 10);
						dir->d_namelen = strlen(dir->d_name);
						dir->d_reclen = (sizeof(*dir) + dir->d_namelen + 1 + 3) & ~3;

						space_left -= dir->d_reclen;
						dir = (struct dirent *)((unsigned int)dir + (unsigned int)dir->d_reclen);
					}
					else
						break;
				}
				++dir_offset;
				prp_list = (prp_node_t *)LIST_NEXT(prp_list);
			}
		}
	}
#endif	/* USE_PROC_OBJ_LISTS */

	return size - space_left;
}

#if 0	// FIX ME - no longer required, here for reference
/*******************************************************************************
 * mpart_in_list
 * 
 * Determine whether <mempart> is contained in <mpart_list>
 * 
 * Returns: bool_t_TRUE if it is, bool_t_FALSE is it is not
*/
static bool_t mpart_in_list(mempart_list_t *mpart_list, mempart_t *mempart)
{
	mempart_id_t  id_to_find = MEMPART_T_TO_ID(mempart);
	unsigned i;

	for (i=0; i<mpart_list->num_entries; i++)
		if (mpart_list->i[i].t.id == id_to_find)
			return bool_t_TRUE;
	return bool_t_FALSE;
}
#endif	/* 0 */

/*******************************************************************************
 * node_is_in_hierarchy
 * 
 * Determine whether the mempartmgr_attr_t referred to by <node> is in the
 * partition hierarchy of any partition in <mpart_list>
 * 
 * Example is if a process is associated with partition sysram/p0/p1/p2 and we
 * are filtering for that process, then as _mempartmgr_readdir() is called we
 * will see first sysram. In this case, we cannot reject p0 because it is in the
 * hierarchy of the partition we are ultimately trying to resolve, ie p2.
 * 
 * The most efficient way to do this is to start with the partitions in
 * <mpart_list> and follow the hierarchy up (since a partition can have at most
 * 1 parent) checking against node->mpart along the way.
 * 
 * Returns: bool_t_TRUE if it is, bool_t_FALSE is it is not
*/
static bool node_is_in_hierarchy(mempartmgr_attr_t *node, mempart_list_t *mpart_list)
{
	unsigned i;

	KASSERT(node->type == mpart_type_MEMPART_real);

	for (i=0; i<mpart_list->num_entries; i++)
	{
		/* does the id match directly? If so we are done */
		if (mpart_list->i[i].t.id == node->mpid)
			return bool_t_TRUE;
		else
		{
			/* if the <node> in the hierarchy of id ? */
			mempart_t *mpart = MEMPART_ID_TO_T(mpart_list->i[i].t.id);
			while ((mpart = mpart->parent) != NULL)
				if (MEMPART_T_TO_ID(mpart) == node->mpid)
					return bool_t_TRUE;
		}
	}
	return bool_t_FALSE;
}


__SRCVERSION("$IQ: mempartmgr_read.c,v 1.23 $");

