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

#ifndef _MEMPART_H_
#define _MEMPART_H_

#ifndef __PLATFORM_H_INCLUDED
#include <sys/platform.h>
#endif

#include <stdint.h>
#include <limits.h>
#include <signal.h>
// FIX ME #include <sys/memclass.h>
#include "kernel/memclass.h"

/*
 * mempart_attr_t
 * 
 * This type defines the attributes used in creating a memory partition
 * 
 * size.min	- A size.min > 0 will guarantee the specified amount of memory that
 * 			  can be allocated by processes associated with the partition but it
 * 			  does not guarantee that an allocation will be successful if the
 * 			  allocation fails for reasons other than for errno == ENOMEM.
 * 			  size.min can never be greater than size.max.
 * size.max	- size.max will limit the amount of memory that can be allocated
 * 			  by processes associated with the partition to the specified value.
 * 			  size.max can never be less than size.min.
 * 
 * When creating a partition with non-zero size.min and size.max other than
 * memsize_t_INFINITY, the values are constrained by the memclass_limits_t.alloc.min
 * setting for the memory class. That is, values must be chosen which are modulo
 * the memclass_limits_t.alloc.min
*/
typedef struct mempart_attr_s
{
	struct {
		_MEMSIZE_T_		min;
		_MEMSIZE_T_		max;
	} size;
} mempart_attr_t;


/*
 * mempart_alloc_policy_t
 * 
 * Valid memory partition allocation policies
*/
typedef _Uint32t	mempart_alloc_policy_t;
typedef enum
{
	/*
	 * mempart_alloc_policy_t_ROOT
	 *	  -	all allocations (including reservations) are autonomous and not
	 * 		accounted for in a partition hierarchy. The parent/child partition
	 * 		relationship exists to constrain partition creation. A hieracrhy may
	 * 		exist in the namespace, however child partitions operate independently
	 * 		for the purpose of accounting allocations
	*/
	mempart_alloc_policy_t_ROOT = 1,
	
	/*
	 * mempart_alloc_policy_t_HIERARCHICAL
	 * 	  -	all allocations (including reservations) are accounted for in the
	 * 		partition hierarchy. This allocation policy results in a completely
	 * 		hierarchical relationship in which memory allocated by a process
	 * 		associated with a given partition must be available in all partitions
	 * 		higher in the tree up to and including the root partition and 
	 * 		associated memory class allocator
	*/
	mempart_alloc_policy_t_HIERARCHICAL,

mempart_alloc_policy_t_last,
mempart_alloc_policy_t_first = mempart_alloc_policy_t_ROOT,
mempart_alloc_policy_t_DEFAULT = mempart_alloc_policy_t_HIERARCHICAL,
} mempart_alloc_policy_t_val;


/*
 * mempart_policy_t
 * 
 * This structure defines the set of policies by which a partition operates.
 * The allocation policies are described above.
 * The boolean policies are write once in that once set TRUE, they can never be
 * changed. The purpose of this is to ensure a partition topology is not
 * compromised. These boolean policies are independent of each other and of
 * posix name space permissions (however write persmissions are required to
 * change a policy) and operate as follows ...
 * 		terminal - if TRUE, no child partitions may be created
 * 		config_lock - if TRUE, the partition attributes and allocation policy
 * 					  cannot be changed
 * 		permanent - if TRUE, the partition cannot be destroyed
*/
typedef struct mempart_policy_s
{
	apm_bool_t				terminal;	// child partitions are permitted (T/F) ?
	apm_bool_t				config_lock;		// configuration is locked (T/F) ?
	apm_bool_t				permanent;			// partition is permanent (T/F) ?
	mempart_alloc_policy_t	alloc;				// the partition allocation policy

} mempart_policy_t;

#define MEMPART_DFLT_POLICY_INITIALIZER \
		{ \
			STRUCT_FLD(terminal) bool_t_FALSE, \
			STRUCT_FLD(config_lock) bool_t_FALSE, \
			STRUCT_FLD(permanent) bool_t_FALSE, \
			STRUCT_FLD(alloc) mempart_alloc_policy_t_HIERARCHICAL, \
		}

