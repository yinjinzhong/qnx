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

#include "externs.h"
#include "pathmgr_node.h"
#include "pathmgr_object.h"
#include "pathmgr_proto.h"
#include "apm.h"
#include <sys/pathmgr.h>


/*
 * Mark flags for quicker searching later
 */
static void 
object_set_flags(NODE *nop, OBJECT *obp) {
	switch(obp->hdr.type) {
	case OBJECT_SERVER:
		if(!(obp->hdr.flags & PATHMGR_FLAG_DIR)) {
			nop->flags |= NODE_FLAG_NOCHILD;
		}
		break;
	case OBJECT_PROC_SYMLINK:
		nop->flags |= NODE_FLAG_NONSERVER;
		break;
	default:
		nop->flags |= NODE_FLAG_NONSERVER | NODE_FLAG_NOCHILD;
		break;
	}
}
	

/*
 * Allocate node entry if necessary, then link object to it.
 * The first objects marked BEFORE of AFTER will win out over
 * later objects marked the same way. If no position reqested
 * the object will be before other objects not marked as BEFORE.
 * The child_count field will be incremented on all ancestors to
 * allow the scaning routine to stop early when checking for
 * objects.
 */
OBJECT *
pathmgr_object_attach(NODE *nop, const char *path, int type, unsigned flags, void *data) {
	OBJECT					*obp, *p, **pp;

	/* Create the node in the pathname space */
	if(!(nop = pathmgr_node_lookup(nop, path, PATHMGR_LOOKUP_ATTACH | PATHMGR_LOOKUP_CREATE, 0))) {
		return 0;
	}
	else
	{
		mempart_id_t mpart_id = mempart_getid(NULL, sys_memclass_id);
		if (nop->object != NULL)
		{
			KASSERT(nop->object->hdr.mpid != mempart_id_t_INVALID);
			mpart_id = nop->object->hdr.mpid;
		}		
		/* Allocate the object. Use pathmgr_prp's sysram memory class for the heap */
		if(!(obp = object_create(type, data, mpart_id))) {
		pathmgr_node_detach(nop);
		return 0;
	}
	}
#ifndef NDEBUG
if(obp->hdr.node || obp->hdr.len < sizeof obp->hdr) crash();
#endif
	obp->hdr.flags = flags;

	/* Grab a mutex */
	pthread_mutex_lock(&pathmgr_mutex);

	/* Find requested location and link object to node */
	pp = &nop->object;

	if(obp->hdr.type != OBJECT_PROC_SYMLINK) {
		for(;(p = *pp); pp = &p->hdr.next) {
			/* PROC SYMLINKS come before everything */
			if(p->hdr.type == OBJECT_PROC_SYMLINK) {
				continue;
			}

			/* Skip after all object marked BEFORE */
			if(!(p->hdr.flags & PATHMGR_FLAG_BEFORE)) {

				/* If it wasn't requested to be AFTER link here */
				if(!(obp->hdr.flags & PATHMGR_FLAG_AFTER)) {
					break;
				}

				/* Always go before an object marked AFTER */
				if(p->hdr.flags & PATHMGR_FLAG_AFTER) {
					break;
				}
			}
		}
	}

	/* Link it to the node */
	obp->hdr.node = nop;
	obp->hdr.next = *pp;
	*pp = obp;

	/* Mark flags for quicker searching later */
	object_set_flags(nop, obp);

	/* Increment child_object count in all ancesters */
	while(nop->parent != nop && (nop = nop->parent)) {
		nop->child_objects++;
	}

	/* Free the mutex */
	pthread_mutex_unlock(&pathmgr_mutex);

	return obp;
}


/*
 * This will remove an object from its neighbouring objects, then
 * unlink from the node. The object must be freed by the calling
 * routine after the unlink.
 */
void 
pathmgr_object_detach(OBJECT *obp) {
	OBJECT						*p, **pp;
	NODE						*nop;

	/* Grab a mutex */
	pthread_mutex_lock(&pathmgr_mutex);

	nop = obp->hdr.node;

	/* Unset these flags, they will be reset in the loop below */
	nop->flags &= ~(NODE_FLAG_NONSERVER | NODE_FLAG_NOCHILD);

	/* Scan the objects to remove this one */
	for(pp = &nop->object; (p = *pp); pp = &p->hdr.next) {
		if(p == obp) {
			if(!(p = *pp = p->hdr.next)) {
				break;
			}
		}
		/* Mark flags for quicker searching later */
		object_set_flags(nop, p);
	}

	/* Decrement child_object count on all ancestors */
	while(nop->parent != nop && (nop = nop->parent)) {
#ifndef NDEBUG
if(nop->child_objects == 0) crash();
#endif
		nop->child_objects--;
	}

	/* Free the mutex */
	pthread_mutex_unlock(&pathmgr_mutex);

	/* Now release the node */
	pathmgr_node_detach(obp->hdr.node);

	/* Detach the object */
	object_done(obp);
}


static int
dummy_done(OBJECT *o) {
	return 1;
}

