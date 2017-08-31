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
 * apm.c
 * 
 * This file contains code that implements memory partitioning when the module
 * is loaded.
 * 
 * See also ../mm_mempart.c
*/
#include "vmm.h"
#include "apm_internal.h"

#if !defined(NDEBUG) || defined(__WATCOMC__)
	#define INLINE
#else	/* NDEBUG */
	#define INLINE	__inline__
#endif	/* NDEBUG */


/* accessed as MEMPART_DEFAULT_POLICY */
static const mempart_policy_t  _mempart_dflt_policy = MEMPART_DFLT_POLICY_INITIALIZER;
#define MEMPART_DFLT_POLICY		_mempart_dflt_policy

/*
 * rsrcmgr_mempart_fnctbl
 * 
 * When the memory partitioning resource manager initializes, it can optionally
 * register a table of interface functions for use by the memory partioning
 * module. In this case the 'rsrcmgr_mempart_fnctbl' variable will be set
 * accordingly and the following public API's enabled
*/
static rsrcmgr_mempart_fnctbl_t *rsrcmgr_mempart_fnctbl = NULL;

/*==============================================================================
 * 
 *			public interfaces to memory partition resource manager
 * 
*/
/*
 * MEMPARTMGR_EVENT
 * 
 * This API is used to send event notifications to any registered process. Event
 * delivery is handled entirely from the resource manager.
 * 
 * Mandatory arguments include the mempart_t.resmgr_attr_p and the event type
 * (mempart_evttype_t). Depending on the event type (etype), additional event
 * specific arguments may be provided.
 * 
 * If the resource manager has elected not to register an event handler, this
 * API is a no-op.
 * 
 * Returns: nothing 
*/
#ifdef MEMPART_NO_EVENTS
/* WATCOM compiler can't do vararg macros #define MEMPARTMGR_EVENT(mpid, etype, args...)	{} */
#define MEMPARTMGR_EVENT(mpart, etype, arg1, arg2)	{}
#else	/* MEMPART_NO_EVENTS */
/* WATCOM compiler can't do vararg macros #define MEMPARTMGR_EVENT(mpid, etype, args...) \*/
#define MEMPARTMGR_EVENT(mpart, etype, arg1, arg2) \
		do { \
			if ((rsrcmgr_mempart_fnctbl != NULL) && \
				(rsrcmgr_mempart_fnctbl->mpart_event_handler != NULL) && \
				((mpart) != NULL)) \
				/* rsrcmgr_mempart_fnctbl->mpart_event_handler(mpart->rmgr_attr_p, (etype) , ## args);*/ \
				rsrcmgr_mempart_fnctbl->mpart_event_handler(mpart->rmgr_attr_p, (etype) , arg1, arg2); \
		} while(0)
#endif	/* MEMPART_NO_EVENTS */

/*
 * _mempart_rsrcmgr_fnctbl
 * 
 * This table defines the memory partitioning interface routines provided for
 * the resource manager porition of the memory partitioning module. When the
 * resource manager module registers (_mempart_fnctbl.rsrcmgr_attach()), it will
 * be returned a pointer to this table so that it can interact with the memory
 * partitioning module.
 * 
*/
static mempart_id_t		_mempart_create(mempart_id_t parent_mpid, mempart_cfg_t *child_cfg, memclass_id_t memclass);
static int				_mempart_destroy(mempart_id_t mpid);
static int 				_mempart_config(mempart_id_t mpid, mempart_cfg_t *cfg);
static mempart_info_t *	_mempart_getinfo(mempart_id_t mpid, mempart_info_t *info);
static PROCESS *		_mempart_find_pid(pid_t pid, mempart_id_t mpid);
static int _validate_cfg_creation(mempart_id_t parent_mpid, mempart_cfg_t *cfg, memclass_id_t memclass_id);
static int _validate_cfg_modification(mempart_id_t mpid, mempart_cfg_t *cfg);

static mempart_rsrcmgr_fnctbl_t _mempart_rsrcmgr_fnctbl =
{
	STRUCT_FLD(create) _mempart_create,
	STRUCT_FLD(destroy) _mempart_destroy,
	STRUCT_FLD(config) _mempart_config,
	STRUCT_FLD(getinfo) _mempart_getinfo,
	STRUCT_FLD(find_pid) _mempart_find_pid,
	STRUCT_FLD(validate_cfg_new) _validate_cfg_creation,
	STRUCT_FLD(validate_cfg_change) _validate_cfg_modification,
};

/*
 * _mempart_fnctbl
 * 
 * This table defines the memory partitioning interface routines for the
 * accounting portion of the implementation. When the memory partitioning module
 * is included, these interface routines are exposed to external code through
 * the 'mempart_fnctbl'.
 * This table is not (typically) accessed directly but rather through the
 * API macros define in mm_mempart_internal.h
*/
static int			_mempart_proc_associate(PROCESS *prp, mempart_id_t mpid, mempart_dcmd_flags_t flags, ext_lockfncs_t *lf);
static int			_mempart_proc_disassociate(PROCESS *prp, mempart_id_t mpid, ext_lockfncs_t *lf);
static int			_mempart_obj_associate(OBJECT *obj, mempart_id_t mpid);
static void			_mempart_obj_disassociate(OBJECT *obj);
static int			_mempart_chk_and_incr(mempart_id_t mpid, memsize_t incr, memsize_t *resv_size);
static memsize_t	_mempart_decr(mempart_id_t mpid, memsize_t decr);
static int			_mempart_incr(mempart_id_t mpid, memsize_t incr);
static void			_mempart_undo_incr(mempart_id_t mpid, memsize_t incr, memsize_t resv_size);
static mempart_node_t *	_mempart_get(PROCESS *prp, memclass_id_t memclass_id);
static int			_mempart_getlist(PROCESS *prp, mempart_list_t *mpart_list, int n,
									 mempart_flags_t flags, struct _cred_info *cred);
static int			_mempart_validate_association(mempart_id_t mpid, struct _cred_info *cred);
static mempart_rsrcmgr_fnctbl_t *_rsrcmgr_attach(rsrcmgr_mempart_fnctbl_t *ifTbl);

static mempart_fnctbl_t	_mempart_fnctbl =
{
	STRUCT_FLD(associate) _mempart_proc_associate,
	STRUCT_FLD(disassociate) _mempart_proc_disassociate,
	STRUCT_FLD(obj_associate) _mempart_obj_associate,
	STRUCT_FLD(obj_disassociate) NULL,	/* implemented through OBJECT structure mempart_obj_disassociate, */
	STRUCT_FLD(chk_and_incr) _mempart_chk_and_incr,
	STRUCT_FLD(decr) _mempart_decr,
	STRUCT_FLD(incr) _mempart_incr,
	STRUCT_FLD(undo_incr) _mempart_undo_incr,
	STRUCT_FLD(get_mempart) _mempart_get,
	STRUCT_FLD(get_mempartlist) _mempart_getlist,
	STRUCT_FLD(validate_association) _mempart_validate_association,
	STRUCT_FLD(rsrcmgr_attach) _rsrcmgr_attach,
};

/*
 * Forward declarations
*/
static int			mempart_reserve(mempart_t *mp, memsize_t size, memsize_t child_min, memclass_id_t memclass_id);
static void			mempart_unreserve(mempart_t *mp, memsize_t size, memsize_t child_min, memclass_id_t memclass_id);
static void			_mempart_init(memsize_t sysmem_size, memclass_id_t memclass_id);
static memsize_t	hierarchy_chk(mempart_t *mp, memsize_t size);
static memsize_t	hierarchy_incr(mempart_t *mp, memsize_t size);
static memsize_t	hierarchy_decr(mempart_t *mp, memsize_t size);
static bool			mempart_size_chk(mempart_t *mp, memsize_t size);
static void			mempart_size_incr(mempart_t *mp, memsize_t inc);
static void			mempart_size_decr(mempart_t *mp, memsize_t dec);
static int			mempart_validate_association(mempart_t *mpart, struct _cred_info *cred);

#ifndef NDEBUG
static void display_mempart_info(mempart_t *mp);
#endif	/* NDEBUG */

/*
 * lockFncs_t and supporting variables/macros
*/
typedef struct lockfncs_s
{
	int		(*lock)(pthread_mutex_t *);
	int		(*unlock)(pthread_mutex_t *);
	int		(*init)(pthread_mutex_t *, const pthread_mutexattr_t *);
	int		(*destroy)(pthread_mutex_t *);
} lockfncs_t;

static int	_fake_lock(pthread_mutex_t *l) {return EOK;}
static int	_fake_unlock(pthread_mutex_t *l) {return EOK;}
static int	_fake_init(pthread_mutex_t *l, const pthread_mutexattr_t *a) {return EOK;}
static int	_fake_destroy(pthread_mutex_t *l) {return EOK;}

#if 0
static int my_mutex_init(pthread_mutex_t *lock, const pthread_mutexattr_t *a)
{
	extern SYNC *_k_sync_create(PROCESS *prp, sync_t *sync, unsigned flags, int *err); // FIX ME
	const pthread_mutex_t tmp = { 0, _NTO_SYNC_MUTEX_FREE };
	/* system partition (via procnto_prp) pays the cost of partition SYNC objects */
	*lock = tmp;
#ifndef NDEBUG
	if (_k_sync_create(procnto_prp, lock, 0, NULL) == NULL)
		crash();
#else	/* NDEBUG */
	_k_sync_create(procnto_prp, lock, 0, NULL);
#endif	/* NDEBUG */
	return EOK;
}
#endif

static lockfncs_t fake = {
		STRUCT_FLD(lock) _fake_lock,
		STRUCT_FLD(unlock) _fake_unlock,
		STRUCT_FLD(init) _fake_init,
		STRUCT_FLD(destroy) _fake_destroy,
		};
static lockfncs_t real = {
		STRUCT_FLD(lock) pthread_mutex_lock,
		STRUCT_FLD(unlock) pthread_mutex_unlock,
		STRUCT_FLD(init) pthread_mutex_init,
//		STRUCT_FLD(init) my_mutex_init,
		STRUCT_FLD(destroy) pthread_mutex_destroy,
		};
static lockfncs_t	*mutex = &fake;

static pthread_mutexattr_t		info_lock_attr = PTHREAD_RMUTEX_INITIALIZER;

/* kermacros.h stuff */
#define PID_MASK			0xfff
#define PINDEX(pid)			((pid) & PID_MASK)
#define SYNC_PINDEX(owner)	PINDEX(((owner) & ~_NTO_SYNC_WAITING) >> 16)
#define SYNC_OWNER_BITS(pid,tid)	((((pid) << 16) | (tid) + 1) & ~_NTO_SYNC_WAITING)
#define SYNC_OWNER(thp)	SYNC_OWNER_BITS((thp)->process->pid, (thp)->tid)