/*
 * mempart_id_t
 * 
 * A unique memory partition identifier that can be retrieved with the appropriate
 * devctl() to the memory partition manager and subsequently passed in calls to
 * posix_spawn() thereby allowing a spawned process to be associated with the memory
 * partition identified by 'mempart_id_t'.
 * A 'mempart_id_t' is also provided in xxx_PARTITION_ASSOCIATION and
 * xxx_PARTITION_DISASSOCIATION events 
 *  
 * FIX ME - When process is spawned, it can get a list of its own partitions by
 * 			doing the devctl(MEMPART_GET_PLIST) on itself (ie open("/proc/self")).
 * 			Now, the partition id is not horrible useful by itself, but if the
 * 			class identifier was encoded into the partition identifier, then the
 * 			process could intelligently decide whether or not a memory class
 * 			should be associated with a spawned process. This would eliminate any
 * 			open()/readdir() sequences on partitions. Of course, if the process
 * 			wanted to obtain additional information about a partition, it would
 * 			require namespace parsing
*/
typedef _Uint32t	mempart_id_t;
#define mempart_id_t_INVALID	((mempart_id_t)0)

/*
 * mempart_cfg_t
 * 
 * This type defines the configuration attributes and policies for the partition.
 * This type is used both to return the configuration state of the partition as
 * well as to allow the configuration to be changed (see mempart_cfgchg_t)
*/
typedef struct mempart_cfg_s
{
	mempart_policy_t	policy;
	mempart_attr_t		attr;
} mempart_cfg_t;

/*
 * mempart_cfgchg_t
 * 
 * This type is used to change the configuration attributes and policies for the
 * partition. The 'valid' field allows the caller to specify which attributes
 * and/or polices should be changed without changing the other attributes and/or
 * policies and therefore does not require knowledge of the partition's current
 * configuration
*/
typedef _Uint32t	cfgchg_t;
typedef enum {
	cfgchg_t_NONE = 0,				

	cfgchg_t_ALLOC_POLICY = 0x1,	// the following flags when set indicate
	cfgchg_t_TERMINAL_POLICY = 0x2,	// a valid mempart_cfg_t value in val field.
	cfgchg_t_LOCK_POLICY = 0x4,		// An unset flag will cause the corresponding
	cfgchg_t_PERMANENT_POLICY = 0x8,// val field value to be left unchanged
	cfgchg_t_ATTR_MAX = 0x10,
	cfgchg_t_ATTR_MIN = 0x20,

	cfgchg_t_ALL = 0xff,
} cfgchg_t_val;
typedef struct mempart_cfgchg_s
{
	cfgchg_t		valid;
	mempart_cfg_t	val;
} mempart_cfgchg_t;

/*
 * mempart_meminfo_t
 *  
 * This type defines the information that can be obtained from a memory
 * partition. Its fields have the following meaning
 *
 * cfg		- partition creation attributes. This field contains the attributes
 * 			  that were specified when the partition was originally created. The
 * 			  values may or may not be the same as the current partition attributes.
 * 			  This value is provided as a record of the partition creation history
 * 			  only and is not otherwise used.
 * 
 * cur		- current partition attributes
 * 			  The cur.min_size and cur.max_size specify what the current min_size
 * 			  and max_size attributes are (configuration attributes may be altered
 * 			  with the MEMPART_CFG_CHG devctl()). At partition creation, 'cur' will
 * 			  be equal to 'cfg'.
 * 
 * cur_size - current partition size.
 * 			  This value indicates the amount of memory that has been successfully
 * 			  allocated by processes associated with the partition. Successful
 * 			  allocations include the min_size for any partition lower in the
 * 			  partition hierarchy. The difference between max_size and cur_size
 * 			  represents the amount of memory that 'may' be allocated by processes
 * 			  associated with the partition but does not necessarily guarantee
 *			  that the amount of memory is actually available.
 * 
 * hi_size	- indicates the maximum amount of memory that has ever been allocated
 * 			  by any process associated with the partition since the value was
 * 			  reset. The value will be reset to 0 upon partition creation or with
 * 			  the MEMPART_CNTR_RESET devctl(). hi_size could be greater than
 * 			  max_size if the partition attributes are changed such that cur.max_size
 * 			  is no longer equal to cfg.max_size.
 * 
 * id		- partition identifier
*/
typedef struct mempart_meminfo_s
{
	_MEMSIZE_T_		cur_size;		// current partition size
	_MEMSIZE_T_		hi_size;		// largest cur_size that this partition has ever been
	mempart_cfg_t	cre_cfg;		// configuration at partition creation
	mempart_cfg_t	cur_cfg;		// current configuration
	mempart_id_t	id;				// partition identifier
	_Uint32t		reserved[3];
} mempart_info_t;


