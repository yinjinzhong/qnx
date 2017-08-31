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
 * mm_class.c
 * 
 * This file contains code pertaining to the maintenance and manipulation of
 * memory classes within the system. It is theorectically separate from memory
 * partitioning but is required by the memory partitioning implementation hence
 * why it has its own file. It is bundles with the memory partitioning module
 * for now
 * 
*/

#include "vmm.h"

typedef struct mclass_node_s
{
	struct mclass_node_s *next;
	memclass_entry_t	 entry;
} memclass_node_t;

/*
 * memclass_list
 * 
 * Pointer to the memory class list
 * 
 * A memory class must be added to the 'memclass_list' in order for it to be
 * partitioned. Memory classes can be added but (currently) not removed.
*/
static memclass_node_t *	memclass_list = NULL;
static pthread_mutex_t		memclass_list_lock = PTHREAD_MUTEX_INITIALIZER;
 
static memclass_sizeinfo_t *size_0(memclass_sizeinfo_t *s, memclass_info_t *i)
{
	memset(s, 0, sizeof(*s));
	return s;
}
static allocator_accessfncs_t  no_op = {NULL, NULL, size_0};
static memclass_node_t *_memclass_find(const char *name, memclass_id_t id);

/*
 * MUTEX_INIT
 * MUTEX_DESTROY
 * MUTEX_LOCK
 * MUTEX_UNLOCK
*/
#define MUTEX_INIT(p, m) \
		do { \
			/* use a recursive mutex */ \
			const pthread_mutex_t  tmp = PTHREAD_RMUTEX_INITIALIZER; \
			(m)->mutex = tmp; \
			(m)->spin.value = 0; \
		} while(0)

#define MUTEX_DESTROY(m)		pthread_mutex_destroy((m));

#define MUTEX_LOCK(m) \
		do { \
			int r = EOK; \
/*			KASSERT(!KerextAmInKernel()); */\
			if (!KerextAmInKernel()) \
				r = pthread_mutex_lock((m)); \
			KASSERT(r == EOK); \
		} while(0)

#define MUTEX_UNLOCK(m) \
		do { \
			int r = EOK; \
/*			KASSERT(!KerextAmInKernel()); */\
			if (!KerextAmInKernel()) \
				r = pthread_mutex_unlock((m)); \
			KASSERT(r == EOK); \
		} while(0)

/*
 * memclass_event
 * 
 * This function pointer will be initialized when the memory partitioning resource
 * manager module is installed.
*/
struct mempartmgr_attr_s;
void (*memclass_event)(struct mempartmgr_attr_s *attr, memclass_evttype_t evtype,
						memclass_sizeinfo_t *cur, memclass_sizeinfo_t *prev) = NULL;

/*******************************************************************************
 * memclass_find
 * 
 * This function will locate the memory class specified either by name or
 * memory class identifier and return a pointer to the memory class entry.
 * One of either the class name or identifier must be specified.
 * This function allows a specific entry to be obtained as follows. 
 * 	if <name> != NULL, it will be used to search for a match and <id> will be
 * 	ignored
 * 	if <name == NULL> and <id> != NULL, <id> will be used to search for a match
 * 	otherwise, NULL will be returned
 * 
 * Returns: (memclass_entry_t *) or NULL if not found or invalid arguments 
 * 
*/
memclass_entry_t *memclass_find(const char *name, memclass_id_t id)
{
	memclass_node_t *listentry;

	if (memclass_list == NULL)
		return NULL;

	if ((name == NULL) && (id == NULL))
		return NULL;
	
	if ((listentry = _memclass_find(name, id)) == NULL)
		return NULL;

	return &listentry->entry;
}


