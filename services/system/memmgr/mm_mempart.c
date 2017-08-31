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

/*******************************************************************************
 * mm_mempart.c
 * 
 * This file contains code that is permanently part of the memory manager even
 * if the memory partition module is not included.
 * It includes stub routines and variables that are present outside of the
 * memory manager and therefore must be included
 * 
 * ./mempart/apm.c contains the memory partitioning code that will be
 * automatically enabled if the memory partitioning module is included in the
 * image build
*/

#include "vmm.h"

/*
 * proxy_mempart_fncTbl
 * the following table is used when no memory partitioning module is installed.
 * It is not declared static so that the MEMPART_INSTALLED() macro can test
 * against it. This eliminates a jump to unneeded stub routines
 * (like for MEMPART_CHECK_and_INCR())
*/
/* the following are proxy functions for when the memory partitioning module is not installed */
static int mempart_proc_disassociate(PROCESS *prp, mempart_id_t mempart_id, ext_lockfncs_t *lf);
static int mempart_proc_associate(PROCESS *prp, mempart_id_t mempart_id, mempart_dcmd_flags_t flags, ext_lockfncs_t *lf);
static int mempart_getlist(PROCESS *prp, mempart_list_t *mpart_list, int n, mempart_flags_t flags, struct _cred_info *cred);
static mempart_node_t *mempart_nodeget(PROCESS *prp, memclass_id_t memclass_id);
static int mempart_obj_associate(OBJECT *obj, mempart_id_t mpid);
static void proxy_mempart_init(memsize_t sysmem_size, memclass_id_t memclass_id);


mempart_fnctbl_t proxy_mempart_fnctbl =
{
	STRUCT_FLD(associate) mempart_proc_associate,
	STRUCT_FLD(disassociate) mempart_proc_disassociate,
	STRUCT_FLD(obj_associate) mempart_obj_associate,
	STRUCT_FLD(obj_disassociate) NULL,
	STRUCT_FLD(chk_and_incr) NULL,
	STRUCT_FLD(decr) NULL,
	STRUCT_FLD(incr) NULL,
	STRUCT_FLD(undo_incr) NULL,
	STRUCT_FLD(get_mempart) mempart_nodeget,
	STRUCT_FLD(get_mempartlist) mempart_getlist,
	STRUCT_FLD(validate_association) NULL,
	STRUCT_FLD(rsrcmgr_attach) NULL,
};

/*
 * mempart_fncTbl
 * 
 * This function table pointer will be initialized when the memory partitioning
 * module is initialized and mempart_init() is called.
*/
mempart_fnctbl_t  *mempart_fnctbl = NULL;

/*
 * sys_mempart
 * 
 * There are memory allocations which take place throughout the kernel which
 * do not have any objects associated with them per se.
 * In these situations, the allocation is accounted against the system
 * partition which is identified by 'sys_mempart'. This variable is initialized
 * in initialize_apm().
 * 
*/
mempart_t  *sys_mempart = NULL;

/*
 * sys_mempart_sysram_min_size
 * 
 * This variable is (optionally) used to allow control of the minimum configured
 * size of the sysram memory class of the system partition. It can be set by
 * specifying the -R option to procnto. If unspecified or incorrect, a default
 * value of 0 will be used
*/
memsize_t sys_mempart_sysram_min_size = 0;

/*
 * mempart_init
 * 
 * This function pointer will be initialized to point to _mempart_init() when
 * the memory partition module is loaded (see initialize_apm()). This will
 * allow the _mempart_init() function to be called to perform the remainder of
 * the initialization. The actual call is made through the MEMPART_INIT() macro.
*/
void (*mempart_init)(memsize_t sysmem_size, memclass_id_t memclass_id) = proxy_mempart_init;

/*
 * default_mempart_flags
 * 
 * This global specifies the default memory partitioning flags, specifically
 * what behaviour is exhibited (by default) at process creation if nothing is
 * explicitly specified. It also sets the flags which are applied to the first
 * processes created by procnto since we do not want procnto's flags inherited.
 * All of the gory details can be found for the description of mempart_flags_t
 * in mempart.h but suffice it to say that unless otherwise specified by the
 * application or configured with the -M option to procnto in the build file,
 * we will use a kernel heap per partition (ie. mempart_flags_HEAP_SHARE)
*/
mempart_dcmd_flags_t default_mempart_flags = mempart_flags_HEAP_SHARE;


