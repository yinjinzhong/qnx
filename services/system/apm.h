/* 
 * $QNXLicenseC:  
 * Copyright 2006, QNX Software Systems. All Rights Reserved.
 *
 * This source code may contain confidential information of QNX Software 
 * Systems (QSS) and its licensors.  Any use, reproduction, modification, 
 * disclosure, distribution or transfer of this software, or any software 
 * that includes or is based upon any of this code, is prohibited unless 
 * expressly authorized by QSS by written agreement.  For more information 
 * (including whether this source code file has been published) please
 * email licensing@qnx.com. $
*/

#ifndef _APM_H_
#define _APM_H_

#include <stdbool.h>

#ifdef __WATCOMC__
typedef char _Bool;	// not done in stdbool.h for watcom
#endif	/* __WATCOMC__ */ 

/*
 * this file contains external interfaces for the memory partitioning module
 * required outside of the memory manager and regardless of whether memory
 * partitioning is installed or not
*/
/*
 * ===========================================================================
 * 
 * 			Memory Partitioning Compile time Behaviour Modification
 *  
 * ===========================================================================
*/

/*
 * MEMPART_DFLT_INHERIT_BEHVR_1
 * MEMPART_DFLT_INHERIT_BEHVR_2
 * 
 * There are 2 behaviours under which memory partitioning will handle default
 * partition inheritance (when a child process is fork()'d/spawn()'d). These
 * are described below. By utilizing the mempart_flags_NO_INHERIT flag or
 * posix_spawn() with an appropriately initialized 'posix_spawnattr_t',
 * partition association can be explicitly controlled. 
 * 
 * MEMPART_DFLT_INHERIT_BEHVR_1
 * By default, all of the memory partitions associated with the parent (creating)
 * process which do not have their mempart_flags_NO_INHERIT flag set, will be
 * inherited by the child (created) process.
 * 
 * MEMPART_DFLT_INHERIT_BEHVR_2
 * By default, only the sysram memory class partition associated with the parent
 * (creating) process will be inherited by the by the child (created) process.
 * 
 * Exactly 1 of these options should be selected
 * 
 * It should be noted that both behaviours are programmatically supported via
 * posix_spawn() and an appropriately configured 'posix_spawnattr_t' object.
 * This compile flag simply selects which behaviour is default for those
 * existing programs which do not prgrammatically select a particular behaviour
 * 
 * There is no method of querying this setting as there is no expectation that
 * once established it will ever change and the default will be documented as
 * the selected behaviour.
*/
#define MEMPART_DFLT_INHERIT_BEHVR_1
//#define MEMPART_DFLT_INHERIT_BEHVR_2

/*
 * USE_PROC_OBJ_LISTS
 * 
 * whether to use the process and object lists to keep track of process and
 * object associations to a memory partition.
 * 
 * Note that it is extremely unlikely at this point that this define will ever
 * be turned off however it is here because the code still contains the
 * conditional compilation control ... although I have not compiled with this
 * #define off in many moons so it may not even build without it anymore.
 * 
 * Just leave it alone ... go away ... skedaddle
*/
#define USE_PROC_OBJ_LISTS

#include "mclass.h"
// FIX ME #include <sys/mempart.h>
#include "kernel/mempart.h"

#ifndef EXTERN
	#define EXTERN	extern
#endif

/*
 * ext_lockfncs_t
 *  
 * This type is to allow me to pass memory partitioning specific lock/unlock
 * functions to the common process association/disassociation implementation in
 * mm_mempart.c, which does not need the locking.
*/
typedef struct ext_lockfncs_s
{
	int (*rdlock)(void *);
	int (*rdunlock)(void *);
	int (*wrlock)(void *);
	int (*wrunlock)(void *);
} ext_lockfncs_t;