/*******************************************************************************
 * memclass_add
 * 
 * add the memory class identified by <memclass_name> to the list of known
 * memory types and return a memory class identifier for the new memory. The
 * attributes of the memory class are specified in <attr> and a set of allocator
 * functions are specified in <f>
 * 
 * If unsuccessful, NULL will be returned.
 * 
 * If the memory class is already known, then the 'memclass_id_t' for the
 * previously added memory type is returned.
 * 
 * IMPORTANT
 * The memory for the class structures always comes from the internal heap
 * since it may need to live beyond the existence of the creating process
*/
memclass_id_t memclass_add(const char *memclass_name, memclass_attr_t *attr,
									allocator_accessfncs_t *f)
{
	memclass_node_t *mclass_node;
	memclass_entry_t  *mclass_entry;

	/* first make sure that the memory class has not already been added */
	if ((mclass_entry = memclass_find(memclass_name, NULL)) != NULL)
	{
#ifndef NDEBUG
		kprintf("memory class '%s' with class id 0x%x already present\n",
				mclass_entry->name, mclass_entry->data.info.id);
#endif	/* NDEBUG */
	}
	/* not found, add a new entry */
	else if ((mclass_node = calloc(1, sizeof(*mclass_node))) == NULL)
		return memclass_id_t_INVALID;
	else
	{
		mclass_entry = &mclass_node->entry;

		STRLCPY(mclass_entry->name, memclass_name, sizeof(mclass_entry->name));
		/* for now, use the address of the memory type name as the class id */
		mclass_entry->data.info.id = (memclass_id_t)mclass_entry->name;

		MUTEX_INIT(NULL, &mclass_entry->lock);

		if (attr)
			mclass_entry->data.info.attr = *attr;
		else
		{
			memclass_attr_t  a = MEMCLASS_DEFAULT_ATTR;
			mclass_entry->data.info.attr = a;
		}
		if (f)
			mclass_entry->data.allocator = *f;
		else
			mclass_entry->data.allocator = no_op;

		memset(&mclass_entry->data.info.size, 0, sizeof(mclass_entry->data.info.size));
		mclass_entry->data.info.size.unreserved.free = mclass_entry->data.info.attr.size;

#ifndef NDEBUG
		kprintf("added memory class '%s' with class id 0x%x, sz = %P\n",
					mclass_entry->name, mclass_entry->data.info.id,
					(paddr_t)mclass_entry->data.info.size.unreserved.free);
#endif	/* NDEBUG */

		/* add the entry to the list */
		MUTEX_LOCK(&memclass_list_lock);
		LINK1_END(memclass_list, mclass_node, memclass_node_t);
		MUTEX_UNLOCK(&memclass_list_lock);
	}
	return mclass_entry->data.info.id;
}


/*******************************************************************************
 * memclass_del
 * 
 *
 * This function will remove the memory class specified by either name or
 * memclass identifier.
 * 
 * Returns: EOK on success, otherwise an errno
 * 
*/
int memclass_delete(const char *name, memclass_id_t id)
{
	memclass_node_t  *node;

	if ((name == NULL) && (id == NULL))
		return EINVAL;
	
	if ((node = _memclass_find(name, id)) == NULL)
		return ENOENT;
	
	MUTEX_LOCK(&memclass_list_lock);
	LINK1_REM(memclass_list, node, memclass_node_t);
	MUTEX_UNLOCK(&memclass_list_lock);

	MUTEX_DESTROY(&node->entry.lock.mutex);
	free(node);

	return EOK;
}

/*******************************************************************************
 * memclass_info
 * 
 * 
*/
memclass_info_t *memclass_info(memclass_id_t id, memclass_info_t *info_buf)
{
	memclass_entry_t *e = memclass_find(NULL, id);

	if (e == NULL) return NULL;
	KASSERT(info_buf != NULL);
	e->data.allocator.size_info(&e->data.info.size, &e->data.info);
	*info_buf = e->data.info;
	return info_buf;
}

/*
 * ===========================================================================
 * 
 * 						Memory Class support routines
 *  
 * ===========================================================================
*/

/*
 * _memclass_find
 * 
 * This function will locate the memory class entry specified by either name or
 * memclass identifier and return a pointer to the memory class node.
 * 
 * If an entry is not found, NULL will be returned
 * The caller should provide either <name> or <id>. If both are provided,
 * <name> will be preferentially used.
*/
static memclass_node_t *_memclass_find(const char *name, memclass_id_t id)
{
	memclass_node_t *listentry = NULL;

	if ((memclass_list != NULL) && ((name != NULL) || (id != NULL)))
	{
		/* search */
		MUTEX_LOCK(&memclass_list_lock);

		listentry = memclass_list;
		while (listentry != NULL)
		{
			if ((name != NULL) && (strcmp(name, listentry->entry.name) == 0)) break;
			else if ((id != NULL) && (id == listentry->entry.data.info.id)) break;
			else listentry = LINK1_NEXT(listentry);
		}
		MUTEX_UNLOCK(&memclass_list_lock);
	}
	return listentry;
}

__SRCVERSION("$IQ: mm_class.c,v 1.7 $");