volatile unsigned int mempart_ker_int_lock_cnt = 0;

static INLINE int _mtx_lock(void *m, int line)
{
	kerproc_lock_t *lock = (kerproc_lock_t *)m;

	if (KerextAmInKernel())
	{
#ifndef NDEBUG
		extern THREAD *actives[];
		if ((lock->wr_cnt > 0) && (lock->rd_cnt > 0) &&
			(lock->mutex.__owner != _NTO_SYNC_DESTROYED) &&
			(actives[RUNCPU] != NULL) && (lock->mutex.__owner & ~_NTO_SYNC_WAITING) &&
			((lock->mutex.__owner & ~_NTO_SYNC_WAITING) != SYNC_OWNER(actives[RUNCPU])))
		{
			kprintf("lock clash with 0x%x @ %d\n", lock->mutex.__owner, line);
#ifndef DISABLE_ASSERT
			crash();
#endif	/* DISABLE_ASSERT */
		}
#endif	/* NDEBUG */
		return EOK;
	}
	else
	{
		int r = mutex->lock(&lock->mutex);
		if (r == EOK)
		{
//			if ((lock->mutex.__count & _NTO_SYNC_COUNTMASK) == 1)
			if (atomic_add_value(&mempart_ker_int_lock_cnt, 1) == 0)
				InterruptLock(&lock->spin); /* FIX ME - temp sync with kernel */
		}
#ifndef NDEBUG
		else
		{
			kprintf("Lock failed at line %d, errno %d\n", line, r);
//#ifndef DISABLE_ASSERT
			crash();
//#endif	/* DISABLE_ASSERT */
		}
#endif	/* NDEBUG */

		return r; // quiet compiler with the return
	}
}

static INLINE int _mtx_unlock(void *m, int line)
{
	if (KerextAmInKernel())
	{
		return EOK;
	}
	else
	{
		kerproc_lock_t *lock = (kerproc_lock_t *)m;

//		if ((lock->mutex.__count & _NTO_SYNC_COUNTMASK) == 1)
		if (atomic_sub_value(&mempart_ker_int_lock_cnt, 1) == 1)
			InterruptUnlock(&lock->spin); /* FIX ME - temp sync with kernel */
#ifndef NDEBUG
		{
			int r;
			if ((r = mutex->unlock(&lock->mutex)) != EOK) crash();
			return r;
		}
#else	/* NDEBUG */
		return mutex->unlock(&lock->mutex);
#endif	/* NDEBUG */
	}
}

static INLINE int _mtx_init(void *m, void *a)
{
	kerproc_lock_t *lock = (kerproc_lock_t *)m;

	lock->spin.value = 0;
	lock->rd_cnt = lock->wr_cnt = 0;
	return mutex->init(&lock->mutex, (pthread_mutexattr_t *)(a));
}

#define MUTEX_LOCK(l)		(_mtx_lock((l), __LINE__))
#define MUTEX_UNLOCK(l)		(_mtx_unlock((l), __LINE__))
#define MUTEX_INIT(l, a)	(_mtx_init((l), (&info_lock_attr)))
#define MUTEX_DESTROY(l)	(mutex->destroy(&(l)->mutex))
#define USING_MUTEXES		(mutex == &real)

// FIX ME - macros indicate the intention regardless of the lock implementation

static INLINE int _mutex_rdlock(kerproc_lock_t *l)
{
	atomic_add_value((unsigned int *)&l->rd_cnt, 1);
	return MUTEX_LOCK((void *)l);
}
static INLINE int _mutex_rdunlock(kerproc_lock_t *l)
{
	atomic_sub_value((unsigned int *)&l->rd_cnt, 1);
	return MUTEX_UNLOCK(l);
}
static INLINE int _mutex_wrlock(kerproc_lock_t *l)
{
	atomic_add_value((unsigned int *)&l->wr_cnt, 1);
	return MUTEX_LOCK((void *)l);
		
}
static INLINE int _mutex_wrunlock(kerproc_lock_t *l)
{
	atomic_sub_value((unsigned int *)&l->wr_cnt, 1);
	return MUTEX_UNLOCK(l);
}
#define MUTEX_RDLOCK(l)		_mutex_rdlock((l))
#define MUTEX_RDUNLOCK(l)	_mutex_rdunlock((l))
#define MUTEX_WRLOCK(l)		_mutex_wrlock((l))
#define MUTEX_WRUNLOCK(l)	_mutex_wrunlock((l))


static int _externally_callable_rdlock_func(void *l) {return MUTEX_RDLOCK(l);}
static int _externally_callable_rdunlock_func(void *l) {return MUTEX_RDUNLOCK(l);}
static int _externally_callable_wrlock_func(void *l) {return MUTEX_WRLOCK(l);}
static int _externally_callable_wrunlock_func(void *l) {return MUTEX_WRUNLOCK(l);}
static ext_lockfncs_t ext_lock_fncs =
{
	STRUCT_FLD(rdlock) _externally_callable_rdlock_func,
	STRUCT_FLD(rdunlock) _externally_callable_rdunlock_func,
	STRUCT_FLD(wrlock) _externally_callable_wrlock_func,
	STRUCT_FLD(wrunlock) _externally_callable_wrunlock_func
};


#ifndef NDEBUG
static INLINE int _list_audit(mpart_qnodehdr_t *list, unsigned int *ret)
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
	if (ret != NULL) *ret = count;
	return (count == LIST_COUNT(*list)) ? EOK : EINVAL;
}
#define prp_list_audit(_l_)		_list_audit((mpart_qnodehdr_t *)&(_l_), NULL)
#define mpart_list_audit(_l_)	_list_audit((mpart_qnodehdr_t *)&(_l_), NULL)
#define obj_list_audit(_l_)		_obj_list_audit((mpart_qnodehdr_t *)&(_l_))
static INLINE int _obj_list_audit(mpart_qnodehdr_t *list)
{
	unsigned int cnt = 0;
	int r;
	if ((r = _list_audit(list, &cnt)) != EOK)
		kprintf("obj list cnt %u does not match %u counted\n", LIST_COUNT(*list), cnt);
	return r;
}
#endif	/* NDEBUG */

/*******************************************************************************
 * reserve_size
 * 
 * calculate how much of <req_size> will come from any reserve established in
 * partition <mp>.
 * Note that when this macro is called, the mp->info.cur_size has not yet been
 * incremented so the allocation has not yet taken place from the perspective of
 * the partition.
 * 
 * if (cur_size < size.min) (ie. there is some reserve to use up)
 *   return (min(size.min - cur_size, req_size))
 * else
 * 	 return 0	(ie. reserved (or guarantee is already used up)
*/
static INLINE memsize_t reserve_size(mempart_info_t *meminfo, memsize_t req_size)

{
#ifdef _MEMSIZE_and_memsize_are_different_
	register const memsize_t  min_size = meminfo->cur_cfg.attr.size.min;
	register const memsize_t  cur_size = meminfo->cur_size;
#else	/* _MEMSIZE_and_memsize_are_different_ */
#define min_size	meminfo->cur_cfg.attr.size.min
#define cur_size	meminfo->cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */

	if (cur_size < min_size)
		return (min(min_size - cur_size, req_size));
	else
		return 0;

#ifndef _MEMSIZE_and_memsize_are_different_
#undef min_size
#undef cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */
}

/*******************************************************************************
 * unreserve_size
 * 
 * calculate how much of <free_size> will go back to reserved such that any
 * reserve established in partition <mp> is maintained.
 * Note that when this macro is called, the mp->info.cur_size has not yet been
 * decremented so the allocation has not yet been freed from the perspective of
 * the partition
 * 
 * if (cur_size <= size.min)
 *   return (free_size)			(ie. it all goes back to reserved)
 * else if ((cur - min) > free)
 * 	 return 0
 * else
 * 	 return (free_size - (cur_size - size.min))
*/
static INLINE memsize_t unreserve_size(mempart_info_t *meminfo, memsize_t free_size)
{
#ifdef _MEMSIZE_and_memsize_are_different_
	register const memsize_t  min_size = meminfo->cur_cfg.attr.size.min;
	register const memsize_t  cur_size = meminfo->cur_size;
#else	/* _MEMSIZE_and_memsize_are_different_ */
#define min_size	meminfo->cur_cfg.attr.size.min
#define cur_size	meminfo->cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */

	if (cur_size <= min_size)
		return free_size;
	else if ((cur_size - min_size) > free_size)
		return 0;
	else
		return (free_size - (cur_size - min_size));

#ifndef _MEMSIZE_and_memsize_are_different_
#undef min_size
#undef cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */
}

/*******************************************************************************
 * initialize_apm
 * 
 * Called from kmain.c during module initialization. This function will
 * initialize the (*mempart_init)() function pointer so that when MEMPART_INIT()
 * is called, the _mempart_init() function can perform the actual module
 * initialization. Doing that initialization now is too early in the kernel
 * startup sequence because memory information is not available nor is the
 * ability to perform allocations.
*/
void initialize_apm(unsigned version, unsigned pass)
{
	/*
	 * assert(sizeof(memsize_t) <= sizeof(_MEMSIZE_T_));
	 * leave this in. Proper operation depends on it
	*/
#ifndef __WATCOMC__
	KASSERT(sizeof(memsize_t) <= sizeof(_MEMSIZE_T_));
#endif	/* __WATCOMC__ */

	if(version != LIBMOD_VERSION_CHECK) {
		kprintf("Version mismatch between procnto (%d) and libmod_mempart (%d)\n",
					version, LIBMOD_VERSION_CHECK);
		crash();
	}
	switch(pass)
	{
		case 0:
		{
			mempart_init = _mempart_init;
#ifndef NDEBUG
			kprintf("memory partitioning module (apm) loaded\n");
#endif	/* NDEBUG */
			break;
		}
		case 5:
		{
			int r;
			
			/* initialize the sys_mempart->info_lock (it could not be done earlier) */
			mutex->init = real.init;

			if ((r = MUTEX_INIT(&sys_mempart->info_lock.mutex, &info_lock_attr)) != EOK)
			{
				kprintf("sys_mempart info lock init failed, err %d\n", r);
				crash();
			}
			if ((r = MUTEX_INIT(&sys_mempart->prplist_lock.mutex, &info_lock_attr)) != EOK)
			{
				kprintf("sys_mempart prp_list lock init failed, err %d\n", r);
				crash();
			}
			if ((r = MUTEX_INIT(&sys_mempart->objlist_lock.mutex, &info_lock_attr)) != EOK)
			{
				kprintf("sys_mempart obj_list lock init failed, err %d\n", r);
				crash();
			}

			/* start using the real lock functions */
			mutex = &real;

#ifndef NDEBUG
			if (ker_verbose > 1) kprintf("apm lock functions%s initialized\n", (mutex == &real) ? "" : " NOT");
#endif	/* NDEBUG */
		}
		default:
			break;
	}
}