/*
 * memclass_evtflags_t
 * 
 * This structure defines the attributes of events that may be registered on
 * a memory partition. Events may be global (they apply to all defined memory
 * partition events) or specific to an event type
*/
typedef _Uint32t	mempart_evtflags_t;
typedef enum
{
	mempart_evtflags_DISARM = 0x0,		// Default action
										// the event, once delivered is disarmed
										// and must be re-registered in order to
										// be received again.
	mempart_evtflags_REARM = 0x10,		// the event, once delivered will remain in effect

} mempart_evtflags_t_val;

/*
 * memclass_evttype_t
 * 
 * This structure defines the set of events that can be registered with a
 * memory partition and subsequently received by the caller
*/
typedef _Int32t	mempart_evttype_t;
typedef enum
{
mempart_evttype_t_INVALID = -1,

	mempart_evttype_t_THRESHOLD_CROSS_OVER,
	mempart_evttype_t_THRESHOLD_CROSS_UNDER,
	mempart_evttype_t_DELTA_INCR,
	mempart_evttype_t_DELTA_DECR,
	mempart_evttype_t_CONFIG_CHG_POLICY,
	mempart_evttype_t_CONFIG_CHG_ATTR_MIN,
	mempart_evttype_t_CONFIG_CHG_ATTR_MAX,
	mempart_evttype_t_PARTITION_CREATE,
	mempart_evttype_t_PARTITION_DESTROY,
	mempart_evttype_t_PROC_ASSOCIATE,
	mempart_evttype_t_PROC_DISASSOCIATE,

	// not sure what these next 2 could be used for other than debug
	mempart_evttype_t_OBJ_ASSOCIATE,
	mempart_evttype_t_OBJ_DISASSOCIATE,

// (mempart_evttype_t_last - mempart_evttype_t_first) is always the number of event types
mempart_evttype_t_last,	
mempart_evttype_t_first = mempart_evttype_t_THRESHOLD_CROSS_OVER,
} mempart_evttype_t_val;

/* convenience macros */
#define NUM_MEMPART_EVTTYPES		(mempart_evttype_t_last - mempart_evttype_t_first)
#define MEMPART_EVTYPE_IDX(etype)	((etype) - mempart_evttype_t_first)

