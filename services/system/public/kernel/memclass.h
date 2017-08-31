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


#ifndef _MEMCLASS_H_
#define _MEMCLASS_H_

#ifndef __PLATFORM_H_INCLUDED
#include <sys/platform.h>
#endif

#include <stdint.h>
#include <limits.h>
#include <signal.h>
#include <devctl.h>

#if defined(__GNUC__)
/*
 * these array element and structure field "safe" initializations are not
 * supported by the WATCOM compiler.
*/
	#define ARRAY_EL(e)		[(e)] =
	#define STRUCT_FLD(e)	.e =
#else
	#define ARRAY_EL(e)
	#define STRUCT_FLD(e)
#endif	/* __GNUC__ */

/*
 * apm_bool_t
 * 
 * The apm_bool_t type is a logical type with the following additional properties
 * 
 * 		- its size is specified
 * 		- it has "write once" behaviour
 * 
 * The write once behaviour is defined by the variable of this type. For example,
 * the variable may be defined such that once set TRUE, it cannot be changed to
 * FALSE or vice versa.
 * This type is always represented by the value bool_t_FALSE or bool_t_TRUE.
 * See mempart.h for specific examples of where this type is used.
*/
typedef _Uint32t	apm_bool_t;
typedef enum {bool_t_FALSE = 0, bool_t_TRUE = !bool_t_FALSE} apm_bool_t_val;

/*
 * memsize_t
 * 
 * This type is used to represent all size information for the memory class
 * and memory partitioning systems.
 * The API defines this type to be 64 bits regardless of the processor on which
 * code may be executing. Internally, if the processor does not support 64 bit
 * types, and any values exchanged between an application and the OS cannot be
 * represented, a EOVERFLOW error will be returned   
*/
#ifndef _MEMSIZE_T_
#define _MEMSIZE_T_		_Uint64t
typedef _MEMSIZE_T_		memsize_t;
#endif

/*
 * memsize_t_INFINITY
 * 
 * When creating a partition, the caller may wish to leave the maximum value
 * unspecified so that any limits will be governed by partitions higher up in
 * the hierarchy. This value should be used to represent such a choice.
*/
#define memsize_t_INFINITY	((memsize_t)ULLONG_MAX)

/*
 * memclass_id_t
 * 
 * Memory class identifier
 * This identifier is used to specify the class of memory for which the
 * configuration attributes and/or information sizes refer to.
*/
typedef _Uint32t	memclass_id_t;
#define memclass_id_t_INVALID	((memclass_id_t)0)

/*
 * memclass_limits_t
 * 
 * This structure defines specific limits as related to the memory class and its
 * associated allocator. The values are considered read-only and are provided
 * to allow an application to make optimal decisions when interacting with the
 * allocator for the memory class.
 * 
 * For example, the allocation sub-structure indicates the smallest and largest
 * allocatable unit supported by the allocator for the class. 
*/
typedef struct memclass_limits_s
{
	struct {
		struct {
			_MEMSIZE_T_		min;		// minimum size of an allocation unit
			_MEMSIZE_T_		max;		// maximum size of an allocation unit
		} size;
	} alloc;

} memclass_limits_t;

/*
 * memclass_attr_t
 * 
 * This structure defines the attributes of a memory class
*/
typedef struct memclass_attr_s
{
	_MEMSIZE_T_			size;		// the amount of available memory
	memclass_limits_t	limits;		// limits as defined above
} memclass_attr_t;
/*
 * MEMCLASS_DEFAULT_ATTR
 * 
 * Default static memory class attribute initializer
*/
#define MEMCLASS_DEFAULT_ATTR \
		{ \
			STRUCT_FLD(size) 0, \
			STRUCT_FLD(limits) \
				{STRUCT_FLD(alloc) \
					{STRUCT_FLD(size) \
						{STRUCT_FLD(min) 0, STRUCT_FLD(max) 0}}}, \
		}

/*
 * memclass_sizeinfo_t
 * 
 * This structure defines the retrievable size information provided by the
 * allocator for the memory class. Memory class size information consists of
 * the amount of reserved memory (memory that has been set aside for partitions
 * created with a non-zero minimum value) and unreserved memory (memory which
 * is freely available to all partitions of the memory class in a discretionary
 * fashion.
 * The sum of all fields will always be equivalent to the size attribute
 * defined in memclass_attr_t. 
*/
typedef struct memclass_sizeinfo_s
{
	struct {
		_MEMSIZE_T_	free;
		_MEMSIZE_T_	used;
	} reserved;
	struct {
		_MEMSIZE_T_	free;
		_MEMSIZE_T_	used;
	} unreserved;
} memclass_sizeinfo_t;

/*
 * memclass_info_t
 * 
 * This structure defines the information that may be retrieved from the
 * allocator for a memory class.
*/
typedef struct memclass_info_s
{
	memclass_id_t		id;
	_Uint32t			reserved;
	memclass_attr_t		attr;
	memclass_sizeinfo_t	size;
} memclass_info_t;