/*
 * ===========================================================================
 * 
 * 					Memory Partition interface routines
 *  
 * The following routines implement the memory partitioning interface for the
 * case when the memory partitioning module is installed.
 * They are exposed via the 'mempart_fnctbl' and in conjunction with macros of
 * the same name, provide the memory partitioning functionality in a format
 * which is optional. 
 *  
 * ===========================================================================
*/

/*******************************************************************************
 * mempart_init
 * 
 * Initialize the memory partition system. Called from pa_memclass_init(), this
 * function will create the initial system partition and establish initial values
 * based on the amount of system memory available and currently used
 *
 * Bootstrapping the partitions
 * 
 * In order to utilize the _mempart_create() function to actually do the work
 * (which will in turn do a memory allocation, which would require a valid
 * memory partition with enough space, etc ... which we don't have) the
 * creation of the system partition is done *before* assigning the mempart_fnctbl
 * pointer thus preventing partition accounting until the first partition is
 * created.
 *
 * The system partition will be created with the memory class of <memclass_id>
 * and size specified by <sysmem_size>.
*/
static void _mempart_init(memsize_t sysmem_size, memclass_id_t memclass_id)
{
	mempart_cfg_t  sys_cfg;

	/* can only be called once */	
	if ((sys_mempart != NULL) || (memclass_id == NULL))
	{
		kprintf("Unable to create system partition, sys_mempart: 0x%x, memclass_id: 0x%x\n",
				sys_mempart, memclass_id);
		crash();
	}

	/*
	 * create the initial system partition. Since 'mempart_fnctbl' is not
	 * yet assigned, don't need to worry about the allocations made in
	 * _mempart_create(). The adjustments will be made after the system
	 * partition is created.
	*/
	memset(&sys_cfg, 0, sizeof(sys_cfg));

	/*
	 * system partition is autonomous and can have children. User with
	 * approriate privileges can change if they like. The 'max' size attribute
	 * is intentionally set to the maximum size of the system ram memory class.
	 * The user can change it if desired (for example to memsize_t_INFINITY to
	 * dynamically accomodate changes to the physical amount of system ram)
	*/
	sys_cfg.policy.terminal = bool_t_FALSE;
	sys_cfg.policy.config_lock = bool_t_FALSE;
	sys_cfg.policy.permanent = bool_t_TRUE;		// can never remove the system partition
	sys_cfg.policy.alloc = mempart_alloc_policy_t_ROOT;

	sys_cfg.attr.size.max = sysmem_size;
	if (sys_mempart_sysram_min_size <= sys_cfg.attr.size.max)
		sys_cfg.attr.size.min = sys_mempart_sysram_min_size;
	else
		sys_cfg.attr.size.min = 0;
	sys_mempart = MEMPART_ID_TO_T(_mempart_create(NULL, &sys_cfg, memclass_id));

	if (sys_mempart == NULL)
	{
		kprintf("Unable to properly create system memory partition\n"
				"Check -R option to procnto in build file\n");
		crash();
	}

	/*
	 * At this point we have a system partition. Prior to creating the system
	 * partition, the physical allocator for the system memory class had already
	 * handed out memory so that amount will be accounted to the system partition
	 * (as if the system partition was available at initial boot). One tricky part
	 * is that if there is any reserved memory for the system partition, we need
	 * to make sure that the previous allocations are against the reserve (as
	 * would be the case had the system partition been available since boot). In
	 * other words, the configured size.min for the system partition could
	 * theorectically be used up already depending on what allocations were made
	 * prior to creating the system partition.
	 * 
	 * The total free memory for the system memory class is represented by
	 * (pa_free_size() + pa_reserved_size()), therefore what has been used so
	 * far (ie. cur_size) is the configured size.max - the total free. Any memory
	 * used upto the configured size.min must be accounted for against reserved
	 * memory
	*/
	
// FIX ME - consider pa_xx() funcs either through class allocator functions or return
//			values as arguments
	sys_mempart->info.cur_size =
		sysmem_size/*sys_mempart->info.cre_cfg.attr.size.max*/ - (pa_free_size() + pa_reserved_size());
	sys_mempart->info.hi_size = sys_mempart->info.cur_size;
	if (sys_mempart->info.cur_size >= sys_mempart->info.cur_cfg.attr.size.min)
		MEMCLASS_RUSED_ADJ_UP(sys_mempart, sys_mempart->info.cur_cfg.attr.size.min);	// no reserve actually left
	else
		MEMCLASS_RUSED_ADJ_UP(sys_mempart, sys_mempart->info.cur_size);	// some reserve will be left

	/*
	 * initialize memory partition manager function table pointers.
	 * From here on in, the system partition will govern the allocation of
	 * memory as well as the creation of child partitions
	*/
	mempart_fnctbl = &_mempart_fnctbl;
	
#ifndef NDEBUG
	{
		static void display_mempart_info(mempart_t *mp);
		display_mempart_info(sys_mempart);
	}
#endif	/* NDEBUG */
}

/*******************************************************************************
 * _mempart_proc_associate
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
static int _mempart_proc_associate(PROCESS *prp, mempart_id_t mempart_id,
									mempart_dcmd_flags_t flags, ext_lockfncs_t *lf)
{
	int r;

	if ((r = proxy_mempart_fnctbl.associate(prp, mempart_id, flags, &ext_lock_fncs)) == EOK)
		MEMPARTMGR_EVENT(MEMPART_ID_TO_T(mempart_id), mempart_evttype_t_PROC_ASSOCIATE, prp->pid, NULL);

	return r;
}

/*******************************************************************************
 * _mempart_proc_disassociate
 * 
 * Remove the process <prp> from the process list for partition <mpart>.
 * If <mpart> is NULL, the process is disassociate with all partitions in
 * its partition list.
 * If mpart is not NULL, then it is also removed from the process list for <prp>
 * _mempart_proc_disassociate_1() actually does the work.
 * 
 * Implementation Note:
 * Why do I not just call the proxy_mempart_fnctbl.disassociate() function?
 * Without memory partitioning installed no events are sent. Therefore the proxy
 * functions don't send events. In the case of disassociating from all associated
 * partitions, it is more correct to send the disassociation event after each
 * disassociation because if we send NULL to proxy_mempart_fnctbl.disassociate()
 * (which we could in order to disassociate all partitions) and it failed we would
 * have no idea which ones were not disassociated and things would get complicated.
 * This is the reason that the list is processed in this function. This then leads
 * to the reason that a write lock is obtained on the processes partition list
 * even though we are reading from the list. Because the
 * proxy_mempart_fnctbl.disassociate() requires a write lock anyway, by doing it
 * outside the call we can pass NULL for the 'ext_lockfncs_t'.
*/
static int _mempart_proc_disassociate(PROCESS *prp, mempart_id_t mempart_id, ext_lockfncs_t *lf)
{
	mempart_t  *mpart = MEMPART_ID_TO_T(mempart_id);
	int  r;

	KASSERT(prp != NULL);

	/* allow the caller to do a cleanup even if no partitions were associated */
	if ((mpart == NULL) && (LIST_FIRST(prp->mpart_list) == NULL))
		return EOK;

	/* note that mpart == NULL is only used internally when a process is destroyed */
	if (mpart != NULL)
	{
		if ((r = proxy_mempart_fnctbl.disassociate(prp, mempart_id, &ext_lock_fncs)) == EOK)
			MEMPARTMGR_EVENT(mpart, mempart_evttype_t_PROC_DISASSOCIATE, prp->pid, NULL);
		return r;
	}
	else
	{
		int  ret = EOK;
		/*
		 * remove the process from all of its associated partition process lists.
		 * Note that we always process the entire list even if a single
		 * disassociation fails. The last failure error will be returned
		*/
		MUTEX_WRLOCK(&prp->mpartlist_lock);
		while (LIST_FIRST(prp->mpart_list) != NULL)
		{
			mpart = ((mempart_node_t *)LIST_FIRST(prp->mpart_list))->mempart;
			if ((r = proxy_mempart_fnctbl.disassociate(prp, mempart_id, NULL)) == EOK) {
				MEMPARTMGR_EVENT(mpart, mempart_evttype_t_PROC_DISASSOCIATE, prp->pid, NULL);
			}
			ret = ((r != EOK) ? r : ret);
		}
		MUTEX_WRUNLOCK(&prp->mpartlist_lock);
		return ret;
	}
}

/*******************************************************************************
 * _mempart_obj_associate 
*/
static int _mempart_obj_associate(OBJECT *obj, mempart_id_t mpid)
{
	mempart_t *mpart = MEMPART_ID_TO_T(mpid);

	KASSERT(mpart != NULL);
	KASSERT(obj != NULL);

	if (obj->hdr.mpid == NULL)
	{
#ifdef USE_PROC_OBJ_LISTS
		obj_node_t  *obj_node;

		/* add 'obj' to the object list for the partition */
		if ((obj_node = calloc(1, sizeof(*obj_node))) == NULL)
		{
			/* obj->hdr.mpid == NULL; already NULL */
			obj->hdr.mpart_disassociate = NULL;
#ifndef NDEBUG
			if (ker_verbose) kprintf("%d: obj_node alloc failed\n", __LINE__);
#endif	/* NDEBUG */
			return ENOMEM;
		}
#endif	/* USE_PROC_OBJ_LISTS */

		obj->hdr.mpid = mpid;
		obj->hdr.mpart_disassociate = _mempart_obj_disassociate;
		
#ifdef USE_PROC_OBJ_LISTS
		obj_node->obj = obj;

		MUTEX_WRLOCK(&mpart->objlist_lock);
		LIST_ADD(mpart->obj_list, obj_node)
		MUTEX_WRUNLOCK(&mpart->objlist_lock);
#endif	/* USE_PROC_OBJ_LISTS */
	}
#ifndef NDEBUG
	/* if its not NULL, it better be the same as what's being passed in */
	else {KASSERT(obj->hdr.mpid == mpid);}
	
	MUTEX_RDLOCK(&mpart->objlist_lock);
	if (obj_list_audit(mpart->obj_list) != EOK) crash();
	MUTEX_RDUNLOCK(&mpart->objlist_lock);
#endif	/* NDEBUG */

	/* send event notifications to any interested parties */
	MEMPARTMGR_EVENT(MEMPART_ID_TO_T(obj->hdr.mpid), mempart_evttype_t_OBJ_ASSOCIATE, obj, NULL);
	return EOK;
}