/*
 * mempart_evtparam_t
 * 
 * This structure defines the parameters of an event that may be registered
 * with a memory partition.
 * For each event type, an optional value may be provided which acts as a filter
 * to the event delivery mechanism. For example, a threshold crossing event
 * requires an absolute value (between 0 and the maximum size attribute of the
 * memory partition) through which the memory partition current size value
 * (mempart_info_t.cur_size) must cross before the event is delivered.
*/
typedef struct mempart_evtparam_s
{
	mempart_evttype_t	type;	// event type
	_Uint32t			reserved[3];
	union {
		// event types
		//		mempart_evttype_t_THRESHOLD_CROSS_OVER
		//		mempart_evttype_t_THRESHOLD_CROSS_UNDER
		//
		// a notification event is generated when the partition crosses the
		// specified threshold in the specified direction. The new current size
		// of the partition is provided
// FIX ME - how will we deliver a _MEMSIZE_T == 64 bits event 
		_MEMSIZE_T_	threshold_cross;

		// event types
		//		mempart_evttype_t_DELTA_INCR
		//		mempart_evttype_t_DELTA_DECR
		//
		// a notification event is generated when the partition changes size
		// by the specified delta in the specified direction from the last time
		// the event was received or when it is first registered.
		// The new current size of the partition is provided
// FIX ME - how will we deliver a _MEMSIZE_T == 64 bits event 
		_MEMSIZE_T_	delta;

		// event types
		//		mempart_evttype_t_CONFIG_CHG_POLICY
		//		mempart_evttype_t_CONFIG_CHG_ATTR_MIN
		//		mempart_evttype_t_CONFIG_CHG_ATTR_MAX
		// a notification event is generated when the partition configuration
		// changes. The config change is separated into either a policy change
		// or an attribute change with each a separately configurable event.
		//
		// The policy changes are further subdivided into a bitmask for the boolen
		// policies (terminal, config lock and permanent) and an 8 bit field for
		// the allocation policy. Since the boolen policies can only ever transition
		// from FALSE to TRUE, the bit position represented by cfgchg_t_xxx will
		// indicate the current policy setting. The alloc policy will be provided
		// as the current value. Any change in any of the allocation policies
		// will cause an mempart_evttype_t_CONFIG_CHG_POLICY event to be sent.
		// 
		// Attribute changes are subdivided into either a change to the minimum
		// (reserved) value or a change to the maximum (restriction) value. A
		// mempart_evttype_t_CONFIG_CHG_ATTR_MIN or mempart_evttype_t_CONFIG_CHG_ATTR_MAX
		// event will be sent repectively.
// FIX ME - how will we deliver a _MEMSIZE_T == 64 bits event 
		union cfg_chg_s {
			struct {
				_Uint8t		b;		// bitset of the boolean policies
				_Uint8t		alloc;	// mempart_alloc_policy_t encoded into a _Uint8t
				_Uint8t		reserved[6];
			} policy;
			union {
				_MEMSIZE_T_	min;
				_MEMSIZE_T_	max;
			} attr;
		} cfg_chg;

		// event types
		//		mempart_evttype_t_PARTITION_CREATE
		//		mempart_evttype_t_PARTITION_DESTROY
		// A notification event is generated when a partition is created as the
		// child of the partition or memory class on which the caller is registering.
		// The partition identifier for the created partition is provided.
		//
		// A notification event is also generated for a partition destruction event
		// and the caller has the option of specifying an 'mpid' indicating interest
		// in the destruction of a specific existing child partition. A value of
		// 0 will cause an event for any child partition destruction.
		//
		// NOTE that in a partition hierarchy, only the immediate child partitions
		// of the partition or memory class to which the event has been registered
		// will cause and event to be generated.
		mempart_id_t	mpid;

		// event types
		//		mempart_evttype_t_PROC_ASSOCIATE
		//		mempart_evttype_t_PROC_DISASSOCIATE
		// a notification event is generated when a process associates with or
		// disassociates from the partition to which the event is registered.
		// The caller has the option of specifying a 'pid' indicating interest
		// in the disassociation of a specific existing process. A value of 0
		// will cause an event for any process disassociation from the partition
		// to which the event is registered.
		//
		// NOTE that in a partition hierarchy, only the processes associated with
		// or disassociated from the partition to which the event has been registered
		// will cause and event to be generated.
		pid_t	pid;

		// event types
		//		mempart_evttype_t_OBJ_ASSOCIATE
		//		mempart_evttype_t_OBJ_DISASSOCIATE
		// a notification event is generated when a process creates an object
		// which is associated with or an object disassociates from the partition
		// to which the event is registered.
		// The caller has the option of specifying a 'oid' indicating interest
		// in the disassociation of a specific existing associated object.
		// A value of 0 will cause an event for any object disassociation from
		// the partition to which the event is registered.
		//
		// NOTE that in a partition hierarchy, only the processes associated with
		// or disassociated from the partition to which the event has been registered
		// will cause and event to be generated.
		void *	oid;

	} val;
} mempart_evtparam_t;