static size_t
dummy_name(OBJECT *o, size_t max, char *dst) {

	if (dst)  dst[0] = '\0';
	return 0;
}

static int
dummy_devctl(resmgr_context_t *ctp, io_devctl_t *msg, OBJECT *obp) {
	return ENOSYS;
}

extern int		server_create(OBJECT *, void *);
extern int		symlink_create(OBJECT *, void *);
extern int		anmem_create(OBJECT *, void *);
extern int		anmem_done(OBJECT *);
extern size_t	anmem_name(OBJECT *, size_t, char *);
extern int		shmem_create(OBJECT *, void *);
extern int		shmem_done(OBJECT *);
extern size_t	shmem_name(OBJECT *, size_t, char *);
extern int		shmem_devctl(resmgr_context_t *ctp, io_devctl_t *msg, OBJECT *obp);
extern int		fdmem_create(OBJECT *, void *);
extern int		fdmem_done(OBJECT *);
extern size_t	fdmem_name(OBJECT *, size_t, char *);
extern int		tymem_create(OBJECT *, void *);
extern int		tymem_done(OBJECT *);

struct object_info {
	unsigned	len;
	int			(*create)(OBJECT *, void *);
	int			(*done)(OBJECT *);
	size_t		(*name)(OBJECT *, size_t, char *);
	int			(*devctl)(resmgr_context_t *ctp, io_devctl_t *msg, OBJECT *obp);
};

struct object_info obj_info[] = {
	//DEAD
	{ 0 },		
	//SERVER
	{sizeof(struct server_object),	server_create, dummy_done, dummy_name, dummy_devctl},	
	//NAME
	{sizeof(struct server_object),	server_create, dummy_done, dummy_name, dummy_devctl},
	//PROC_SYMLINK
	{ 0, symlink_create, dummy_done, dummy_name, dummy_devctl},
	//MEM_ANON
	{sizeof(struct mm_object_anon),	anmem_create, anmem_done, anmem_name, dummy_devctl}, 
	//MEM_SHARED
	{sizeof(struct mm_object_shared),shmem_create, shmem_done, shmem_name, shmem_devctl}, 
	//MEM_FD
	{sizeof(struct mm_object_fd),	fdmem_create, fdmem_done, fdmem_name, dummy_devctl},
	//MEM_TYPED
	{sizeof(struct mm_object_typed),tymem_create, tymem_done, dummy_name, dummy_devctl},
};


OBJECT *
object_create(int type, void *extra, mempart_id_t mpart_id) {
	struct object_info *info;
	unsigned			len;
	OBJECT				*obp;

	info = &obj_info[type];
	len = info->len;
	if(len == 0) {
		len = info->create(NULL, extra);
	}
	obp = _scalloc(len);
	if(obp != NULL) {
		obp->hdr.type = type;
		obp->hdr.len  = len;
		obp->hdr.mpid = mempart_id_t_INVALID;
		obp->hdr.mpart_disassociate = NULL;
		
		if ((MEMPART_OBJ_ASSOCIATE(obp, mpart_id) != EOK) ||
			(info->create(obp, extra) != EOK))
		{
			if (obp->hdr.mpart_disassociate != NULL)
				obp->hdr.mpart_disassociate(obp);
			_sfree(obp, obp->hdr.len);
			obp = NULL;
		}
		else {KASSERT(obp->hdr.mpid == mpart_id);}
	}
	return obp;
}


int
object_done(OBJECT *obp) {
	int		r;

	//Right now we can recursively invoke this code. It'd be nice
	//if there was something that could be set to stop the obj_info[].done() 
	//routine being called again in that case. An obp->hdr.flags bit
	//would be nice, but those bit definitions are publicly exposed.

	r = obj_info[obp->hdr.type].done(obp);
	if(r) {
		/*
		 * disassociate from the partition this object was created in
		 * Implementation note: excerpt from Design Details Document
		 * "It was not possible to use a method similar
		 * to that used elsewhere, namely MEMPART_DISASSOCIATE() since that
		 * would have required inclusion of the mm_mempart_internal.h file
		 * creating a linkage between pathmgr and memmgr that did not otherwise
		 * exist. Although functionally identical, the implementation instead
		 * includes a function pointer within the OBJECT.hdr field into which
		 * is stored the disassociate function when the object is first
		 * associated. The disassociate function will remove itself from the
		 * OBJECT.hdr, thereby preventing being called more than once"
		*/
		if (obp->hdr.mpart_disassociate != NULL)
			obp->hdr.mpart_disassociate(obp);

		_sfree(obp, obp->hdr.len);
	}
	return r;
}


size_t
object_name(OBJECT *obp, size_t max, char *name) {
	return obj_info[obp->hdr.type].name(obp, max, name);
}


int
object_devctl(resmgr_context_t *ctp, io_devctl_t *msg, OBJECT *obp) {
	return obj_info[obp->hdr.type].devctl(ctp, msg, obp);
}

__SRCVERSION("pathmgr_object.c $Rev: 156323 $");