/*******************************************************************************
 * _mempart_obj_disassociate
 * 
 * remove <obp> from the object list in the partition in which it is associated
*/
static void _mempart_obj_disassociate(OBJECT *obj)
{
	mempart_t *mp;

	KASSERT(obj != NULL);
	KASSERT(obj->hdr.mpid != mempart_id_t_INVALID);
	if ((obj->hdr.type == OBJECT_MEM_ANON) ||
		(obj->hdr.type == OBJECT_MEM_SHARED) ||
		(obj->hdr.type == OBJECT_MEM_FD) ||
		(obj->hdr.type == OBJECT_MEM_TYPED)) {
		KASSERT(obj->mem.mm.refs == NULL);
	}

	mp = MEMPART_ID_TO_T(obj->hdr.mpid);	// save for event notification;

#ifdef USE_PROC_OBJ_LISTS
	MUTEX_WRLOCK(&mp->objlist_lock);
	{
		obj_node_t *p;
		obj_node_t *n;
		/* remove the object from the partition object list */
		p = (obj_node_t *)LIST_FIRST(mp->obj_list);
		n = p;
		while ((n != NULL) && (n->obj != obj))
		{
			p = n;
			n = (obj_node_t *)LIST_NEXT(n);
		}
		if (n != NULL)
		{
			LIST_DEL(mp->obj_list, n);
			MUTEX_WRUNLOCK(&mp->objlist_lock);
			free(n);
		}
		else
		{
			/* obj not found */
			MUTEX_WRUNLOCK(&mp->objlist_lock);
#ifndef NDEBUG
			/* FIX ME - what to do if not found in a non debug load ? */
			crash();
#endif	/* NDEBUG */
		}
	}
#endif	/* USE_PROC_OBJ_LISTS */

	/* invalidate the memory partitioning related fields */
	obj->hdr.mpid = mempart_id_t_INVALID;
	obj->hdr.mpart_disassociate = NULL;		// so we don't get called again

#ifndef NDEBUG
	MUTEX_RDLOCK(&mp->objlist_lock);
	if (obj_list_audit(mp->obj_list) != EOK) crash();
	MUTEX_RDUNLOCK(&mp->objlist_lock);
#endif	/* NDEBUG */

	/* send event notifications to any interested parties */
	MEMPARTMGR_EVENT(mp, mempart_evttype_t_OBJ_DISASSOCIATE, obj, NULL);
}

/*******************************************************************************
 * _mempart_chk_and_incr
 * 
 * Atomically check for enough free space for <incr> in partition <mp> and
 * account it if there is. If there is, the amount of the allocation that should
 * be taken from previously reserved memory will be returned in <resv>.
 * 
 * Return: EOK on success, errno otherwise
 * No accounting takes place unless EOK is returned.
*/
static int _mempart_chk_and_incr(mempart_id_t mpid, memsize_t incr, memsize_t *resv_size)
{
	mempart_t *mp = MEMPART_ID_TO_T(mpid);
	int r;

	KASSERT(mp != NULL);
	MUTEX_WRLOCK(&mp->info_lock);

	if (mempart_size_chk(mp, incr) == bool_t_TRUE)
	{
		*resv_size = reserve_size(&mp->info, incr);	// expects that incr happens after
		mempart_size_incr(mp, incr);
		r = EOK;
	}
	else
		r = ENOMEM;

	MUTEX_WRUNLOCK(&mp->info_lock);

	return r;
}

/*******************************************************************************
 * _mempart_incr
 * 
 * Atomically account for <incr> in partition <mp>.
 * 
 * Note that this routine is really only used when a kernel object is returned
 * from a user process.
 * 
 * Return: EOK or an errno
*/
static int _mempart_incr(mempart_id_t mpid, memsize_t incr)
{
	mempart_t *mp = MEMPART_ID_TO_T(mpid);

	KASSERT(mp != NULL);

	MUTEX_WRLOCK(&mp->info_lock);
	mempart_size_incr(mp, incr);
	MUTEX_WRUNLOCK(&mp->info_lock);

	return EOK;
}

/*******************************************************************************
 * _mempart_decr
 * 
 * Atomically account for <decr> in partition <mp> and return the anount of the
 * deallocation that should be given back to previously reserved memory.
 * 
 * Return: from 0 to <decr> bytes that should be returned to reserved memory by
 * 			the deallocator
*/
static memsize_t _mempart_decr(mempart_id_t mpid, memsize_t decr)
{
	mempart_t *mp = MEMPART_ID_TO_T(mpid);
	memsize_t  resv;

	KASSERT(mp != NULL);

	if (decr == 0) {
		return 0;
	}

	MUTEX_WRLOCK(&mp->info_lock);

	KASSERT(decr <= mp->info.cur_size);

	resv = unreserve_size(&mp->info, decr);	// current implementation expects that decr happens 2nd
	mempart_size_decr(mp, decr);

	MUTEX_WRUNLOCK(&mp->info_lock);

	return resv;
}

/*******************************************************************************
 * _mempart_undo_incr
 * 
 * Atomically undo the increment previously done by _mempart_chk_and_incr()
 * after the subsequent allocation was attempted and failed.
 * 
 * This routine should ONLY BE CALLED after MEMPART_CHK_and_INCR() returns EOK
 * AND the subsequent allocation (recorded by MEMPART_CHK_and_INCR()) fails.
 * 
 * Return: nothing (void)
*/
static void _mempart_undo_incr(mempart_id_t mpid, memsize_t incr, memsize_t resv_size)
{
	mempart_t *mp = MEMPART_ID_TO_T(mpid);
	
	KASSERT(mp != NULL);

	if (incr == 0) {
		return;
	}

	MUTEX_WRLOCK(&mp->info_lock);

	mempart_size_decr(mp, incr);
	if (resv_size)
	{
		MUTEX_WRLOCK(&mp->memclass->lock);
		(void)mp->memclass->data.allocator.unreserve(resv_size, &mp->memclass->data.info);
		MUTEX_WRUNLOCK(&mp->memclass->lock);
	}

	MUTEX_WRUNLOCK(&mp->info_lock);
}

/*******************************************************************************
 * _mempart_get
 * 
 * Return the mempart_node_t * associated with the process <prp> corresponding to
 * <memclass_id>.
 * 
 * Returns: a pointer to the mempart_node_t or NULL is no partition of that memory
 * class is associated with the process
*/
static mempart_node_t *_mempart_get(PROCESS *prp, memclass_id_t memclass_id)
{
	mempart_node_t *m;

	KASSERT(prp != NULL);
	KASSERT(memclass_id != memclass_id_t_INVALID);
	
	MUTEX_RDLOCK(&prp->mpartlist_lock);
	m = (mempart_node_t *)LIST_FIRST(prp->mpart_list);
	/* search through the list of partitions for this process for <memclass_id> */
	while(m != NULL)
	{
		KASSERT(m->mempart != NULL);
		KASSERT(m->mempart->memclass != NULL);

		if (m->mempart != NULL) {
			if (m->mempart->memclass != NULL) {
				if (m->mempart->memclass->data.info.id == memclass_id) {
					break;
				}
			}
		}
		m = (mempart_node_t *)LIST_NEXT(m);
	}
	MUTEX_RDUNLOCK(&prp->mpartlist_lock);
		
	return m;
}

/*******************************************************************************
 * _mempart_getlist
 * 
 * Retrieve the list of partitions associated with process <prp>.
 * This function will fill in the provided <mpart_list> for the process <prp>
 * filtering the results based on <flags> and optionally <cred>.
 * The number of entries that can be filled in is specified by <n>.
 * 
 *  The flags which can be specified are
 * 		mempart_flags_t_GETLIST_ALL			- no filtering, return all associated partitions
 * 		mempart_flags_t_GETLIST_INHERITABLE	- filter out those partitions which have the
 * 											  mempart_flags_NO_INHERIT flag set
 * 		mempart_flags_t_GETLIST_CREDENTIALS	- filter out those partitions which do not have
 * 											  appropriate credentials
 * 
 * If mempart_flags_t_GETLIST_CREDENTIALS is set, <cred> should point to an appropriately
 * initialized 'struct _cred_info'. Only partitions which have the appropriate S_IXUSR or
 * S_IXGRP bits set will be returned. S_IXOTH will always be included unless other filters
 * take effect
 * 
 * Returns: -1 on any error. If successful, the number of remaining associations
 * 			which would not fit in <mpart_list> is returned. A return value of 0
 * 			indicates that all of the associations "requested" are contained in
 * 			<mpart_list>. "Requested" associations means that
 * 			'<mpart_list>->num_entries' may be less than <n> because of filter
 * 			flags <f>.
*/
static int _mempart_getlist(PROCESS *prp, mempart_list_t *mpart_list, int n,
							mempart_flags_t flags, struct _cred_info *cred)
{
	mempart_node_t *mp_node;
	unsigned  n_mparts;
	unsigned int  filtered = 0;

	KASSERT((n >= 0) && (n < 256));

	mpart_list->num_entries = 0;
	MUTEX_RDLOCK(&prp->mpartlist_lock);
	n_mparts = LIST_COUNT(prp->mpart_list);
	;
	for (mp_node = (mempart_node_t *)LIST_FIRST(prp->mpart_list);
		 mp_node != NULL; mp_node = (mempart_node_t *)LIST_NEXT(mp_node))
	{
		if (mpart_list->num_entries >= n) break;

		/* filter out entries based on inheritablility ? */
		if ((flags & mempart_flags_t_GETLIST_INHERITABLE) &&
			(mp_node->flags & mempart_flags_NO_INHERIT))
		{
			++filtered;
			continue;
		}
		/* filter out entries based on credentials ? */
		if ((flags & mempart_flags_t_GETLIST_CREDENTIALS) && (cred != NULL) &&
			(mempart_validate_association(mp_node->mempart, cred) != EOK))
		{
			++filtered;
			continue;
		}

		/* otherwise return this entry */
		mpart_list->i[mpart_list->num_entries].t.id = MEMPART_T_TO_ID(mp_node->mempart);
		mpart_list->i[mpart_list->num_entries++].flags = mp_node->flags;
	}
	MUTEX_RDUNLOCK(&prp->mpartlist_lock);
	
	return (n_mparts - (mpart_list->num_entries + filtered));
}

/*******************************************************************************
 * _mempart_validate_association
 * 
 * This API is used to determine whether or not the partition identified by
 * 'mempart_id_t' <mpid> can be associated with based on the 'struct _cred_info' <c>.
 * 
 * Returns: EOK if association is permitted, otherwise and errno.
 * 
 * If the resource manager module is not installed, EOK will be returned since
 * associations control is meaningless
 *
*/
static int mempart_validate_association(mempart_t *mpart, struct _cred_info *cred)
{
	int r = EOK;

	KASSERT(mpart != NULL);
	KASSERT(cred != NULL);

	if ((rsrcmgr_mempart_fnctbl != NULL) && (rsrcmgr_mempart_fnctbl->validate_association != NULL)) {
		r = rsrcmgr_mempart_fnctbl->validate_association(mpart->rmgr_attr_p, cred);
	}
	return r;
}
static int _mempart_validate_association(mempart_id_t mpid, struct _cred_info *cred)
{
	return mempart_validate_association(MEMPART_ID_TO_T(mpid), cred);
}