/*
 * mempart_evtreg_t
 * 
 * The structure defines the event registration structure. This structure is
 * a variable size structure which is used by applications to register to
 * receive 1 or more events of interest from partition of a memory class.
 * The same event may be registered multiple times with different parameter
 * values.
 * When registering an event, the caller is required to provide properly
 * initialized mempart_evtparam_t, sigevent and mempart_evtflags_t values.
 * 
 * The macros FIX ME can be used to aid in the initialization of the
 * mempart_evtreg_t structure. 
*/
typedef struct mempart_evtreg_s
{
	_Uint32t	num_entries;	// number of mempart_evtparam_t structures which follow
	_Uint32t	reserved[3];
	struct mp_evt_info_s { 
		mempart_evtparam_t		info;	// the event to register for
		struct sigevent			sig;	// sigevent
		mempart_evtflags_t		flags;	// event specific flags
		_Uint32t				reserved;
	} evt[1];
} mempart_evtreg_t;
/*
 * MEMPART_EVTREG_T_SIZE
 * 
 * convenience macro to calculate the size (in bytes) of the resulting
 * 'mempart_evtreg_t' structure given 'n' where 'n' (typically) represents the
 * value of the 'mempart_evtreg_t.num_entries' field.
 * 
 * Note:
 * 		'n' must be >= 0
 * 		implementation is coded to ensure the proper result even for 'n' == 0 
*/
#define MEMPART_EVTREG_T_SIZE(n) \
		((sizeof(mempart_evtreg_t) - sizeof(struct mp_evt_info_s)) + \
		((n) * sizeof(struct mp_evt_info_s)))

/*
 * mempart_plist_t
 * mempart_olist_t
 * 
 * structures used to retrieve the list of processes (pid_t) or objects
 * associated with a memory partition respectively
*/
typedef struct mempart_plist_s
{
	_Int32t	num_entries;		// number of 'pid' information structures
	pid_t	pid[1];
} mempart_plist_t;
/*
 * MEMPART_PLIST_T_SIZE
 * 
 * convenience macro to calculate the size (in bytes) of the resulting
 * 'mempart_plist_t' structure given 'n' where 'n' (typically) represents the
 * value of the 'mempart_plist_t.num_entries' field.
 * 
 * Note:
 * 		'n' must be >= 0
 * 		implementation is coded to ensure the proper result even for 'n' == 0
*/
#define MEMPART_PLIST_T_SIZE(n) \
		((sizeof(mempart_plist_t) - sizeof(pid_t)) + ((n) * sizeof(pid_t)))

typedef struct mempart_olist_s
{
	_Int32t		num_entries;		// number of 'obj' information structures
	_Uint32t	reserved;
	struct obj_info_s
	{
		_Uint8t					type;
		_Uint8t					size;
		_Uint16t				flags;
		mempart_id_t			mpid;
		_Uint32t				reserved[2];
	} obj[1];
} mempart_olist_t;
/*
 * MEMPART_OLIST_T_SIZE
 * 
 * convenience macro to calculate the size (in bytes) of the resulting
 * 'mempart_olist_t' structure given 'n' where 'n' (typically) represents the
 * value of the 'mempart_olist_t.num_entries' field.
 * 
 * Note:
 * 		'n' must be >= 0
 * 		implementation is coded to ensure the proper result even for 'n' == 0
*/
#define MEMPART_OLIST_T_SIZE(n) \
		((sizeof(mempart_olist_t) - sizeof(void *)) + ((n) * sizeof(void *)))