/*
 * mpmgr_st_size
 * 
 * FIX ME
 * This function pointer exists to allow procfs to get the size of free memory
 * from a partitioning perspective (ie. a processes "world view" of free memory
 * is the free memory in the hieracrhy of partitions for its associated sysram
 * memory class). It is initialized in mempartmgr_init() but needs to be here
 * since we cannot reference a variable in a module
*/
int (*mpmgr_st_size)(void *attr, memsize_t *size) = NULL;


/* protos */
static int mempart_proc_disassociate_1(PROCESS *prp, mempart_t *mpart, ext_lockfncs_t *lf);

#ifndef NDEBUG
static int _list_audit(mpart_qnodehdr_t *list)
{
	unsigned count = 0;
	struct _s {
		mpart_qnode_t  hdr;
	} *n = (struct _s *)LIST_FIRST(*list);
	while(n != NULL)
	{
		++count;
		n = (struct _s *)LIST_NEXT(n);
	}
	return (count == LIST_COUNT(*list)) ? EOK : EINVAL;
}
#define prp_list_audit(_l_)		_list_audit((mpart_qnodehdr_t *)&(_l_))
#define mpart_list_audit(_l_)	_list_audit((mpart_qnodehdr_t *)&(_l_))
#endif	/* NDEBUG */

/*******************************************************************************
 * mempart_getid
 * 
 * This function will return a pointer to the partition identifier corresponding
 * to the memory class <mclass> for the process <prp>.
 * 
 * If <prp> == NULL, the system memory partition identifier will be returned
 * 
 * Returns: the partition pointer or NULL if no partition of the memory class
 * 			is associated with the process
*/
mempart_id_t mempart_getid(PROCESS *prp, memclass_id_t mclass_id)
{
	if (prp == NULL)
		return MEMPART_T_TO_ID(sys_mempart);
	else
	{
		mempart_node_t  *m = MEMPART_NODEGET(prp, mclass_id);
		return ((m == NULL) ? mempart_id_t_INVALID : MEMPART_T_TO_ID(m->mempart));
	}
}

/*******************************************************************************
 * mempart_get_classid
 * 
 * this function is used obtain the memclass_id_t for a given partition. The
 * class id can then be used in subsequent memclass_xxx() calls
 * 
 * Returns: the 'memclass_id_t' for <mpart_id> otherwise memclass_id_t_INVALID
*/
memclass_id_t mempart_get_classid(mempart_id_t mpart_id)
{
	mempart_t *mpart = MEMPART_ID_TO_T(mpart_id);
	KASSERT(mpart != NULL);
	KASSERT(mpart->memclass != NULL);
	return mpart->memclass->data.info.id;
}

/*******************************************************************************
 * free_mem
 * 
 * return the amount of free memory in the partition hierarchy starting at
 * <mpart_id>
 * 
*/
memsize_t free_mem(pid_t pid, mempart_id_t mpart_id)
{
	extern int (*mpmgr_st_size)(void *attr, memsize_t *size);
	memsize_t  st_size;
//	PROCESS *prp = proc_lookup_pid(pid);
	
//	KASSERT(prp != NULL);

	if (mpmgr_st_size != NULL)
	{
		mempart_t  *mpart = MEMPART_ID_TO_T(mpart_id);
		if (mpmgr_st_size(mpart->rmgr_attr_p, &st_size) != EOK)
			return EINVAL;
	}
	else
	{
		memclass_info_t _ci, *ci = memclass_info(sys_memclass_id, &_ci);
		
		KASSERT(ci != NULL);
		st_size = ci->size.unreserved.free;
	}
	return st_size;
}

/*******************************************************************************
 * mempart_validate_id
 * 
 * this function is used to validate a partition identifier passed in from
 * user space.
 * 
 * It returns EOK if <mempart_id> is valid and accessible
 * otherwise it returns an errno.
*/
int mempart_validate_id(mempart_id_t mpid)
{
	return ValidateMpartId(mpid);
}

/*******************************************************************************
 * mempart_module_loaded
 * 
 * this function is used to determine whether the memory partitioning module
 * is loaded or not. The module is considered to be loaded of the (*mempart_init)
 * function pointer is not NULL and not pointing to the proxy_mempart_init()
 * (ie. it has been overridden with the real memory partitioning initialization
 * function). 
 * 
 * It returns TRUE or FALSE depending on whether the memory partitioning module
 * is considered to be loaded or not.
 * 
 * Note that loaded and installed/initialized are considered to be 2 different
 * states for the memory partitioning module.
*/
bool mempart_module_loaded(void)
{
	return ((mempart_init != NULL) && (mempart_init != proxy_mempart_init));
}