/*******************************************************************************
 * _rsrcmgr_attach
 *
*/
mempart_rsrcmgr_fnctbl_t *_rsrcmgr_attach(rsrcmgr_mempart_fnctbl_t *ifTbl)
{
#ifndef NDEBUG
	if (ker_verbose) kprintf("Attaching memory partitioning resource manager\n"); 
#endif
	if (ifTbl != NULL)
	{
		rsrcmgr_mempart_fnctbl = ifTbl;
		memclass_event = rsrcmgr_mempart_fnctbl->mclass_event_handler;
	}
	return &_mempart_rsrcmgr_fnctbl;
}


/*
 * ===========================================================================
 * 
 * 				Memory Partition resource manager interface routines
 *  
 * The following routines are made available to the memory partition resource
 * manager via the 'mempart_rsrcfnctbl' which is initialized when mempart_init()
 * is called.
 * 
 * These routines allow the resource manager to create and destroy memory
 * partitions, add or remove memory classes to or from an existing partition
 * and retrieve information about a partition
 * 
 * _mempart_create 		- create a new partition optionally specifying the
 * 						  attributes and polcies
 * _mempart_destroy		- destroy an existing partition
 * _mempart_config		- modify the attributes and/or polices of the specified
 * 						  partition
 * _mempart_getinfo		- retrieve a attributes/policies and runtime metrics
 * 						  for the specified partition
 * _validate_cfg_creation - validate a new partition configuration
 * _validate_cfg_modification - validate proposed changes to an existing partition
 * _resmgr_attach		- allows the resource manager for memory partitioning
 * 						  to retrieve the interface functions necessary to
 * 						  interact with the memory partitioning module
 *  
 * ===========================================================================
*/

/*******************************************************************************
 * _mempart_create
 * 
 * This function will (internally) create a new memory partition as a child of
 * the parent. The parent/child relationship exists primarily for the purpose of
 * establishing constraints on the newly created partition. In this regard, the
 * specified child configuration will be validated against the parent to
 * determine whether or not the creation will be permitted.
 * 
 * Note that at this point permissions level checking has been provided by the
 * resource manager, so what is beng done here it to validate any optional
 * attributes and policies. If no attributes or policies are provided
 * (ie <child_cfg> == NULL), defaults will be used that will effectively disable
 * the use of the partition (ie min = max = 0) although processes may still
 * associate with the partition.
 * Attributes can be modified at a later time with _mempart_config().
 *  
 * If successful, a partition identifier is returned that can be used to
 * reference the partition otherwise NULL is returned.
 * 
 * LOCKING note
 * When a new partition is created is not usable in any way by the system
 * prior to return therefore no locks are requried to be held. Because this
 * function calls _mempart_config() to perform a configuration change (if
 * <child_cfg> != NULL), which does acquire the mempart->info_lock, this lock
 * needs to be initialized prior to calling that function
 * 
 * IMPORTANT
 * The memory for the partition structures always comes from the internal heap
 * since it may need to live beyond the existence of the creating process.
 * Note that this represents a security hole since an appropriately privileged
 * process could exhaust system memory by continuously creating partitions 
*/
static mempart_id_t _mempart_create(mempart_id_t parent_mpid, mempart_cfg_t *child_cfg, memclass_id_t memclass)
{
	mempart_t  *mempart;
	mempart_t  *parent = MEMPART_ID_TO_T(parent_mpid);
	int  r;

	/* validate the requested configuration */
	if ((r = _validate_cfg_creation(parent_mpid, child_cfg, memclass)) != EOK)
	{
#ifndef NDEBUG
		if (ker_verbose > 1) kprintf("cfg create validation err %d\n", r);
#endif	/* NDEBUG */
		return mempart_id_t_INVALID;
	}
	else if ((mempart = malloc(sizeof(*mempart))) == NULL)
	{
		return mempart_id_t_INVALID;
	}
	else
	{
		mempart_id_t mpid = MEMPART_T_TO_ID(mempart);

		/*
		 * initialize the state of the new partition. Use default policies and
		 * min = max = 0 attributes so that the partition can be associated
		 * with but is otherwise not usable unless valid configuration data has
		 * been provided to override the default
		*/
		memset(mempart, 0, sizeof(*mempart));
		r = MUTEX_INIT(&mempart->info_lock, &info_lock_attr);
#ifndef NDEBUG
		if (r != EOK) {kprintf("lock init failure, partition %p, err %d\n", mempart, r); crash();}
#endif	/* NDEBUG */

#ifdef USE_PROC_OBJ_LISTS
		r = MUTEX_INIT(&mempart->prplist_lock, NULL);
	#ifndef NDEBUG
		if (r != EOK) {kprintf("lock init failure, partition %p, err %d\n", mempart, r); crash();}
	#endif	/* NDEBUG */
		r = MUTEX_INIT(&mempart->objlist_lock, NULL);
	#ifndef NDEBUG
		if (r != EOK) {kprintf("lock init failure, partition %p, err %d\n", mempart, r); crash();}
	#endif	/* NDEBUG */
#endif	/* USE_PROC_OBJ_LISTS */

		mempart->info.cre_cfg.policy = mempart->info.cur_cfg.policy = MEMPART_DFLT_POLICY;
		/*
		 * Generate the policy keys based on the currently selected policies. This
		 * must be done before calling _mempart_config() 
		*/
		SET_MPART_POLICY_TERMINAL_KEY(&mempart->info.cre_cfg);
		SET_MPART_POLICY_CFG_LOCK_KEY(&mempart->info.cre_cfg);
		SET_MPART_POLICY_PERMANENT_KEY(&mempart->info.cre_cfg);
		SET_MPART_POLICY_TERMINAL_KEY(&mempart->info.cur_cfg);
		SET_MPART_POLICY_CFG_LOCK_KEY(&mempart->info.cur_cfg);
		SET_MPART_POLICY_PERMANENT_KEY(&mempart->info.cur_cfg);

		mempart->signature = MEMPART_SIGNATURE;
		mempart->parent = parent;
		mempart->memclass = memclass_find(NULL, memclass);

		KASSERT(mempart->memclass != NULL);

		/* if partitioning data has been supplied, use it (it has been validated) */
		if (child_cfg != NULL)
		{
			/* use _mempart_config() to set the current configuration ... */
			if ((r = _mempart_config(mpid, child_cfg)) != EOK)
			{
#ifndef NDEBUG
				/*
				 * crash the debug load since this should not happen.
				 * _validate_cfg_creation() should have verified the config
				*/
				kprintf("cfg validated but could not be set, errno %d\n", r);
				crash();
#endif	/* NDEBUG */
				free(mempart);
				return mempart_id_t_INVALID;
			}
			/* ... and since this is creation time, set the creation config as well */
			mempart->info.cre_cfg = mempart->info.cur_cfg;
			mempart->info.cre_cfg.policy.config_lock = GET_MPART_POLICY_CFG_LOCK(&mempart->info.cur_cfg);
			mempart->info.cre_cfg.policy.terminal = GET_MPART_POLICY_TERMINAL(&mempart->info.cur_cfg);
			mempart->info.cre_cfg.policy.permanent = GET_MPART_POLICY_PERMANENT(&mempart->info.cur_cfg);
			SET_MPART_POLICY_TERMINAL_KEY(&mempart->info.cre_cfg);
			SET_MPART_POLICY_CFG_LOCK_KEY(&mempart->info.cre_cfg);
			SET_MPART_POLICY_PERMANENT_KEY(&mempart->info.cre_cfg);
		}
		/* send event notifications to any interested parties */
		MEMPARTMGR_EVENT(parent, mempart_evttype_t_PARTITION_CREATE, MEMPART_T_TO_ID(mempart), NULL);
		{
			MEMCLASSMGR_EVENT(&mempart->memclass->data, memclass_evttype_t_PARTITION_CREATE,
								(memclass_sizeinfo_t *)&mpid, NULL);
		}

		return mpid;
	}
}

/*******************************************************************************
 * _mempart_destroy
 * 
 * This function will (internally) destroy the memory partition <mp> and return
 * any reserved memory back to the parent.
 * The memory allocated for <mp> will also be released.
 * 
 * IMPORTANT:
 * 
 * It is assumed that all previous associations (by objects or processes) with
 * the partition have been removed. An assert for this condition will be made.
 * Specifically, only the 'mempart_t' structure is released.
 * Process references to <mp> made through the list of 'mempart_node_t' structures
 * are not handled in this function but should have been cleaned up when the
 * processes were disassociated with the partition.
 * 
 * Policies regarding the removal of a partition (ie. are all associated
 * processes terminated/objects freed unvoluntarily or do associations prevent
 * removal) are handled in the resource manager portion of the code.
 * 
 * Returns: N/A
 * 
 * IMPORTANT
 * The memory for the partition structures always comes from the internal sysram
 * heap since it may need to live beyond the existence of the creating process. It
 * therefore gets released back to that heap as well.
*/
static int _mempart_destroy(mempart_id_t mpid)
{
	mempart_t  *mp = MEMPART_ID_TO_T(mpid);
	mempart_t  *parent = mp->parent;
	memclass_t  *mclass;

	if (mp->info.cur_size != 0)
		return EBUSY;

#ifdef USE_PROC_OBJ_LISTS
	KASSERT(LIST_FIRST(mp->prp_list) == NULL);
	KASSERT(LIST_FIRST(mp->obj_list) == NULL);
#endif	/* USE_PROC_OBJ_LISTS */

	MUTEX_WRLOCK(&mp->info_lock);

	if (mp->info.cur_cfg.attr.size.min) {
		mempart_unreserve(mp->parent, mp->info.cur_cfg.attr.size.min, 0, mp->memclass->data.info.id);
	}
	
	MUTEX_DESTROY(&mp->info_lock);
#ifdef USE_PROC_OBJ_LISTS
	MUTEX_DESTROY(&mp->prplist_lock);
	MUTEX_DESTROY(&mp->objlist_lock);
#endif	/* USE_PROC_OBJ_LISTS */

	mclass = &mp->memclass->data;	// grab this before the partition is destroyed
	free(mp);
	
	/* send event notifications to any interested parties */
	MEMPARTMGR_EVENT(parent, mempart_evttype_t_PARTITION_DESTROY, mpid, NULL);
	MEMCLASSMGR_EVENT(mclass, memclass_evttype_t_PARTITION_DESTROY,
						(memclass_sizeinfo_t *)&mpid, NULL);
	
	return EOK;
}

