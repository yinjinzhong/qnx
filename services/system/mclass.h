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

#ifndef _MCLASS_H_
#define _MCLASS_H_

// FIX ME #include <sys/memclass.h>
#include "kernel/memclass.h"


#ifndef EXTERN
	#define EXTERN	extern
#endif

/*
 * allocator_accessfncs_t
 * 
 * This type defines the interface to the allocator for a memory class. By
 * utilizing a 'memclass_info_t *', the same allocator can be used for multiple
 * memory classes
*/
typedef struct allocator_accessfncs_s
{
	memsize_t				(*reserve)(memsize_t s, memclass_info_t *i);
	memsize_t				(*unreserve)(memsize_t s, memclass_info_t *i);
	memclass_sizeinfo_t *	(*size_info)(memclass_sizeinfo_t *si, memclass_info_t *i);
	void					(*resv_adjust)(memsize_t adjust, int sign, memclass_info_t *i);
} allocator_accessfncs_t;

/*
 * memclass_t
 * 
 * This is the internal memory class structure. It is created when a memory class
 * is added to the system (typically through the resource manager).
 * 
 * This type is used in combination with a 'name' to form an entry in the memory
 * class list.
*/
#define MEMCLASS_SIGNATURE		(~0x13991187)
typedef struct memclass_s
{
	_Uint32t		signature;		// used to help ensure not looking at a bogus memclass_t

	memclass_info_t		info;		// (public) accounting and configuration data
	allocator_accessfncs_t	allocator;

	void 			*rmgr_attr_p;	// back pointer to resource manager structure
} memclass_t;


/*
 * memclass_entry_t
 * 
 * A 'memclass_entry_t' is created for each of the memory classes which are
 * added to the system. The entries are added/removed to/from as physical memory
 * classes are added/removed to/from the system (note that adding a physical
 * chunk of memory does not "necessarily" add a new memory class unless that new
 * chunk of physical memory is desired to be partitioned independently. It can
 * otherwise simply be added to the pool of other chunks of physical memory
 * belonging to an existing memory class).
 * 
 * The current implementation requires that the name specified when adding a
 * memory class correspond to a syspage memory class name. This is because
 * what is otherwise currently known to the system as separate classes of
 * memory resides in the system page. Without a lot of rewrite is was decided
 * that this nehaviour would be enforced. The memory classes are therefore
 * added in startup and made available to be partitioned by add the class using
 * the syspage name
 *
*/
typedef struct memclass_entry_s
{
	kerproc_lock_t	lock;
	memclass_t		data;
	char			name[NAME_MAX+1];	// the syspage name by which the meory is known
} memclass_entry_t;

struct mempartmgr_attr_s;
extern void (*memclass_event)(struct mempartmgr_attr_s *attr, memclass_evttype_t evtype,
						memclass_sizeinfo_t *cur, memclass_sizeinfo_t *prev);


/*==============================================================================
 * 
 * 				public interfaces for memory class management
 * 
*/

/*
 * MEMCLASS_RUSED_ADJ_UP
 * MEMCLASS_RUSED_ADJ_DN
 * 
 * Adjust the amount of memory accounted as reserved by the allocator for the
 * memory class for which <mp> is a partition of. The reserved memory in use is
 * either increased (MEMCLASS_RUSED_ADJ_UP) or descreased (MEMCLASS_RUSED_ADJ_DN).
 * The adjustment does not affect the amount of memory configured as reserved
 * but rather the amount of reserved memory that is currently accounted as in use.
 * 
 * The result (amount reserved + amount unreserved) will remain constant. That is,
 * an increase in the reserved in use will result in a equal descrease in the
 * unreserved in use and vice versa.
 * 
 * Returns: nothing
*/
#define MEMCLASS_RUSED_ADJ_UP(p, s) \
		do { \
			if ((p)->memclass->data.allocator.resv_adjust != NULL) \
				((p)->memclass->data.allocator.resv_adjust)((s), +1, &((p)->memclass->data.info)); \
		} while(0)

#define MEMCLASS_RUSED_ADJ_DN(p, s) \
		do { \
			if ((p)->memclass->data.allocator.resv_adjust != NULL) \
				((p)->memclass->data.allocator.resv_adjust)((s), -1, &((p)->memclass->data.info)); \
		} while(0)


/*
 * MEMCLASSMGR_EVENT
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
#define MEMCLASSMGR_EVENT(mc, etype, c, p)	{}
#else	/* MEMPART_NO_EVENTS */
#define MEMCLASSMGR_EVENT(mc, etype, c, p) \
		do { \
			if ((memclass_event != NULL) && ((mc) != NULL)) \
				memclass_event((mc)->rmgr_attr_p, (etype) , (c), (p)); \
		} while(0)
#endif	/* MEMPART_NO_EVENTS */
		
#endif	/* _MCLASS_H_ */

/* __SRCVERSION("$IQ: memclass.h,v 1.91 $"); */