/*
 * ===========================================================================
 * 
 * 					Memory Partition interface routines
 *  
 * The following routines implement the memory partitioning interface for the
 * case when the memory partitioning module is not installed. The exception are
 * the process association and disassociation functions which are effectively
 * used by both. The implementations in this file are called out through
 * wrappers in the partitioning module when it is installed (which perform
 * additional things like event notification).
 * 
 * They are exposed via the 'mempart_fncTbl' and in conjunction with
 * macros of the same name, provide the memory partitioning functionality in a format
 * which is optional. 
 *  
 * ===========================================================================
*/
/*
 * When the memory partitioning module is installed, <lf> will point to the
 * implementation specific lock/unlock functions in that module. Otherwise
 * <lf> will be NULL because we don't need to lock/unlock when no partitioning
 * is installed since process association/disassociation in that case is only
 * done from kerext_process_create() and kerext_process_destroy()
 * FIX ME - I don't think this is TRUE
*/
#define MUTEX_RDLOCK(l)		((lf == NULL) ? EOK : lf->rdlock((l)))
#define MUTEX_RDUNLOCK(l)	((lf == NULL) ? EOK : lf->rdunlock(l))
#define MUTEX_WRLOCK(l)		((lf == NULL) ? EOK : lf->wrlock((l)))
#define MUTEX_WRUNLOCK(l)	((lf == NULL) ? EOK : lf->wrunlock(l))

/*******************************************************************************
 * mempart_proc_associate
 * 
 * Associate process <prp> with the memory partition <mempart_id>.
 * A process can only be associated with 1 partition of a given memory class.
 * Attempts to associate with an already associated partition will return
 * EALREADY.
 * 
 * HEAP structure creation is controlled by the <flags> argument. This allows
 * the caller to specify whether or not a HEAP is considered necessary. Some
 * memory classes may not require a HEAP since it is known that no internal
 * allocations will be made to that memory class. In this case, creation of a
 * HEAP would be wasteful requiring that at least 1 page of the memory class be
 * allocated just to hold the HEAP structure which would go unused.
 * For this case, the 'mempart_node_t' structure will be allocated from the
 * processes 'sysram' heap and have its HEAP pointer set to NULL (ie no heap
 * for the memory class even though it has partition association with the class)
 * 
 * All of the partition structures required for process association, currently
 * use the sysram memory class for their allocations hence when associating with
 * the sysram memory class, it is expected that a HEAP be either shared or created
 * (ie. flags & mempart_flags_HEAP_CREATE | mempart_flags_HEAP_SHARE) since we
 * know that most (if not all) internal memory allocations will be made from the
 * sysram heap for the process. See also mempart.h
 * 
 * Returns EOK or an errno
*/
static int mempart_proc_associate(PROCESS *prp, mempart_id_t mempart_id,
									mempart_dcmd_flags_t flags, ext_lockfncs_t *lf)
{
	mempart_t  *mempart = MEMPART_ID_TO_T(mempart_id);
	mempart_node_t  *mpart_node;

	KASSERT(mempart != NULL);

	if (mempart == NULL)
	{
		return EINVAL;
	}
	else if ((mpart_node = MEMPART_NODEGET(prp, mempart->memclass->data.info.id)) != NULL)
	{
		/*
		 * a partition of <memclass_id> is already associated with this process.
		 * Return EALREADY if it's the same partition (allowing the caller to
		 * determine if it thinks this is an error) or EEXIST if it's a different
		 * partition (of the same memory class) which is an error
		*/
		return (mpart_node->mempart == mempart) ? EALREADY : EEXIST;
	}
	else
	{
#ifdef USE_PROC_OBJ_LISTS
		prp_node_t  *prp_node;
#endif	/* USE_PROC_OBJ_LISTS */

		if ((mpart_node = calloc(1, sizeof(*mpart_node))) == NULL)
		{
			return ENOMEM;
		}

		/* finish initializing the mpart_node_t structure (all other fields are NULL) */
		mpart_node->mempart = mempart;
		mpart_node->flags = flags;
		mpart_node->mem_used = sizeof(*mpart_node);
#ifdef MEMPART_DFLT_INHERIT_BEHVR_2
		/* behaviour 2 means all but the sysram class partition are not inheritable */
		if (mempart->memclass->id != sys_memclass_id)
			mpart_node->flags |= MEMPART_NO_INHERIT;
#endif	/* MEMPART_DFLT_INHERIT_BEHVR_2 */

#ifdef USE_PROC_OBJ_LISTS
		if ((prp_node = calloc(1, sizeof(*prp_node))) == NULL)
		{
			free(mpart_node);
			return ENOMEM;
		}
		mpart_node->mem_used += sizeof(*prp_node);
#endif	/* USE_PROC_OBJ_LISTS */

		/* add 'mpart_node' to the partition list for the process */
		MUTEX_WRLOCK(&prp->mpartlist_lock);
		LIST_ADD(prp->mpart_list, mpart_node);
		KASSERT(mpart_list_audit(prp->mpart_list) == EOK);
		MUTEX_WRUNLOCK(&prp->mpartlist_lock);

		/*
		 * from here on in per process accounting can be done because we have
		 * completed associating with the partition (we have a 'mempart_node_t')
		*/

#ifdef USE_PROC_OBJ_LISTS
		/* add 'prp' to the process list for the partition */
		prp_node->prp = prp;
		
		MUTEX_WRLOCK(&mpart_node->mempart->prplist_lock);
		LIST_ADD(mpart_node->mempart->prp_list, prp_node);
		KASSERT(prp_list_audit(mpart_node->mempart->prp_list) == EOK);
		MUTEX_WRUNLOCK(&mpart_node->mempart->prplist_lock);
#endif	/* USE_PROC_OBJ_LISTS */

		return EOK;
	}
}