/*******************************************************************************
 * _mempart_config
 *
 * Modify the attributes and/or policies of the memory partition <mpart>
 * 
 * This function will modify the current partition attributes and polcies
 * as per <cfg>. The requested change will be validated against the current
 * partition settings and usage as well as any parent partition restrictions.
 * 
 * Most of the validation should have taken place prior to calling this function
 * 
 * Returns: EOK on success otherwise errno
*/
static int _mempart_config(mempart_id_t mpid, mempart_cfg_t *cfg)
{
	int  r;
	mempart_cfg_t  prev_cfg;
	mempart_t  *mpart = MEMPART_ID_TO_T(mpid);

	KASSERT(mpart != NULL);
	KASSERT(cfg != NULL);

	MUTEX_WRLOCK(&mpart->info_lock);

	if ((r = _validate_cfg_modification(mpid, cfg)) != EOK)
	{
		MUTEX_WRUNLOCK(&mpart->info_lock);
		return r;
	}

/* FIX ME - re Peter V change
 * Peter would like reservations to come from discretionary first and then from
 * the partition hierarchy. I am not sure I agree (I believe they should always
 * come from the partition hierarchy when the allocation policy is hierarchical
 * - this is what's coded) however the change would be to call mempart_reserve()
 * with a NULL parent, see how much was obtained and then call mempart_reserve()
 * again, with the remainder to be allocated and the new partitions' parent.
 * The problem I see with this is that the amount of memory accounted aginst a
 * parents reserved is dependent on the available discretionary instead of
 * always being accounted against the parent's reserved. If they don't have any
 * reserved, the allocation is still accounted to them even though all of the
 * new reservation would neccessarily come from discretionary in that case.
 * Resolution is TBD although I think I will elect to handle it conditionally
*/

// FIX ME - need to go straight to allocator for non hierarchical partitions
	/*
	 * In order to get the base configuration modification working, the following
	 * rules will be enforced. There must be no existing allocations in the
	 * partition hierarchy. This does not mean that the cur_size of a partition
	 * is 0, only that the cur_size of a partition MUST be equal to the reservation
	 * size of all of the children. The partition being modified however must have
	 * a cur_size of 0 if it has no children.
	*/

	/*
	 * determine if there are any changes to the amount of reserved memory in
	 * the hierarchy
	*/
	if (cfg->attr.size.min > mpart->info.cur_cfg.attr.size.min)
	{
		/* minimum is being increased */
		memsize_t resv_size = cfg->attr.size.min - mpart->info.cur_cfg.attr.size.min;
		memsize_t resv_used_adjust = 0;

		/* calculate any adjustment to the reserved memory in use */
		if (mpart->info.cur_size > mpart->info.cur_cfg.attr.size.min)
		{
			if (cfg->attr.size.min >= mpart->info.cur_size)
			{
				resv_used_adjust = mpart->info.cur_size - mpart->info.cur_cfg.attr.size.min;
			}
			else
			{
				resv_used_adjust = cfg->attr.size.min - mpart->info.cur_cfg.attr.size.min;
			}
		}

/*
 * There is a boundary case bug (??) whereby a fully consumed partition cannot be changed
 * from having all of its allocation re-established as reserved memory. For example
 * if I have a partition of min=0, max=0x1000000 and cur_size == max == 0x1000000
 * I cannot modify the partition such that all of it's allocation is reserved if there
 * is not enough unreserved in the memory class to do the reservation first before the
 * resv_adjust().
*/
		if ((resv_size > 0) &&
			(mempart_reserve(mpart->parent, resv_size, cfg->attr.size.min, mpart->memclass->data.info.id) != EOK))
		{
			MUTEX_WRUNLOCK(&mpart->info_lock);
			return ENOMEM;
		}
		if (resv_used_adjust > 0) {
			MEMCLASS_RUSED_ADJ_UP(mpart, resv_used_adjust);
		}
	}
	else if (cfg->attr.size.min < mpart->info.cur_cfg.attr.size.min)
	{
		/* minimum is being decreased */
		memsize_t unresv_size = mpart->info.cur_cfg.attr.size.min - cfg->attr.size.min;
		memsize_t unresv_used_adjust = 0;

		/* calculate any adjustment to the reserved memory in use */
		if (mpart->info.cur_size > cfg->attr.size.min)
		{
			if (mpart->info.cur_size >= mpart->info.cur_cfg.attr.size.min)
				unresv_used_adjust = mpart->info.cur_cfg.attr.size.min - cfg->attr.size.min;
			else
				unresv_used_adjust = mpart->info.cur_size - cfg->attr.size.min;
		}

		if (unresv_size > 0) {
			mempart_unreserve(mpart->parent, unresv_size, cfg->attr.size.min, mpart->memclass->data.info.id);
		}
		
		if (unresv_used_adjust > 0) {
			MEMCLASS_RUSED_ADJ_DN(mpart, unresv_used_adjust);
		}
	}
	
	/* save a copy of the previous configuration */	
	prev_cfg = mpart->info.cur_cfg;
	prev_cfg.policy.terminal = GET_MPART_POLICY_TERMINAL(&mpart->info.cur_cfg);
	prev_cfg.policy.config_lock = GET_MPART_POLICY_CFG_LOCK(&mpart->info.cur_cfg);
	prev_cfg.policy.permanent = GET_MPART_POLICY_PERMANENT(&mpart->info.cur_cfg);

	/* complete the modification changes regenerate the keys based on the new settings */
	mpart->info.cur_cfg = *cfg;
	SET_MPART_POLICY_TERMINAL_KEY(&mpart->info.cur_cfg);
	SET_MPART_POLICY_CFG_LOCK_KEY(&mpart->info.cur_cfg);
	SET_MPART_POLICY_PERMANENT_KEY(&mpart->info.cur_cfg);

	MUTEX_WRUNLOCK(&mpart->info_lock);

	/* send event appropriate notifications to any interested parties */
	if (cfg->attr.size.min != prev_cfg.attr.size.min) {
		MEMPARTMGR_EVENT(mpart, mempart_evttype_t_CONFIG_CHG_ATTR_MIN, &prev_cfg, cfg);
	}
	if (cfg->attr.size.max != prev_cfg.attr.size.max) {
		MEMPARTMGR_EVENT(mpart, mempart_evttype_t_CONFIG_CHG_ATTR_MAX, &prev_cfg, cfg);
	}
	if ((cfg->policy.terminal != prev_cfg.policy.terminal) ||
		(cfg->policy.config_lock != prev_cfg.policy.config_lock) ||
		(cfg->policy.permanent != prev_cfg.policy.permanent) ||
		(cfg->policy.alloc != prev_cfg.policy.alloc)) {
		MEMPARTMGR_EVENT(mpart, mempart_evttype_t_CONFIG_CHG_POLICY, &prev_cfg, cfg);
	}
	return EOK;
}


/*******************************************************************************
 * _mempart_get_info
 * 
 * Get partition info into caller provided buffer.
 * If successful, a pointer to the mempart_info_t buffer is returned otherwise
 * NULL is returned.
 * 
 * Note that this routine will also "restore" policy keys back to bool_t values   
*/
static mempart_info_t *_mempart_getinfo(mempart_id_t mpid, mempart_info_t *info)
{
	mempart_t *mpart = MEMPART_ID_TO_T(mpid);

	KASSERT(mpart != NULL);
	KASSERT(info != NULL);

	MUTEX_RDLOCK(&mpart->info_lock);
	memcpy(info, &mpart->info, sizeof(*info));
	MUTEX_RDUNLOCK(&mpart->info_lock);

	/* get the external (bool_t) representation of the policy settings */
	info->cre_cfg.policy.terminal = GET_MPART_POLICY_TERMINAL(&mpart->info.cre_cfg);
	info->cre_cfg.policy.config_lock = GET_MPART_POLICY_CFG_LOCK(&mpart->info.cre_cfg);
	info->cre_cfg.policy.permanent = GET_MPART_POLICY_PERMANENT(&mpart->info.cre_cfg);
	info->cur_cfg.policy.terminal = GET_MPART_POLICY_TERMINAL(&mpart->info.cur_cfg);
	info->cur_cfg.policy.config_lock = GET_MPART_POLICY_CFG_LOCK(&mpart->info.cur_cfg);
	info->cur_cfg.policy.permanent = GET_MPART_POLICY_PERMANENT(&mpart->info.cur_cfg);
	return(info);
}

/*******************************************************************************
 * _mempart_find_pid
 *
 * determine if the process id <pid> is associated with the memory partition
 * identified by <mp> 
 * 
 * Returns: a PROCESS pointer if found, NULL otherwise
*/
static PROCESS *_mempart_find_pid(pid_t pid, mempart_id_t mpid)
{
	mempart_t *mempart = MEMPART_ID_TO_T(mpid);
	prp_node_t *prp_node;
	PROCESS *prp;

	KASSERT(mempart != NULL);

	if ((prp = proc_lookup_pid(pid)) != NULL)
	{
		MUTEX_RDLOCK(&mempart->prplist_lock);
		prp_node = (prp_node_t *)LIST_FIRST(mempart->prp_list);

		while (prp_node != NULL)
		{
			if (prp_node->prp == prp) {
				break;
			}
			prp_node = (prp_node_t *)LIST_NEXT(prp_node);
		}
		MUTEX_RDUNLOCK(&mempart->prplist_lock);
		if (prp_node == NULL) prp = NULL;
	}
	return prp;
}