/*
 * mempart_dcmd_flags_t
 * 
 * Memory Partitioning specific flags which can be set and retrieved with the
 * DCMD_ALL_SETFLAGS and DCMD_ALL_GETFLAGS respectively
 * 
 * Some notes on the CREATE and SHARE flags
 * If the 'mempart_flags_HEAP_CREATE' flag is set, a kernel heap for the memory
 * class represented by the partition for which the flag is set, WILL be created.
 * If the 'mempart_flags_HEAP_SHARE' is not set then this will be a private
 * heap for the exclusive use of the caller. If 'mempart_flags_HEAP_SHARE' is
 * set as well, then the heap WILL be created (even if there are other shared
 * heaps from the same partition) but it will also then be shareable. This means
 * that if in the future, a process associates with the partition with only the
 * 'mempart_flags_HEAP_SHARE' flag set, then that process will use the same heap
 * as the first process it finds that is associated with the partition that also
 * has the 'mempart_flags_HEAP_SHARE' flag set. Since multiple processes could
 * associate with a partition with both the 'mempart_flags_HEAP_CREATE' and
 * 'mempart_flags_HEAP_SHARE' flags set, the actual heap to be shared will depend
 * on the chronological order in which the processes associate as well as whether
 * or not the process that created a shared heap is still present in the system.
 * 
 * If a process associates with a partition and only has the 'mempart_flags_HEAP_SHARE'
 * flag set and there are no other processes associated with the partition which
 * have the 'mempart_flags_HEAP_SHARE' flag set then the behaviour will be as if
 * both the 'mempart_flags_HEAP_CREATE' and 'mempart_flags_HEAP_SHARE' flags were
 * specified.
 * 
 * If a process associates with a partition and does not specify either the
 * 'mempart_flags_HEAP_CREATE' or 'mempart_flags_HEAP_SHARE' flags, then no
 * kernel heap will be created. The memory class represented by the partition
 * will still govern user memory allocations, however no memory of the class
 * will be available for internal memory allocations. This is not a problem as
 * most internal memory allocations utilize the 'sysram' memory class and when
 * a process is created, the system automatically ensures that it is associated
 * with a partition of the 'sysram' memory class either by inheritance or by
 * ensuring that the the appropriate paremeters to posize_spawn() are provided. 
 * 
*/
typedef _Uint32t	mempart_dcmd_flags_t;
typedef enum
{
	mempart_flags_NONE = 0,
	mempart_flags_NO_INHERIT = 0x01,	// partition will not be inherited

	mempart_flags_HEAP_CREATE = 0x10,	// create a process private kernel HEAP for
										// the memory class represented by the partition
	mempart_flags_HEAP_SHARE = 0x20,	// find (or create) a common heap for the
										// memory class represented by the partition
	mempart_flags_USE_ID = 0x100,		// interpret mempart_list_t.i[].t as an
										// mempart_id_t instead of a name pointer
	mempart_flags_RESERVED = 0xF0000000U,// top nibble is reserved 
} mempart_dcmd_flags_t_val;

/*
 * mempart_list_t
 * 
 * This type is used to obtain the partition names that a process is associated
 * with. The list will always contain the actual partition for the particular
 * memory class and not a partition group name (ie. partition groups will be
 * resolved down to the specific memory partition).
 * FIX ME - this needs to be a generic partition list type so that groups can
 * be handled properly 
*/
typedef struct mempart_list_s
{
	_Int32t 	num_entries;
	_Uint32t	reserved;
	struct list_info_s {
		union {							// when associating a with a partition,
			mempart_id_t	id;			// the caller may provide either a name
			char *			name;		// or a partition identifier. This choice
		} t;							// is controlled in 'flags'
		mempart_dcmd_flags_t flags;		// process specific flags pertaining to the
										// association with the partition identified
										// by 't.id'
		_Uint16t			prime;		// a non-zero value will cause the created
										// kernel heap to be allocated 'prime' bytes
										// of memory from the physical page allocator
										// NOTE that the size specified must be a
										// multiple of 4096 bytes (between 0 and 0xF000)
										// or it will be rounded down to the closest
										// multiple
		_Uint16t			reserved[3];
	} i[1];
} mempart_list_t;
/*
 * MEMPART_LIST_T_SIZE
 *
 * convenience macro to calculate the size (in bytes) of the resulting
 * 'mempart_list_t' structure given 'n' where 'n' (typically) represents the
 * value of the 'mempart_list_t.num_entries' field.
 * 
 * Note:
 * 		'n' must be >= 0
 * 		implementation is coded to ensure the proper result even for 'n' == 0
*/
#define MEMPART_LIST_T_SIZE(n) \
		((sizeof(mempart_list_t) - sizeof(struct list_info_s)) + \
		((n) * sizeof(struct list_info_s)))