/*******************************************************************************
 * mempart_proc_disassociate
 * 
 * Remove the process <prp> from the process list for partition <mpart>.
 * If <mpart> is NULL, the process is disassociate with all partitions in
 * its partition list.
 * If mpart is not NULL, then it is also removed from the process list for <prp>
 * _mempart_proc_disassociate_1() actually does the work.
*/
static int mempart_proc_disassociate(PROCESS *prp, mempart_id_t mempart_id, ext_lockfncs_t *lf)
{
	mempart_t  *mpart = MEMPART_ID_TO_T(mempart_id);

	KASSERT(prp != NULL);

	/* allow the caller to do a cleanup even if no partitions were associated */
	if ((mpart == NULL) && (LIST_FIRST(prp->mpart_list) == NULL))
		return EOK;

	/* note that mpart == NULL is only used internally when a process is destroyed */
	if (mpart != NULL)
		return mempart_proc_disassociate_1(prp, mpart, lf);
	else
	{
		int  ret = EOK;
		/* remove the process from all of its associated partition process lists */
		while (LIST_FIRST(prp->mpart_list) != NULL)
		{
			int  r;
			mpart = ((mempart_node_t *)LIST_FIRST(prp->mpart_list))->mempart;
			r = mempart_proc_disassociate_1(prp, mpart, lf);
			ret = ((r != EOK) ? r : ret);
		}
		return ret;
	}
}

/*******************************************************************************
 * mempart_getlist
 * 
 * This is a proxy handler for the case when the memory partitioning module is
 * not installed. It minimally replicates the behaviour of the
 * _mempart_getlist() function in apm.c.
 * 
 * Since there are no actual partitions, this function always returns the
 * same thing
*/
static int mempart_getlist(PROCESS *prp, mempart_list_t *mpart_list, int n,
							mempart_flags_t flags, struct _cred_info *cred)
{
	mempart_node_t *mp_node = (mempart_node_t *)LIST_FIRST(prp->mpart_list);

	KASSERT(!MEMPART_INSTALLED());

	mpart_list->num_entries = 0;
	mpart_list->i[mpart_list->num_entries].t.id = MEMPART_T_TO_ID(mp_node->mempart);
	mpart_list->i[mpart_list->num_entries++].flags = mp_node->flags;
	return EOK;
}

/*******************************************************************************
 * mempart_nodeget
 * 
 * This is a proxy handler for the case when the memory partitioning module is
 * not installed. It minimally replicates the behaviour of the _mempart_get()
 * function in apm.c.
 * 
 * Since there are no actual partitions, this function always returns the
 * same thing
*/
static mempart_node_t *mempart_nodeget(PROCESS *prp, memclass_id_t memclass_id)
{
	KASSERT(!MEMPART_INSTALLED());
	KASSERT(prp != NULL);

	return (mempart_node_t *)LIST_FIRST(prp->mpart_list);
}

/*******************************************************************************
 * mempart_obj_associate
 * 
 * This is a proxy handler for the case when the memory partitioning module is
 * not installed. It minimally replicates the behaviour of the
 * _mempart_obj_associate() function in apm.c.
*/
static int mempart_obj_associate(OBJECT *obj, mempart_id_t mpid)
{
	KASSERT(!MEMPART_INSTALLED());

	if (obj->hdr.mpid == mempart_id_t_INVALID)
	{
		obj->hdr.mpid = mpid;
		obj->hdr.mpart_disassociate = NULL;
	}
	/* if its not NULL, it better be the same as what's being passed in */
	else {KASSERT(obj->hdr.mpid == mpid);}

	return EOK;
}