/*******************************************************************************
 * _validate_cfg_creation
 * _validate_cfg_modification
 * 
 * These 2 routines are used to validate configurations for partition creation
 * or modification based on the policies in use. They provide common routines
 * for validation (via validate_config()) so that these checks are not littered
 * throughout the code.
 * 
 * They are mainly called from the memory partition resource manager in order
 * to trap illegal requests from applications, however they are also called
 * internally from functions in this file for debug loads in order to ensure
 * that the resource manager is doing its job 
 * 
 * FIX ME - currently no locking here. Could lead to a bad decision re validation
*/
static int _validate_cfg_creation(mempart_id_t parent_mpid, mempart_cfg_t *cfg, memclass_id_t memclass_id)
{
	mempart_t *parent_mp = MEMPART_ID_TO_T(parent_mpid);
	memclass_entry_t  *memclass = memclass_find(NULL, memclass_id);

#ifdef _MEMSIZE_and_memsize_are_different_
	register const memsize_t  max_size = cfg->attr.size.max;
	register const memsize_t  min_size = cfg->attr.size.min;
#else	/* _MEMSIZE_and_memsize_are_different_ */
#define max_size	cfg->attr.size.max
#define min_size	cfg->attr.size.min
#endif	/* _MEMSIZE_and_memsize_are_different_ */

	/* assert that the memory class exists in the system */
	if (memclass == NULL) {
		return ENOMEM;
	}

	if (cfg != NULL)
	{
		/* assert config attribute min falls within allocator limits */
		if (min_size & (memclass->data.info.attr.limits.alloc.size.min - 1)) {
			return EINVAL;
		}

		/* assert config attribute max falls within allocator limits */
		if (max_size != memsize_t_INFINITY) {
			if (max_size & (memclass->data.info.attr.limits.alloc.size.min - 1)) {
				return EINVAL;
			}
		}

		/* assert that a valid allocation policy has been provided */
		if ((cfg->policy.alloc < mempart_alloc_policy_t_first) ||
			(cfg->policy.alloc > mempart_alloc_policy_t_last)) {
			return EINVAL;
		}

		/* assert config attributes min <= max */
		if (min_size > max_size) {
			return EINVAL;
		}
	}
	
	if (parent_mp != NULL)
	{
		/* assert that the specified memory class is same as the parent */
		if (parent_mp->memclass->data.info.id != memclass->data.info.id) {
			return EINVAL;
		}
		
		/* assert that the parent partition may have children */
		if (GET_MPART_POLICY_TERMINAL(&parent_mp->info.cur_cfg) == bool_t_TRUE) {
			return EPERM; 
		}
	
		/* the following are checks validate the config against the parent partition */
		if (cfg != NULL)
		{
#ifdef _MEMSIZE_and_memsize_are_different_
			register const memsize_t  parent_max_size = parent_mp->info.cur_cfg.attr.size.max;
#else	/* _MEMSIZE_and_memsize_are_different_ */
			#define parent_max_size		parent_mp->info.cur_cfg.attr.size.max
#endif	/* _MEMSIZE_and_memsize_are_different_ */

			/* assert the specified allocation policy is the same as the parent */ 
			if (cfg->policy.alloc != parent_mp->info.cur_cfg.policy.alloc) {
				return EINVAL;
			}

			/* for allocation policy mempart_alloc_policy_t_ROOT, assert that the new maximum is <= parent maximum */
			if (cfg->policy.alloc == mempart_alloc_policy_t_ROOT) {
				if (max_size > parent_max_size) {
					return EINVAL;
				}
			}
		}
	}
	return EOK;
#ifndef _MEMSIZE_and_memsize_are_different_
#undef max_size
#undef min_size
#undef parent_max_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */
}

static int _validate_cfg_modification(mempart_id_t mpid, mempart_cfg_t *cfg)
{
	mempart_t *mp = MEMPART_ID_TO_T(mpid);

	/*
	 * NOTE: anything to do with changes in reservations will be handled when
	 * change is actually made, not here
	*/
	
	/* assert modifying a valid partition */
	if (mp == NULL) {
		return ENOENT;
	}
	
	if (cfg == NULL) {
		return EINVAL;
	}

	/* assert that if the terminal partition policy is set, it cannot be changed */
	if (GET_MPART_POLICY_CFG_LOCK(&mp->info.cur_cfg) == bool_t_TRUE) {
		if (cfg->policy.config_lock == bool_t_FALSE) {
			return EPERM;
		}
	}

	/* assert that if the terminal partition policy is set, it cannot be changed */
	if (GET_MPART_POLICY_TERMINAL(&mp->info.cur_cfg) == bool_t_TRUE) {
		if (cfg->policy.terminal == bool_t_FALSE) {
			return EPERM;
		}
	}

	/* assert that if the permanent partition policy is set, it cannot be changed */
	if (GET_MPART_POLICY_PERMANENT(&mp->info.cur_cfg) == bool_t_TRUE) {
		if (cfg->policy.permanent == bool_t_FALSE) {
			return EPERM;
		}
	}

	/* assert that a valid allocation policy has been provided */
	if ((cfg->policy.alloc < mempart_alloc_policy_t_first) ||
		(cfg->policy.alloc > mempart_alloc_policy_t_last)) {
		return EINVAL;
	}

// FIX ME - assert that if changing the allocation policy to non hierarchical
//			that the partition does not have any children with mempart_alloc_policy_t_HIERARCHICAL

	/* assert config attribute min falls within allocator limits */
	if (cfg->attr.size.min & (mp->memclass->data.info.attr.limits.alloc.size.min - 1)) {
		return EINVAL;
	}

	/* assert config attribute max falls within allocator limits */
	if (cfg->attr.size.max != memsize_t_INFINITY) {
		if (cfg->attr.size.max & (mp->memclass->data.info.attr.limits.alloc.size.min - 1)) {
			return EINVAL;
		}
	}

	/* assert config attributes min <= max */
	if (cfg->attr.size.min > cfg->attr.size.max) {
		return EINVAL;
	}
	
	/* assert that the new maximum >= current size */
	if (cfg->attr.size.max < mp->info.cur_size) {
		return ENOMEM;
	}

	/* for allocation policy mempart_alloc_policy_t_ROOT, assert that the new maximum is <= parent maximum */
	if (cfg->policy.alloc == mempart_alloc_policy_t_ROOT) {
		if (mp->parent != NULL) {
			if (cfg->attr.size.max > mp->parent->info.cur_cfg.attr.size.max) {
				return EINVAL;
			}
		}
	}

	/*
	 * assert that if attribute or allocation policy changes are being made
	 * that they are permitted (ie. that the config lock policy is not set)
	*/
	if ((cfg->policy.alloc != mp->info.cur_cfg.policy.alloc) ||
		(cfg->attr.size.min != mp->info.cur_cfg.attr.size.min) ||
		(cfg->attr.size.max != mp->info.cur_cfg.attr.size.max)) {
		if (GET_MPART_POLICY_CFG_LOCK(&mp->info.cur_cfg) == bool_t_TRUE) {
			return EPERM;
		}
	}

	return EOK;
}

/*
 * ===========================================================================
 * 
 * 							Internal support routines
 *  
 * ===========================================================================
*/

/*******************************************************************************
 * mempart_size_chk
 * 
 * Check whether <size> bytes can be allocated in the partition hierarchy
 * starting at partition <mp>.
*/
static bool mempart_size_chk(mempart_t *mp, memsize_t size)
{
	KASSERT(mp != NULL);

	{
#ifdef _MEMSIZE_and_memsize_are_different_
	register memsize_t  max_size = mp->info.cur_cfg.attr.size.max;
//	register memsize_t  min_size = mp->info.cur_cfg.attr.size.min;
	register memsize_t  cur_size = mp->info.cur_size;
#else	/* _MEMSIZE_and_memsize_are_different_ */
#define max_size	mp->info.cur_cfg.attr.size.max
#define min_size	mp->info.cur_cfg.attr.size.min
#define cur_size	mp->info.cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */

	if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_ROOT)
	{
		return ((max_size - cur_size) >= size) ? bool_t_TRUE : bool_t_FALSE;
	}
	else if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL)
	{
		memsize_t resv_size = reserve_size(&mp->info, size);
		/* if there is not enough reserve, check the partition hierarchy */ 
		if (resv_size >= size)
		{
			return bool_t_TRUE;
		}
		else
		{
			return (hierarchy_chk(mp, max_size - cur_size) >= size) ? bool_t_TRUE : bool_t_FALSE;
		}
	}
	else	/* bad mempart_policy.alloc */
	{
#ifndef NDEBUG
		crash();
#endif	/* NDEBUG */
		return bool_t_FALSE;
	}
#ifndef _MEMSIZE_and_memsize_are_different_
#undef max_size
#undef min_size
#undef cur_size
#endif	/* _MEMSIZE_and_memsize_are_different_ */
	}	
}

	
/*******************************************************************************
 * mempart_size_incr
 * 
 * Account an allocation of <inc> bytes against the partition hierarchy starting
 * at <mp>.
*/
static void mempart_size_incr(mempart_t *mp, memsize_t inc)
{
#ifndef NDEBUG
	static mempart_t *watch_mempart = NULL;
	if (mp == watch_mempart)
		kprintf("");	// something to break on

	if (mp == NULL) crash();
	if ((mp->info.cur_cfg.attr.size.max - mp->info.cur_size) < inc)
	{
		kprintf("can't allocate %P with only %P\n",
					(paddr_t)inc, (paddr_t)(mp->info.cur_cfg.attr.size.max - mp->info.cur_size));
		crash();
	}
	if (inc && (inc & (mp->memclass->data.info.attr.limits.alloc.size.min - 1)))
	{
		kprintf("funny looking inc of %P\n", (paddr_t)inc);
		crash();
	}
#endif	/* NDEBUG */

	if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_ROOT)
	{
		memsize_t  prev_size = mp->info.cur_size;
		if ((mp->info.cur_size += inc) > mp->info.hi_size) {
			mp->info.hi_size = mp->info.cur_size;
		}

		/* send event notifications to any interested parties */
		MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_INCR, mp->info.cur_size, prev_size);
	}
	else if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL)
	{
#ifndef NDEBUG
		if (hierarchy_incr(mp, inc) != inc) crash();
	}
	else
	{
		crash();
#else	/* NDEBUG */
		(void)hierarchy_incr(mp, inc);
#endif	/* NDEBUG */
	}
}

/*******************************************************************************
 * mempart_size_decr
 * 
 * Account a deallocation of <dec> bytes against the partition hierarchy starting
 * at <mp>.
*/
static void mempart_size_decr(mempart_t *mp, memsize_t dec)
{
#ifndef NDEBUG
	if (mp == NULL) crash();
	if (dec == 0) crash();

	if (mp->info.cur_size < dec)
	{
		kprintf("can't deallocate %P, only %P allocated\n",
						(paddr_t)dec, (paddr_t)mp->info.cur_size);
		crash();
	}
#endif	/* NDEBUG */

	if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_ROOT)
	{
		memsize_t  prev_size = mp->info.cur_size;
		mp->info.cur_size -= dec;
		
		/* send event notifications to any interested parties */
		MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_DECR, mp->info.cur_size, prev_size);
	}
	else if (mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL)
	{
#ifndef NDEBUG
		if (hierarchy_decr(mp, dec) != dec)
			crash();
	}
	else
	{
		crash();
#else	/* NDEBUG */
		(void)hierarchy_decr(mp, dec);
#endif	/* NDEBUG */
	}
}


/*******************************************************************************
 * reserved_free
 * 
 * This function returns the amount of unallocated reserved memory of the class
 * identified by <meminfo>
 * <memclass_id> in partition <mp> or 0 if there is no unallocated reserved
 * memory of class <class_id>.
*/


/*******************************************************************************
 * free_size
 * 
 * This function returns the amount of free space in partition <mp>
*/
static INLINE memsize_t free_size(mempart_t *mp)
{
	return ((memsize_t)mp->info.cur_cfg.attr.size.max - (memsize_t)mp->info.cur_size);
}