/*
 * mempart_flags_t
 * 
*/
typedef enum
{
	/* flags used to filter returns from MEMPART_GETLIST()
	 *  The flags which can be specified are
	 * 	mempart_flags_t_GETLIST_ALL			- no filtering, return all associated partitions
	 *	mempart_flags_t_GETLIST_INHERITABLE	- filter out those partitions which have the
	 * 										  mempart_flags_NO_INHERIT flag set
	 *	mempart_flags_t_GETLIST_CREDENTIALS	- filter out those partitions which do not have
	 * 										  appropriate credentials
	*/
	mempart_flags_t_GETLIST_ALL = 0,
	mempart_flags_t_GETLIST_INHERITABLE = 0x01,
	mempart_flags_t_GETLIST_CREDENTIALS = 0x02,
} mempart_flags_t;

typedef LINK4_NODE		mpart_qnode_t;
typedef LINK4_HDR		mpart_qnodehdr_t;

#ifdef USE_PROC_OBJ_LISTS
/* prp_node_t */
typedef struct prp_node_s
{
	mpart_qnode_t	hdr;
	PROCESS *	prp;
} prp_node_t;

/* obj_node_t */
typedef struct obj_node_s
{
	mpart_qnode_t	hdr;
	OBJECT *	obj;
} obj_node_t;
#endif	/* USE_PROC_OBJ_LISTS */

/*
 * mempart_t
 * 
 * This is the internal memory partition structure. It is created when a memory
 * partition for a memory class is added to the system (typically through the
 * resource manager).
 * 
 * This structure contains all of the attributes and policies for a partition
 * of a given class of memory. This structure is all that is required in order
 * to maintain the accounting data and enforce memory partition policies when
 * a process attempts to allocate/deallocate memory for its objects.
 * 
 * A linked list of these 'mempart_node_t' structures is maintained by each
 * process which contains a 'mempart_t' pointer to this structure
 * 
 * The structure is referenced from the respective resource manager structure
 * representing the partition and (as previously stated) by all of the processes
 * which have associated with this partition.
 *
 * The structure maintains associated process and object lists (ie. pointers to
 * all of the processes/objects that reference this partition) as well as a parent
 * partition pointer (which will be NULL if this is a root partition).
 * In addition is event information which allows asynchronous events to be sent
 * to registered process when specified event conditions occur.  
*/
#define MEMPART_SIGNATURE		0x13991187
struct memclass_entry_s;
typedef struct mempart_s
{
	_Uint32t		signature;		// used to help ensure not looking at a bogus mempart_t

	struct memclass_entry_s  *memclass;// pointer to the class to which this partition belongs

	struct mempart_s  *parent;	// pointer to the partition from which this partition
								// was derived (for the purpose of establishing creation
								// attributes)
	kerproc_lock_t	info_lock;	// protection for mempart_info_t
	mempart_info_t	info;		// (public) accounting and configuration data

#ifdef USE_PROC_OBJ_LISTS
	kerproc_lock_t	prplist_lock;	// protects the 'prp_list' 
	mpart_qnodehdr_t	prp_list;	// associated process list
	kerproc_lock_t	objlist_lock;	// protects the 'obj_list'
	mpart_qnodehdr_t	obj_list;	// associated object list
#endif	/* USE_PROC_OBJ_LISTS */

	void 			*rmgr_attr_p;	// back pointer to resource manager structure
} mempart_t;

/*
 * mempart_node_t
 * 
 * Per process partition structure.
 * When a process is associated with a partition, a 'mempart_node_t' is created
 * to reference the associated partition as well as hold per process partition
 * information, such as flags and heap information.
 * The 'mempart_node_t' added to the linked list of associated partitions for
 * the process. This also allows a partition to "reside" on several (process)
 * lists.
*/
typedef struct mempart_node_s
{
	mpart_qnode_t			hdr;
	struct mempart_s *		mempart;
	mempart_dcmd_flags_t	flags;		// per process partition flags
	memsize_t				mem_used;	// amount of physical memory used by the
										// process for the associated memory
										// class partition
} mempart_node_t;

/*
 * rsrcmgr_mempart_fnctbl_t
 * 
 * This type defines the table of interface functions provided by the memory
 * partitioning resource manager for use by the memory partitioning module.
*/
struct mempartmgr_attr_s;
typedef struct rsrcmgr_mempart_fnctbl_s
{
	int		(*validate_association)(struct mempartmgr_attr_s *attr, struct _cred_info *cred);
	void	(*mpart_event_handler)(struct mempartmgr_attr_s *attr, mempart_evttype_t evtType, ...);
	void	(*mclass_event_handler)(struct mempartmgr_attr_s *attr, memclass_evttype_t evtType,
										memclass_sizeinfo_t *cur, memclass_sizeinfo_t *prev);
} rsrcmgr_mempart_fnctbl_t;