/*
 * Memory Partition devctl()'s (_DCMD_MEMPART)
 * 
 * MEMPART_GET_ID
 * 	retrieve the partition identifier for the partition identified by 'fd'. This
 * 	devctl() is used when a process wishes to posix_spawn() a new process and
 * 	associate that new process with the aforementioned partition. The returned
 * 	partition identifier is an opaque type that has no interpretation for the
 * 	caller. It can be used to set the 'posix_spawnattr_t' attributes structure
 * 	prior to calling posix_spawn(). The spawned process will then be associated
 * 	with the partition corresponding to the returned identifier.
 * 	The partition identifier is also returned in partition association and
 * 	disassociation events.
 * 
 * 	On success, EOK will be returned by devctl() and the caller provided
 * 	'mempart_id_t' will contain the applicable information.
 * 	If any error occurs, the caller provided 'mempart_id_t' should be considered
 * 	to contain garbage.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the specified number of events being registered
 * 		EBADF - the MEMPART_GET_ID devctl() has been issued on a name which
 * 				does not represent a memory partition
 * 
 * MEMPART_GET_INFO
 * 	return memory partition information (mempart_info_t) for the partition
 * 	identified by 'fd'.
 *  On success, EOK will be returned by devctl() and the caller provided
 * 	'mempart_info_t' structure will contain the applicable information.
 * 	If any error occurs, the caller provided mempart_info_t structure should
 * 	be considered to contain garbage.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the specified number of events being registered
 * 		EBADF - the MEMPART_GET_INFO devctl() has been issued on a name which
 * 				does not represent a memory partition
 *		EIO - internal I/O error. There is no information available or the
 * 				request to retrieve the information failed.
 * 
 * MEMPART_CFG_CHG
 * 	change the current configuration attributes for the partition identified by
 *  'fd' to those specified in the provided 'mempart_cfgchg_t'. The success of
 *  this operation depends on the current partition configuration, current
 *  parent partition configuration, allocation use and the amount of unallocated
 *  unreserved memory of the class the partition represents. The reconfiguration
 *  will fail if ...
 * 		- size.max attribute is specified to be less than the current allocation
 * 		  size (ie. cur_size)
 * 		- the specified size.min or size.max is greater than the size.max of the
 * 		  parent partition (FIX ME - Peter V change)
 * 		- a size.min (ie reservation) is specified which cannot be satisfied
 * 		- the attribute values are invalid (ex. size.max is less than size.min)
 *  On success, EOK will be returned by devctl() and the caller can assume the
 * 	partition configuration has been successfully changed to that those provided.
 * 	If any error occurs, the caller can assume that no partition changes were
 * 	made.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the specified number of events being registered
 * 		EBADF - the MEMPART_CFG_CHG devctl() has been issued on a name which
 * 				does not represent a memory partition
 * 
 * MEMPART_EVT_REG
 * 	register the caller to receive asynchronous notification of specified number
 *  of partition events
 *  On success, EOK will be returned by devctl() and the caller can assume that
 * 	all events have been successfully registered.
 * 	If any error occurs, none of the requested events will be registered.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EACESS - the caller does not have read permission on the memory class
 * 		EOVERFLOW - the caller has not provided a buffer large enough for the
 * 					specified number of events
 * 				  - the caller has provided a event parameter value which could
 * 					not be internally represented. This can occur if the processor
 * 					does support a 64 bit types
 * 		EBADF - the MEMPART_EVT_REG devctl() has been issued on a name which
 * 				does not represent a memory partition
 * 		EINVAL - at least 1 of the events to be registered contained invalid
 * 				 data
 * 		ENOMEM - the memory necessary to register the events could not be
 * 				allocated
 * 
 * MEMPART_CNTR_RESET
 * 	reset the the hi_size high water mark counter. You may want to use this to
 * 	reset the counters if the configuration parameters of the partition are
 *	modified. The hi water mark value at the time the reset occurs will be
 * 	returned i the caller provided 'memsize_t' buffer
 * 
 * MEMPART_GET_ASSOC_PLIST
 * 	return the list (mempart_plist_t) of processes associated with the
 * 	partition identifed by 'fd'.
 *  On success, EOK will be returned by devctl() and the caller provided
 * 	'mempart_plist_t' structure will contain some or all if the applicable
 * 	information (see @@ below)
 * 	If any error occurs, the caller provided mempart_plist_t structure should
 * 	be considered to contain garbage.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the amount of information being requested.
 * 		EBADF - the MEMPART_GET_ASSOC_PLIST devctl() has been issued on a name
 * 				which does not represent a memory partition
 *		EIO - internal I/O error. There is no information available or the
 * 				request to retrieve the information failed.
 * @@
 * When the caller provides the 'mempart_plist_t' buffer, the num_entries
 * field can be set to any value >= 0 as long as enough storage has been provided
 * to accomodate a value > 0. If there are more entries to be returned than can fit
 * in the space specified by num_entries, then num_entries will be modified on
 * return to be the 2's compliment of the number of entries that would not fit.
 * Therefore, if you wish to first determine how many entries are required,
 * first pass in a mempart_plist_t buffer with num_entries set to zero. On
 * successful return, num_entries will be the 2's compliment (ie. negative) of
 * the num_entries that the caller should allocate space for (ie. num_entries = -10
 * indicates that the caller should allocate space for 10 entries).
 * If num_entries == 0 on successful return, all requested entries were returned.
 * 
 * MEMPART_GET_ASSOC_OLIST
 * 	return the list (mempart_olist_t) of objects associated with the
 * 	partition identifed by 'fd'.
 *  On success, EOK will be returned by devctl() and the caller provided
 * 	'mempart_olist_t' structure will contain some or all if the applicable
 * 	information (see @@ below)
 * 	If any error occurs, the caller provided mempart_olist_t structure should
 * 	be considered to contain garbage.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the amount of information being requested.
 * 		EBADF - the MEMPART_GET_ASSOC_OLIST devctl() has been issued on a name
 * 				which does not represent a memory partition
 *		EIO - internal I/O error. There is no information available or the
 * 				request to retrieve the information failed.
 * @@
 * When the caller provides the 'mempart_olist_t' buffer, the num_entries
 * field can be set to any value >= 0 as long as enough storage has been provided
 * to accomodate a value > 0. If there are more entries to be returned than can fit
 * in the space specified by num_entries, then num_entries will be modified on
 * return to be the 2's compliment of the number of entries that would not fit.
 * Therefore, if you wish to first determine how many entries are required,
 * first pass in a mempart_olist_t buffer with num_entries set to zero. On
 * successful return, num_entries will be the 2's compliment (ie. negative) of
 * the num_entries that the caller should allocate space for (ie. num_entries = -10
 * indicates that the caller should allocate space for 10 entries).
 * If num_entries == 0 on successful return, all requested entries were returned.
 *
*/
#define	MEMPART_GET_ID			__DIOF(_DCMD_MEMPART, 1, mempart_id_t)
#define MEMPART_CFG_CHG			__DIOT(_DCMD_MEMPART, 2, mempart_cfgchg_t)

#define MEMPART_EVT_REG			__DIOT(_DCMD_MEMPART, 3, mempart_evtreg_t)
#define MEMPART_CNTR_RESET		__DIOF(_DCMD_MEMPART, 4, memsize_t)
#define MEMPART_GET_INFO		__DIOTF(_DCMD_MEMPART, 5, mempart_info_t)
#define MEMPART_GET_ASSOC_PLIST	__DIOTF(_DCMD_MEMPART, 6, mempart_plist_t)
#define MEMPART_GET_ASSOC_OLIST	__DIOTF(_DCMD_MEMPART, 7, mempart_olist_t)


#endif	/* _MEMPART_H_ */