/*******************************************************************************
 * max_size
 * 
 * This function returns the currently configured maximum for partition <mp>
*/
/* FIX ME - I wonder if some confusion with the #define max_size was ocurring
static INLINE memsize_t max_size(mempart_t *mp)
{
	return (mp->info.cur_cfg.attr.size.max);
}
*/

/*******************************************************************************
 * mempart_reserve
 * 
 * This function will recurse back as far as necessary in the partition hierarchy
 * (terminating at the 'root' partition) attempting to ensure that the requested
 * preallocation can be accounted for. There is no actual memory allocation that
 * takes place, but the memory for the requested guarantee of a child must be
 * satisfiable and therefore accounted for at some point in the hierarchy
 * 
 * This function will either return the amount of memory requested or 0 if the
 * reservation could not be made.
 *
*/
static int mempart_reserve(mempart_t *mp, memsize_t size, memsize_t child_min, memclass_id_t memclass_id)
{
	if (mp == NULL)
	{
		int r;
		memclass_entry_t  *entry = memclass_find(NULL, memclass_id);

		KASSERT(entry != NULL);

		/* FIX ME - this locking should be done inside the allocator for the class
		 * 			Need to pass &entry then so that entry->lock is available */
		MUTEX_WRLOCK(&entry->lock);
		r = (entry->data.allocator.reserve(size, &entry->data.info) == size) ? EOK : ENOMEM;
		MUTEX_WRUNLOCK(&entry->lock);

		return r;
	}
	else
	{
		/*
		 * FIX ME - currently the only modification scenario that will be
		 * supported is for no allocations to exist in the partition hierarchy.
		 * This means that if a cur_size exists it will represent the total
		 * reservation amounts of all of the child partitions. This is not
		 * currently checked for
		*/
		int r = EOK;
		memsize_t  prev_size = mp->info.cur_size;
		memsize_t  new_size = prev_size;
		memsize_t  resv_incr;
//		memsize_t  cur_size_incr = size;

		MUTEX_RDLOCK(&mp->info_lock);
		
		/* verify that the parent limit will not be exhausted */
		if ((mp->info.cur_cfg.attr.size.max - mp->info.cur_size) < size)
		{
			MUTEX_RDUNLOCK(&mp->info_lock);
			return ENOMEM;		// no room in partition
		}
		

		if (mp->info.cur_size >= mp->info.cur_cfg.attr.size.min)
		{
			resv_incr = size;
		}
		else if ((mp->info.cur_size + size) > mp->info.cur_cfg.attr.size.min)
		{
			resv_incr = (mp->info.cur_size + size) - mp->info.cur_cfg.attr.size.min;
		}
		else
		{
			resv_incr = 0;
		}

		if (resv_incr > 0) {
			r = mempart_reserve(mp->parent, resv_incr, mp->info.cur_cfg.attr.size.min, memclass_id);
		}

		if (r == EOK) {
			new_size = (mp->info.cur_size += size);
		}
		
		MUTEX_RDUNLOCK(&mp->info_lock);

		/* if required, send event notifications to any interested parties */
		if (r == EOK) {
			if (new_size != prev_size) {
				MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_INCR, new_size, prev_size);
			}
		}

		return r;
	}
}

/*******************************************************************************
 * mempart_unreserve
 * 
 * This function will release <size> bytes of previously reserved memory of
 * class <memclass_id>.
 * It basically undoes mempart_reserve().
*/
static void mempart_unreserve(mempart_t *mp, memsize_t size, memsize_t child_min, memclass_id_t memclass_id)
{
	if (size == 0) return;

	if (mp == NULL)
	{
		memclass_entry_t  *entry = memclass_find(NULL, memclass_id);

		KASSERT(entry != NULL);

		/* FIX ME - this locking should be done inside the allocator for the class
		 * 			Need to pass &entry then so that entry->lock is available */
		MUTEX_WRLOCK(&entry->lock);
		entry->data.allocator.unreserve(size, &entry->data.info);
		MUTEX_WRUNLOCK(&entry->lock);
	}
	else
	{
		/*
		 * FIX ME - currently the only modification scenario that will be
		 * supported is for no allocations to exist in the partition hierarchy.
		 * This means that if a cur_size exists it will represent the total
		 * reservation amounts of all of the child partitions. This is not
		 * currently checked for
		*/
		memsize_t  prev_size = mp->info.cur_size;
		memsize_t  new_size = prev_size;
		memsize_t  resv_decr;
//		memsize_t  cur_size_decr = size;

		MUTEX_RDLOCK(&mp->info_lock);
		
		if (mp->info.cur_size <= mp->info.cur_cfg.attr.size.min)
		{
			resv_decr = 0;
		}
		else if ((mp->info.cur_size - size) <= mp->info.cur_cfg.attr.size.min)
		{
			resv_decr = mp->info.cur_size - mp->info.cur_cfg.attr.size.min;
		}
		else
		{
			resv_decr = size;
		}

		if (resv_decr > 0) {
			mempart_unreserve(mp->parent, resv_decr, mp->info.cur_cfg.attr.size.min, memclass_id);
		}

		new_size = (mp->info.cur_size -= size);		

		MUTEX_RDUNLOCK(&mp->info_lock);

		/* if required, send event notifications to any interested parties */
		if (new_size != prev_size) {
			MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_DECR, new_size, prev_size);
		}
	}
}

/*******************************************************************************
 * hierarchy_chk
 * 
 * This routine is only used for mempart_alloc_policy_t_HIERARCHICAL
 * This routine will recurse back through a partition hierarchy starting
 * with partition <mp> and return the least amount of memory of memory class
 * <memclass_id>. It does this by returning the lesser of <size> and the
 * free size for <mp> at this or the parent partition (if the root has not been
 * reached)
*/
static memsize_t hierarchy_chk(mempart_t *mp, memsize_t size)
{
	memsize_t  free_sz;
	memsize_t  r;

	KASSERT(mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL);

	MUTEX_RDLOCK(&mp->info_lock);
	
	free_sz = free_size(mp);
	r =  min(size, free_sz);

	if ((mp->parent != NULL) && (r > 0)) {
		r = hierarchy_chk(mp->parent, r);
	}

	MUTEX_RDUNLOCK(&mp->info_lock);
	return r;
}

/*******************************************************************************
 * hierarchy_incr
 * 
 * This routine is only used for mempart_alloc_policy_t_HIERARCHICAL
 * This routine will recurse back through a partition hierarchy starting
 * with partition <mp> incrementing the cur_size for memory class <memclass_id>.
 * If a failure occurs a along the way, a 0 memsize_t value will be returned.
*/
static memsize_t hierarchy_incr(mempart_t *mp, memsize_t size)
{
	mempart_info_t  *mp_meminfo;
	memsize_t  s = size;
	memsize_t  resv_size;
	memsize_t  new_size;
	memsize_t  prev_size;

	MUTEX_WRLOCK(&mp->info_lock);

	KASSERT(mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL);

	mp_meminfo = &mp->info;

	/*
	 * stop going up the hierarchy if the current partition has enough
	 * unallocated reserved memory or if there is no parent.
	*/
	resv_size = reserve_size(mp_meminfo, size);

	if ((resv_size < size) && (mp->parent != NULL))
	{
		if ((s = hierarchy_incr(mp->parent, size - resv_size)) != (size - resv_size))
		{
			MUTEX_WRUNLOCK(&mp->info_lock);
#ifndef NDEBUG
			kprintf("%s: FAILED, only allocated %P of %P\n",
						(paddr_t)s, (paddr_t)(size - resv_size));
#endif	/* NDEBUG */
			return s;
		}
	}

	prev_size = mp_meminfo->cur_size;
	if ((mp_meminfo->cur_size += size) > mp_meminfo->hi_size) {
		mp_meminfo->hi_size = mp_meminfo->cur_size;
	}
	
	new_size = mp_meminfo->cur_size;	// grab cur_size before we unlock
	MUTEX_WRUNLOCK(&mp->info_lock);
	
	/* send event notifications to any interested parties */
	if (new_size != prev_size) {
		MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_INCR, new_size, prev_size);
	}
	
	return size;
}

/*******************************************************************************
 * hierarchy_decr
 * 
 * This routine is only used for mempart_alloc_policy_t_HIERARCHICAL
 * This routine will recurse back through a partition hierarchy starting
 * with partition <mp> decrementing the cur_size for memory class <memclass_id>.
 * If a failure occurs a along the way, a 0 memsize_t value will be returned.
*/
static memsize_t hierarchy_decr(mempart_t *mp, memsize_t size)
{
	mempart_info_t  *mp_meminfo;
	memsize_t  s = size;
	memsize_t  unresv_size;
	memsize_t  new_size;
	memsize_t  prev_size;

	MUTEX_WRLOCK(&mp->info_lock);

	KASSERT(mp->info.cur_cfg.policy.alloc == mempart_alloc_policy_t_HIERARCHICAL);

	mp_meminfo = &mp->info;

	unresv_size = unreserve_size(mp_meminfo, size);

	if ((unresv_size < size) && (mp->parent != NULL))
	{
		if ((s = hierarchy_decr(mp->parent, size - unresv_size)) != (size - unresv_size))
		{
			MUTEX_WRUNLOCK(&mp->info_lock);
			return s;
		}
	}
	prev_size = mp_meminfo->cur_size;
	mp_meminfo->cur_size -= size;

	new_size = mp_meminfo->cur_size;	// grab cur_size before we unlock
	MUTEX_WRUNLOCK(&mp->info_lock);
	
	/* send event notifications to any interested parties */
	if (new_size != prev_size) {
		MEMPARTMGR_EVENT(mp, mempart_evttype_t_DELTA_DECR, new_size, prev_size);
	}
	return size;
}



#ifndef NDEBUG

/*******************************************************************************
 * display_mempart_info
*/
static void display_mempart_info(mempart_t *mp)
{
	if (ker_verbose <= 1) return;

	kprintf("mempart @ %p, memory class id: 0x%x\n", mp, mp->memclass->data.info.id);
	kprintf("\tcreation cfg: min/max = %P / %P\n",
				(paddr_t)mp->info.cre_cfg.attr.size.min, (paddr_t)mp->info.cre_cfg.attr.size.max);
	kprintf("\tcurrent  cfg: min/max = %P / %P\n",
				(paddr_t)mp->info.cur_cfg.attr.size.min, (paddr_t)mp->info.cur_cfg.attr.size.max);
	kprintf("\tcur/hi = %P / %P\n",
			(paddr_t)mp->info.cur_size, (paddr_t)mp->info.hi_size);
}
#endif	/* NDEBUG */


__SRCVERSION("$IQ: mm_partitions_2.c,v 1.7 $");