/*
 * mempart_rsrcmgr_fnctbl_t
 * 
 * This type defines the table of interface functions provided by the memory
 * partitioning module for use by the memory partitioning resource manager.
*/
typedef struct mempart_rsrcmgr_fnctbl_s
{
	mempart_id_t (*create)(mempart_id_t parent_mpid, mempart_cfg_t *child_cfg, memclass_id_t memclass);
	int			(*destroy)(mempart_id_t mpid);
	int			(*config)(mempart_id_t mpid, mempart_cfg_t *cfg);
	mempart_info_t *	(*getinfo)(mempart_id_t mpid, mempart_info_t *info);
	PROCESS *	(*find_pid)(pid_t pid_filter, mempart_id_t mpid);
	int			(*validate_cfg_new)(mempart_id_t parent_mpid, mempart_cfg_t *cfg, memclass_id_t memclass_id);
	int			(*validate_cfg_change)(mempart_id_t mpid, mempart_cfg_t *cfg);
} mempart_rsrcmgr_fnctbl_t;

/*
 * mempart_fncTbl_t
 * 
 * This type defines the table of interface functions between the kernel
 * and the memory partitioning module. These functions (with the exception of
 * rsrcmgr_attach()), provide the means to account all allocations/deallocations
 * The rsrcmgr_attach() call is used by the partition resource manager code
 * to obtain the resource manager interfaces to the memory partitioning module.
 * It also allows the resource manager to supply a set of interface routines
 * for the memory partitioning module to use. 
*/
typedef struct mempart_fnctbl_s
{
	int			(*associate)(PROCESS *prp, mempart_id_t mpid, mempart_dcmd_flags_t flags, ext_lockfncs_t *lf);
	int			(*disassociate)(PROCESS *prp, mempart_id_t, ext_lockfncs_t *lf);
	int			(*obj_associate)(OBJECT *obj, mempart_id_t mpid);
	void		(*obj_disassociate)(OBJECT *o);
	int			(*chk_and_incr)(mempart_id_t mpid, memsize_t incr, memsize_t *resv_size);
	memsize_t	(*decr)(mempart_id_t mpid, memsize_t decr);
	int			(*incr)(mempart_id_t mpid, memsize_t incr);
	void		(*undo_incr)(mempart_id_t mpid, memsize_t incr, memsize_t resv_size);
	mempart_node_t *	(*get_mempart)(PROCESS *prp, memclass_id_t memclass_id);
	int			(*get_mempartlist)(PROCESS *prp, mempart_list_t *mpart_list, int n, mempart_dcmd_flags_t flags, struct _cred_info *cred);
	int			(*validate_association)(mempart_id_t mpid, struct _cred_info *cred); 
	mempart_rsrcmgr_fnctbl_t *  (*rsrcmgr_attach)(rsrcmgr_mempart_fnctbl_t *);
} mempart_fnctbl_t;

extern mempart_fnctbl_t *mempart_fnctbl;
extern mempart_fnctbl_t proxy_mempart_fnctbl;
extern memsize_t sys_mempart_sysram_min_size;
extern mempart_dcmd_flags_t default_mempart_flags;

/*==============================================================================
 * 
 * 				public interfaces for memory partition management
 * 
*/

/*
 * MEMPART_INSTALLED
 * 
 * True or False depending on whether or not memory partitioning is installed
 * and initialized (MEMPART_INSTALLED) and whether the memory partitioning
 * module is loaded (MEMPART_MODULE_LOADED) respectively.
*/
#define MEMPART_INSTALLED()			((mempart_fnctbl != NULL) && (mempart_fnctbl != &proxy_mempart_fnctbl))

/*
 * MEMPART_ASSOCIATE
 * 
 * Associate process <p> with partition identified by partition id <mp>
 * 
 * Returns: ENOSYS is no memory partitioning installed
 * 			EOK if successful, otherwise errno
 * 
*/
#define MEMPART_ASSOCIATE(p, mpid, f)	mempart_fnctbl->associate((p), (mpid), (f), NULL)