/*
 * ===========================================================================
 * 
 * 								support routines
 *  
 * ===========================================================================
*/


/*******************************************************************************
 * mempart_disassociate_1
 * 
 * Disassociate process <prp> from the partition <mpart>
 * 
 * Returns: EOK on success, otherwise and errno
*/
static int mempart_proc_disassociate_1(PROCESS *prp, mempart_t *mpart, ext_lockfncs_t *lf)
{
	memclass_id_t  memclass_id;
	mempart_node_t *p, *n;

	KASSERT(mpart != NULL);
	KASSERT(prp != NULL);
	KASSERT(LIST_FIRST(prp->mpart_list) != NULL);

#ifdef USE_PROC_OBJ_LISTS
	/* remove <prp> from the process list for the partition */
	MUTEX_WRLOCK(&mpart->prplist_lock);
	{
		prp_node_t *pp = (prp_node_t *)LIST_FIRST(mpart->prp_list);
		prp_node_t *nn = pp;

		while ((nn != NULL) && (nn->prp != prp))
		{
			pp = nn;
			nn = (prp_node_t *)LIST_NEXT(nn);
		}
		if (nn != NULL)
		{
			LIST_DEL(mpart->prp_list, nn);
			KASSERT(prp_list_audit(mpart->prp_list) == EOK);
			MUTEX_WRUNLOCK(&mpart->prplist_lock);
			free(nn);
  		}
  		else	/* prp not found */
  		{
  			MUTEX_WRUNLOCK(&mpart->prplist_lock);
#ifndef NDEBUG
			crash();
#endif	/* NDEBUG */
  		}
	}
#endif	/* USE_PROC_OBJ_LISTS */

	/* remove <mpart> from the partition list for the process */
	MUTEX_WRLOCK(&prp->mpartlist_lock);
	memclass_id = mpart->memclass->data.info.id;

	p = (mempart_node_t *)LIST_FIRST(prp->mpart_list);
	n = p;

	while ((n != NULL) && (n->mempart != mpart))
	{
		p = n;
		n = (mempart_node_t *)LIST_NEXT(n);
	}
	if (n != NULL)	/* mpart not found */
	{
		LIST_DEL(prp->mpart_list, n);
		KASSERT(mpart_list_audit(prp->mpart_list) == EOK);
		MUTEX_WRUNLOCK(&prp->mpartlist_lock);
		free(n);
	}
	else
	{
		MUTEX_WRUNLOCK(&prp->mpartlist_lock);
#ifndef NDEBUG
		crash();
#endif	/* NDEBUG */
	}

	return EOK;
}

/*******************************************************************************
 * proxy_mempart_init
 * 
 * This is a proxy handler for the case when the memory partitioning module is
 * not installed. It minimally replicates the behaviour of the
 * _mempart_init() function in apm.c.
 * 
*/
static void proxy_mempart_init(memsize_t sysmem_size, memclass_id_t memclass_id)
{
	/*
	 * this structure exists only so that sys_mempart->memclass->data.info.id
	 * dereferences can be successfully made. There will be no wasteful accounting
	*/
	static mempart_t _sys_mempart;

// not required - 	memset(&_sys_mempart, 0, sizeof(_sys_mempart));
	_sys_mempart.signature = MEMPART_SIGNATURE;
	_sys_mempart.memclass = memclass_find(NULL, sys_memclass_id);

	/* can only be called once */	
	if ((sys_mempart != NULL) || (memclass_id == NULL))
	{
		kprintf("Unable to create system partition, sys_mempart: 0x%x, memclass_id: 0x%x\n",
				sys_mempart, memclass_id);
		crash();
	}

	sys_mempart = &_sys_mempart;

	mempart_fnctbl = &proxy_mempart_fnctbl;
}

/*
 * ===========================================================================
 * 
 * The following will pull in libc routines into the the main procnto build
 * so that they are available when a module is included. Currently any function
 * used by a module must already be included in the base procnto build so this
 * is how it will be done for partitioning modules
 *  
 * ===========================================================================
*/
void dummy_func(void)
{
	volatile char *s = (char *)dummy_func;
	volatile unsigned int x = 0;
	(void)memchr((char *)s, 0, 0);
	(void)atomic_add_value(&x, 1);
	(void)atomic_sub_value(&x, 1);
}

	
__SRCVERSION("$IQ: mm_mempart.c,v 1.7 $");