/*
 * memclass_evtflags_t
 * 
 * This structure defines the attributes of events that may be registered on
 * a memory class. Events may be global (they apply to all defined memory class
 * events) or specific to an event type
*/
typedef _Uint32t	memclass_evtflags_t;
typedef enum
{
	memclass_evtflags_DISARM = 0x0,		// Default action
										// the event, once delivered is disarmed
										// and must be re-registered in order to
										// be received again.
	memclass_evtflags_REARM = 0x10,		// the event, once delivered will remain in effect

} memclass_evtflags_t_val;

/*
 * memclass_evttype_t
 * 
 * This structure defines the set of events that can be registered with a
 * memory class and subsequently received by the caller
*/
typedef _Int32t	memclass_evttype_t;
typedef enum
{
memclass_evttype_t_INVALID = -1,

	//	free and used crossings (total)
	memclass_evttype_t_THRESHOLD_CROSS_TF_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER,
	memclass_evttype_t_THRESHOLD_CROSS_TU_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER,
	//	free crossings (reserved and unreserved)
	memclass_evttype_t_THRESHOLD_CROSS_RF_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER,
	memclass_evttype_t_THRESHOLD_CROSS_UF_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER,
	//	used crossings	(reserved and unreserved)
	memclass_evttype_t_THRESHOLD_CROSS_RU_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER,
	memclass_evttype_t_THRESHOLD_CROSS_UU_OVER,
	memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER,
	
	//	free and used deltas (total)
	memclass_evttype_t_DELTA_TF_INCR,
	memclass_evttype_t_DELTA_TF_DECR,
	memclass_evttype_t_DELTA_TU_INCR,
	memclass_evttype_t_DELTA_TU_DECR,
	//	free deltas (reserved and unreserved)
	memclass_evttype_t_DELTA_RF_INCR,
	memclass_evttype_t_DELTA_RF_DECR,
	memclass_evttype_t_DELTA_UF_INCR,
	memclass_evttype_t_DELTA_UF_DECR,
	//	used deltas (reserved and unreserved)
	memclass_evttype_t_DELTA_RU_INCR,
	memclass_evttype_t_DELTA_RU_DECR,
	memclass_evttype_t_DELTA_UU_INCR,
	memclass_evttype_t_DELTA_UU_DECR,
	
	// memory class partition creation/destruction
	memclass_evttype_t_PARTITION_CREATE,
	memclass_evttype_t_PARTITION_DESTROY,

// (memclass_evttype_t_last - memclass_evttype_t_first) is always the number of event types
memclass_evttype_t_last,
memclass_evttype_t_first = memclass_evttype_t_THRESHOLD_CROSS_TF_OVER,
} memclass_evttype_t_val;

/* convenience macros */
#define NUM_MEMCLASS_EVTTYPES			(memclass_evttype_t_last - memclass_evttype_t_first)
#define MEMCLASS_EVTYPE_IDX(etype)		((etype) - memclass_evttype_t_first)

/*
 * memclass_evtparam_t
 * 
 * This structure defines the parameters of an event that may be registered
 * with a memory class.
 * For each event type, an optional value may be provided which acts as a filter
 * to the event delivery mechanism. For example, a threshold crossing event
 * requires an absolute value (between 0 and the size attribute of the memory
 * class) through which the specified memory class size value (memclass_sizeinfo_t)
 * must cross before the event is delivered.
*/
typedef struct memclass_evtparam_s
{
	memclass_evttype_t	type;	// event type
	_Uint32t			reserved[3];
	union {
		// event types
		//		memclass_evttype_t_THRESHOLD_CROSS_TF_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER
		//		memclass_evttype_t_THRESHOLD_CROSS_TU_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER
		//	free crossings (reserved and unreserved)
		//		memclass_evttype_t_THRESHOLD_CROSS_RF_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER
		//		memclass_evttype_t_THRESHOLD_CROSS_UF_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER
		//	used crossings	(reserved and unreserved)
		//		memclass_evttype_t_THRESHOLD_CROSS_RU_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER
		//		memclass_evttype_t_THRESHOLD_CROSS_UU_OVER
		//		memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER
		//
		// a notification event is generated when the memory class crosses the
		// specified threshold in the specified direction. In order to provide
		// the most flexibility, a number of events are specified which specifically
		// allow events to be generated (in either direction) on either
		//		total free (reserved free + unreserved free)
		//		total used (reserved used + unreserved used)
		//		reserved free
		//		reserved used
		//		unreserved free
		//		unreserved used
		// The new memory class value (as it relates to the specific event) will
		// be provided
// FIX ME - how will we deliver a _MEMSIZE_T == 64 bits event 
		_MEMSIZE_T_		threshold_cross;

		// event types
		//	free and used deltas (total)
		//		memclass_evttype_t_DELTA_TF_INCR
		//		memclass_evttype_t_DELTA_TF_DECR
		//		memclass_evttype_t_DELTA_TU_INCR
		//		memclass_evttype_t_DELTA_TU_DECR
		//	free deltas (reserved and unreserved)
		//		memclass_evttype_t_DELTA_RF_INCR
		//		memclass_evttype_t_DELTA_RF_DECR
		//		memclass_evttype_t_DELTA_UF_INCR
		//		memclass_evttype_t_DELTA_UF_DECR
		//	used deltas (reserved and unreserved)
		//		memclass_evttype_t_DELTA_RU_INCR
		//		memclass_evttype_t_DELTA_RU_DECR
		//		memclass_evttype_t_DELTA_UU_INCR
		//		memclass_evttype_t_DELTA_UU_DECR
		//
		// a notification event is generated when the memory class changes by an
		// amount >= the specified delta from the time the last event was
		// received or when it is first registered. In order to provide the most
		// flexibility, a number of events are defined which specifically allow
		// events to be generated (in either direction) on either ...
		//		total free (reserved free + unreserved free)
		//		total used (reserved used + unreserved used)
		//		reserved free
		//		reserved used
		//		unreserved free
		//		unreserved used
		// The new memory class value (as it relates to the specific event) will
		// be provided
// FIX ME - how will we deliver a _MEMSIZE_T == 64 bits event 
		_MEMSIZE_T_		delta;

		// event types
		//		memclass_evttype_t_PARTITION_CREATE
		//		memclass_evttype_t_PARTITION_DESTROY
		//
		// A notification event is generated when a partition of the memory
		// class on which the caller is registering is either created or
		// destroyed.
		// The partition identifier for the created partition is provided.
		//
		// NOTE that any partition created or destroyed for the memory class
		// will cause an event. These events ARE NOT hierarchy sensitive as with
		// partition creation/destruction events.
		void *	mpid;

	} val;
} memclass_evtparam_t;