/*
 * MEMPART_DISASSOCIATE
 * 
 * disassociate PROCESS <p> from the associated partition identified by partition
 * id <mp>. If <mp> == NULL, the process will be disassociated from all of its
 * associated partitions (this is the situation when the address space for a
 * process is destroyed during process termination).
 * 
 * Returns: ENOSYS is no memory partitioning installed
 * 			EOK if successful, otherwise errno
*/
#define MEMPART_DISASSOCIATE(p, mpid)	mempart_fnctbl->disassociate((p), (mpid), NULL)

/*
 * MEMPART_OBJ_ASSOCIATE
 * 
 * Associate an object with a partition
 * 
 * Returns: EOK on success otherwise an errno
*/
#define	MEMPART_OBJ_ASSOCIATE(o, mp)	mempart_fnctbl->obj_associate((o), (mp))

/*
 * MEMPART_OBJ_DISASSOCIATE
 * 
 * Disassociate object <o> from its associated partition
 * The object is removed from its partition's associated object list. This
 * function is called when the object is destroyed (onbect_done()).
 * The ((o)->hdr.mpid == NULL check is an optimization to avoid the call if the object
 * is already associated with a partition
 * 
 * Returns: (void) nothing
 * 
 * currently the MEMPART_OBJ_DISASSOCIATE() function is not used for the reasons
 * specified in the Design Documentation (see also object_done())

#define	MEMPART_OBJ_DISASSOCIATE(o) \
		if ((MEMPART_INSTALLED()) && ((o) != NULL) && (((OBJECT *)(o))->hdr.mpid != NULL)) \
		{mempart_fnctbl->obj_disassociate((o));}
*/

/*
 * MEMPART_CHK_and_INCR
 * 
 * This macro provides a partition check and increment operation for atomically
 * checking partition free space and recording a desired allocation. If
 * successful, the requested allocation size will have been deemed to be available
 * in the partition and subsequently accounted for. The allocation itself will
 * not yet have been made by the allocator although the memory must be available.
 * 
 * Return:
 * Upon success, EOK will be returned, the allocation accounted for in the
 * partition and <resv> filled in to indicate what portion of the reservation
 * should be accounted against a preexisting reservation. This value is passed
 * to the allocator. The allocator should only be called if EOK is returned.
 * 
 * If unsuccessful, an errno indicating the problem will be returned and no
 * accounting operation will be performed. ENOMEM indicates that the allocation
 * is not permitted based on the partition rules. The contents if <resv> should
 * be considered garbage and the allocator should not be called to perform the
 * allocation.
 *
 * see also MEMPART_UNDO_INCR
*/
#define MEMPART_CHK_and_INCR(mp, inc, r) \
		((!MEMPART_INSTALLED()) ? EOK : mempart_fnctbl->chk_and_incr((mp), (inc), (r)))

/*
 * MEMPART_UNDO_INCR
 * 
 * This macro will undo the MEMPART_CHK_and_INCR() operation in the event of an
 * allocation failure.
 * The partition check/allocation sequence is optimized for the success path
 * hence the chk_and_incr() operation which is performed atomically in advance
 * of the actual allocation taking place. Under normal circumstances, the
 * allocation will succeed and the sequence is complete. If however the allocation
 * fails, we need to undo the previously accounted increment. The undo operation
 * must also take into account the portion of the allocation that was considered
 * to have come from a previous reservation. This is because there is no guarantee
 * that the same process thread will call the MEMPART_CHK_and_INCR() and then
 * the allocation routine atomically. It is possible for the MEMPART_CHK_and_INCR()
 * to be called by one process, be preempted and then a different process call
 * the MEMPART_CHK_and_INCR() followed by the allocation call prior to the first
 * process calling into the allocator. When the first process continues with its
 * allocation call, it may actually fail and if there was a portion of the
 * allocation that was to be made from reserved memory (which would have affected
 * the second processes value for the reserved memory portion of its allocation)
 * then this reserved memory request which didn't happen for the failed request
 * needs to be adjusted for as if it was attributed to the second successful
 * allocation.
 * Go read the design documentation, there is an example there.
 * 
 * Returns: nothing (void)
*/
#define MEMPART_UNDO_INCR(mp, inc, r) \
		if (MEMPART_INSTALLED()) {mempart_fnctbl->undo_incr((mp), (inc), (r));}

/*
 * MEMPART_DECR
 * 
 * This function is the complement of MEMPART_CHK_and_INCR which is used during
 * deallocation
 * 
 * Returns: (memsize_t) the amount of memory (0 to 100% of <free_size>) that should
 * 			be accounted back to a reservation in partition <mp> 
*/
#define MEMPART_DECR(mp, free_size) \
		((!MEMPART_INSTALLED()) ? 0 : mempart_fnctbl->decr((mp), (free_size)))

/*
 * MEMPART_INCR
 * 
 * This function is used to increment the cur_size for a partition. It currently
 * only used in managing the transfer of kernel objects between the system
 * partition and a process partition of the sysram memory class 
 * 
 * Returns: EOK or errno
*/
#define MEMPART_INCR(mp, req_size) \
		((!MEMPART_INSTALLED()) ? EOK : mempart_fnctbl->incr((mp), (req_size)))

/*
 * MEMPART_GETLIST
 * 
 * Retrieve the list of partitions associated with process <prp>.
 * This function will fill in the provided <mplist> for the process <p> filtering
 * the results based on the flags <f>.
 * The number of entries that can be filled in is specified by <n> and indicates
 * the space available in <mplist>
 * 
 * The flags which can be specified are
 * 		mempart_flags_t_GETLIST_ALL			- no filtering, return all associated partitions
 * 		mempart_flags_t_GETLIST_INHERITABLE	- filter out partitions which have the
 * 											  mempart_flags_NO_INHERIT flag is set
 *		mempart_flags_t_GETLIST_CREDENTIALS	- filter out partitions which do not have
 * 											  appropriate credentials as pointed to by <c>  
 * 
 * Returns: -1 on any error. If successful, the number of remaining partitions
 * 			which would not fit in <mplist> is returned. A return value of 0
 * 			indicates that all of the partitions are contained in <mplist>
*/
#define MEMPART_GETLIST(p, mplist, n, f, c) \
		mempart_fnctbl->get_mempartlist((p), (mplist), (n), (f), (c))


/*
 * MEMPART_VALIDATE_ID
 * 
 * Validate a mempart_id_t coming in from user space
 * 
 * Returns: EOK is <mpid> represents a valid memory partition identifier
 * 			otherwise an errno is returned
*/
#define MEMPART_VALIDATE_ID(mpid) \
		(MEMPART_INSTALLED() ? mempart_validate_id((mpid)) : EOK)

/*
 * MEMPART_VALIDATE_ASSOCIATION
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
#define MEMPART_VALIDATE_ASSOCIATION(mpid, c) \
		(MEMPART_INSTALLED() ? mempart_fnctbl->validate_association((mpid), (c)) : EOK)

/*
 * MEMPART_T_TO_ID
 * MEMPART_ID_TO_T
 * 
 * These macros translate between a mempart_id_t and a mempart_t *
 * They don't do much right now but have been coded into place in anticipation
 * of a more elaborate implementation
*/
#define MEMPART_T_TO_ID(m)	(((m) == NULL) ? mempart_id_t_INVALID : (mempart_id_t)(m))
#define MEMPART_ID_TO_T(i)	(((i) == mempart_id_t_INVALID) ? NULL : (mempart_t *)(i))

/*
 * MEMPART_NODEGET
 * 
 * This function will return a pointer to the 'mempart_node_t' corresponding to
 * <mc> for the process <p>
 * 
 * Returns: the partition pointer or NULL if no partition of the memory class
 * 			is associated with the process
*/
#define MEMPART_NODEGET(p, mc)	mempart_fnctbl->get_mempart((p), (mc))