/*
 * memclass_evtreg_t
 * 
 * The structure defines the event registration structure. This structure is
 * a variable size structure which is used by applications to register to
 * receive 1 or more events of interest from the allocator for a memory class.
 * The same event may be registered multiple times with different parameter
 * values.
 * When registering an event, the caller is required to provide properly
 * initialized memclass_evtparam_t, sigevent and memclass_evtflags_t values.
 * 
 * The macros FIX ME can be used to aid in the initialization of the
 * memclass_evtreg_t structure. 
*/
typedef struct memclass_evtreg_s
{
	_Uint32t	num_entries;	// number of memclass_evtparam_t structures which follow
	_Uint32t	reserved[3];
	struct mc_evt_info_s {
		memclass_evtparam_t		info;	// the event to register for
		struct sigevent			sig;	// sigevent
		memclass_evtflags_t		flags;	// event specific flags
		_Uint32t				reserved;
	} evt[1];
} memclass_evtreg_t;
/*
 * MEMCLASS_EVTREG_T_SIZE
 * 
 * convenience macro to calculate the size (in bytes) of the resulting
 * 'memclass_evtreg_t' structure given 'n' where 'n' (typically) represents the
 * value of the 'memclass_evtreg_t.num_entries' field.
 * 
 * Note:
 * 		'n' must be >= 0
 * 		implementation is coded to ensure the proper result even for 'n' == 0 
*/
#define MEMCLASS_EVTREG_T_SIZE(n) \
		((sizeof(memclass_evtreg_t) - sizeof(struct mc_evt_info_s)) + \
		((n) * sizeof(struct mc_evt_info_s)))


/*
 * Memory Class devctl()'s (_DCMD_MEMCLASS)
 * 
 * MEMCLASS_GET_INFO
 * 	retrieve the memory class information
 * 	On success, EOK will be returned by devctl() and the caller provided
 * 	'memclass_info_t' structure will contain the applicable information.
 * 	If any error occurs, the caller provided memclass_info_t structure should
 * 	be considered to contain garbage.
 * 	The following errnos and their interpretation may be returned
 * 		ENOSYS - the memory partitioning module is not installed
 * 		EACESS - the caller does not have read permission on the memory class
 * 		EOVERFLOW - the caller has provided a buffer which is inconsistent with
 * 					the specified number of events being registered
 * 		EBADF - the MEMCLASS_GET_INFO devctl() has been issued on a name which
 * 				does not represent a memory class
 * 		ENOENT - there is no memory class information for the open()'d name
 * 		EIO - internal I/O error. There is no size information available or
 * 				the request to retrieve the information failed.
 * 
 * MEMCLASS_EVT_REG
 *	register to receive one or more of the defined memory class events
 * 	On success, EOK will be returned by devctl() and the caller can assume that
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
 * 		EBADF - the MEMCLASS_EVT_REG devctl() has been issued on a name which
 * 				does not represent a memory class
 * 		EINVAL - at least 1 of the events to be registered contained invalid
 * 				 data
 * 		ENOMEM - the memory necessary to register the events could not be
 * 				allocated
 * 
 * 
*/
#define MEMCLASS_GET_INFO		__DIOF(_DCMD_MEMCLASS, 1, memclass_info_t)
#define MEMCLASS_EVT_REG		__DIOT(_DCMD_MEMCLASS, 2, memclass_evtreg_t)

#endif	/* _MEMCLASS_H_ */