/*
 * MEMPART_INIT
 * 
 * This function is called to initialize the memory partition module. There is
 * an module initialization function (initialize_apm()) which is called by
 * the module_init() which installs the (*mempart_init)() function pointer used
 * by this macro. Therefore, if the module is not included, (*mempart_init)()
 * will be NULL, and the module will not be initialized. This function will
 * install (*mempart_fnctbl) and (*mempart_rsrcmgr_fnctbl) function pointer tables
 * and effectively activate all aforementioned MEMPART macros.
 * See the comments in apm.c for initialize_apm() and _mempart_init()
 * for rationale for this 2 stage initialization.
 * 
 * This routine will also create the mandatory default system partition.
 * For arguments, this function takes the system memory class size <s> and system
 * memory class name <n> (as currently obtained from pa_init()) in order to create
 * the system partition with system memory class (the partition and class against
 * which all allocations are accounted unless other partitions/classes are created
 * and used)
 * 
 * Returns: (void) nothing
*/
#define MEMPART_INIT(s, mc)		mempart_init((s), (mc))


/* MEMSIZE_OFLOW
 * 
 * external size of the 'memsize_t' type is always 64 bits. The internal
 * representation of 'memsize_t' is always set to 'paddr_t' and it is the size
 * as defined by _PADDR_BITS. If _PADDR_BITS != 64, then the internal representation
 * of memsize_t is not the same as the external size and hence a check for
 * overflows on incoming attributes must be made
 * 
 * Note that the assumption is that the external representation of 'memsize_t' is
 * always >= the internal representation of 'memsize_t'
 * 
 * The parameter <s> being checked will always be a 64bit quantity
 * 
 * Returns: TRUE or FALSE depending on whether an overflow is detected
*/
#if (_PADDR_BITS == 64)	// I wish this could be sizeof(_MEMSIZE_T_) != sizeof(memsize_t)

#define MEMSIZE_OFLOW(s)		bool_t_FALSE
#undef _MEMSIZE_and_memsize_are_different_

#else	/* (_PADDR_BITS == 64) */

#define MEMSIZE_OFLOW(s) \
		(((s) != memsize_t_INFINITY) && (((s) & 0xFFFFFFFF00000000ULL) != 0ULL))
#define _MEMSIZE_and_memsize_are_different_

#endif	/* (_PADDR_BITS == 64) */

/*
 * LIST manipulation macros
*/
#ifdef __GNUC__
#define TYPEOF(t)	typeof(t)
#else	/* __GNUC__ */
#define TYPEOF(t)	// ok for now because the LINKx macros don't current use the type anyway
#endif	/* __GNUC__ */
#define LIST_INIT(_q)		LINK4_INIT((_q))
#define LIST_FIRST(_q)		(_q).head
#define LIST_LAST(_q)		(_q).tail
#define LIST_COUNT(_q)		(_q).count
#define LIST_ADD(_q, _n)	LINK4_END((_q), &(_n)->hdr, TYPEOF(_n))
#define LIST_DEL(_q, _n)	LINK4_REM((_q), &(_n)->hdr, TYPEOF(_n))
#define LIST_NEXT(_n)		LINK2_NEXT(&(_n)->hdr, TYPEOF(_n))
#define LIST_PREV(_n)		LINK2_PREV(&(_n)->hdr, TYPEOF(_n))

memsize_t free_mem(pid_t pid, mempart_id_t mpid);

/* mm_mempart.c */
extern void (*mempart_init)(memsize_t sysmem_size, memclass_id_t memclass_id);
mempart_id_t mempart_getid(PROCESS *prp, memclass_id_t mclass);
memclass_id_t mempart_get_classid(mempart_id_t mpid);
int mempart_validate_id(mempart_id_t mpid);
bool mempart_module_loaded(void);

/* mm_memclass.c */
memclass_id_t memclass_add(const char *memclass_name, memclass_attr_t *attr, allocator_accessfncs_t *f);
/* FIX ME - try to have memclass_find() return a memclass_id_t */
memclass_entry_t *memclass_find(const char *name, memclass_id_t id);
int memclass_delete(const char *name, memclass_id_t id);
memclass_info_t *memclass_info(memclass_id_t id, memclass_info_t *info);


#endif	/* _APM_H_ */

/* __SRCVERSION("$IQ: apm.h,v 1.91 $"); */
