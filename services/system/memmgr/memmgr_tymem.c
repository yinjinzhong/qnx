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
#include "vmm.h"

#include <sys/rsrcdbmgr.h>
#include <sys/rsrcdbmsg.h>


//static OBJECT				*ty_head = NULL;
static pthread_mutex_t		rl_mux = PTHREAD_MUTEX_INITIALIZER;

static int
match_name(struct asinfo_entry *base_as, struct asinfo_entry *as, 
				const char *name, unsigned name_len) {
	const char				*str = SYSPAGE_ENTRY(strings)->data;
	const char				*curr;
	const char				*p;
	unsigned				len;

	p = &name[name_len];
	for( ;; ) {
	    if(p == name) break;
	    if(p[-1] == '/') break;
	    --p;
	}
	curr = &str[as->name];
	len = name_len - (p - name);

	//Check that this piece matches
	if(strlen(curr) != len) return 0;
	if(memcmp(curr, p, len) != 0) return 0;

	//See if that was the whole given name
	if(p == name) return 1;

	// First character in given name is a "/" => must be top level asinfo.
	if((p == &name[1]) && (as->owner == AS_NULL_OFF)) return 1;

	//More to given name, no owner for current asinfo
	if(as->owner == AS_NULL_OFF) return 0;

	//See if prefix also matches.
	return match_name(base_as, 
				(struct asinfo_entry *)((uint8_t *)base_as + as->owner), 
				name, (name_len - len) - 1);
}


static void
kill_restrict(struct pa_restrict *par) {
	struct pa_restrict	*next;

	while(par != NULL) {
		next = par->next;
		_sfree(par, sizeof(*par));
		par = next;
	}
}


static int
restrict_create(const char *name, unsigned name_len, OBJECT *obp, memclass_attr_t *attr) {
	struct asinfo_entry		*base_as = SYSPAGE_ENTRY(asinfo);
	struct asinfo_entry		*as;
	struct asinfo_entry		*try_as;
	unsigned				num;
	struct pa_restrict		*par;
	struct pa_restrict		*new;
	struct pa_restrict		**owner;
	struct pa_restrict		*try;
	int						is_sysram;

	// We're hiding the asinfo pointer inside the checker field of the
	// restrict entry so we can remember both the priority & alloc_checker
	// field in this routine. It all gets fixed up at the end.
	attr->size = 0;
	attr->limits.alloc.size.min = __PAGESIZE;			// FIX ME
	attr->limits.alloc.size.max = memsize_t_INFINITY;	// FIX ME

	as = base_as;
	obp->mem.mm.restrict = par = NULL;
	num = _syspage_ptr->asinfo.entry_size / sizeof(*as);
	while(num != 0) {
		if(match_name(base_as, as, name, name_len)) {
			owner = &par;
			for( ;; ) {
			    try = *owner;
			    if(try == NULL) break;
			    try_as = (struct asinfo_entry *)try->checker;
			    if(try_as->priority < as->priority) break;
			    owner = &try->next;
			}
			new = _smalloc(sizeof(*new));
			if(new == NULL) {
				kill_restrict(par);
				return ENOMEM;
			}
			new->start = as->start;
			new->end = as->end;
			new->checker = (void *)as;
			new->next = *owner;
			*owner = new;
			attr->size += ((as->end - as->start) + 1);
		}
		++as;
		--num;
	}
	if(par == NULL) return ENOENT;

	is_sysram = 0;
	for(try = par; try != NULL; try = try->next) {
		try_as = (void *)try->checker;
		try->checker = try_as->alloc_checker;
		// If any piece of the restriction list overlaps with a
		// system ram entry, we're only going to allocate out of the
		// system ram piece. If a POSIX_TYPED_MEM_MAP_ALLOCATABLE request
		// is used for that region, it's the user's lookout.
		is_sysram |= pa_sysram_overlap(try);
	}
	//RUSH3: coalese overlapping/adjacent entries where possible
	if(!is_sysram) obp->mem.mm.flags |= MM_MEM_RDB;
	obp->mem.mm.restrict = par;
	return EOK;
}


int
tymem_create(OBJECT *obp, void *extra) {
	return shmem_create(obp, extra);
}


int
tymem_done(OBJECT *obp) {
//	OBJECT *p, **prev;

	/* following copied from shmem_done() */
	if(obp->mem.mm.flags & MM_MEM_INPROGRESS) return 0;
	if(OBJECT_SHM_REF_COUNT(obp) != 0) return 0;
	if(obp->mem.mm.refs != NULL) return 0;

	/* FIX ME - following required as per comments on per open() OBJECT creation */
	_sfree(obp->tymem.name, strlen(obp->tymem.name + 1));

	/* remove the OBJECT from the ty_head list */
/* ty_head no longer required
	for (prev = &ty_head; (p = *prev); prev = &p->hdr.next) {
		if (p == obp) {
			*prev = p->hdr.next;
			break;
		}
	}
*/
	return 1;
}

/*
 * tymem_sizeinfo
*/
static memclass_sizeinfo_t *
tymem_sizeinfo(memclass_sizeinfo_t *s, memclass_info_t *info) {
	KASSERT(s != NULL);

	*s = info->size;
	return s;
}
/*
 * tymem_reserve
*/
static memsize_t
tymem_reserve(memsize_t size, memclass_info_t *info) {

	if (size <= info->size.unreserved.free) {
		memclass_t *mclass;
		memclass_sizeinfo_t prev = info->size;

		info->size.unreserved.free -= size;
		info->size.reserved.free += size;

		mclass = (memclass_t *)((unsigned int)info - offsetof(memclass_t, info));
		MEMCLASSMGR_EVENT(mclass, memclass_evttype_t_DELTA_RF_INCR, &info->size, &prev);
		return size;
	}

	return 0;
}

static memsize_t
tymem_unreserve(memsize_t size, memclass_info_t *info) {

	if (size <= info->size.reserved.free) {
		memclass_t *mclass;
		memclass_sizeinfo_t prev = info->size;

		info->size.unreserved.free += size;
		info->size.reserved.free -= size;

		mclass = (memclass_t *)((unsigned int)info - offsetof(memclass_t, info));
		MEMCLASSMGR_EVENT(mclass, memclass_evttype_t_DELTA_RF_DECR, &info->size, &prev);
		return size;
	}

	return 0;
}

static void
tymem_resv_adjust(memsize_t size, int sign, memclass_info_t *info) {

	memclass_t *mclass;
	memclass_sizeinfo_t prev = info->size;
	memclass_evttype_t evt = memclass_evttype_t_INVALID;

	if (size == 0)
		return;
	else if (sign == +1) {
		/* transfer used unreserved to used reserved (while keeping totals constant) */
		info->size.unreserved.used -= size;
		info->size.reserved.used += size;

		/* ... and fix up free's to keep config constant */
		info->size.unreserved.free += size;
		info->size.reserved.free -= size;
		evt = memclass_evttype_t_DELTA_RU_DECR;
	}
	else if (sign == -1) {
		/* transfer used reserved to used unreserved (while keeping totals constant) */
		info->size.reserved.used -= size;
		info->size.unreserved.used += size;

		/* ... and fix up free's to keep config constant */
		info->size.reserved.free += size;
		info->size.unreserved.free -= size;
		evt = memclass_evttype_t_DELTA_RU_INCR;
	}
#ifndef NDEBUG
	else crash();
#endif	/* NDEBUG */

	mclass = (memclass_t *)((unsigned int)info - offsetof(memclass_t, info));
	MEMCLASSMGR_EVENT(mclass, evt, &info->size, &prev);
}

/*
 * tymem_register
 * 
 * this routine ties the memclass table to the resource database. Its main
 * purpose is to check for the existence of <name> and add it to the class table
 * with the startup defined attributes if it does exist.
 * 
 * On error, errno will be returned and the entry will not be added
 * 
 * Should be reworked so that there is one memory class repository
*/ 
int
tymem_register(const char *name) {

	static allocator_accessfncs_t  af =	{
		STRUCT_FLD(reserve) tymem_reserve,
		STRUCT_FLD(unreserve) tymem_unreserve,
		STRUCT_FLD(size_info) tymem_sizeinfo,
		STRUCT_FLD(resv_adjust) tymem_resv_adjust,
	};
	OBJECT  tmp_obj;
	memclass_attr_t  attr;
	int  r;

	if (((r = restrict_create(name, strlen(name), &tmp_obj, &attr)) != EOK) ||
		(memclass_add(name, &attr, &af) == NULL)) {
		r = (r == EOK) ? ENOENT : r;
	}

/*
 * FIX ME
 * Currently what I am doing is using restrict_create() with a temporary OBJECT
 * only to setup the 'attr' structure for the memclass_add() call since its knows
 * about sizes, etc. Then, I delete the restriction and the temporary OBJECT.
 * When an open() on the memory class is done, I recreate the restriction so that
 * can have an OBJECT per open() similar to a processes anonymous memory OBJECT.
 * As a minimum, I should eliminate the restrict_create() call for every open since
 * it is wasteful. Not positve I even need the per open() OBJECT. Could just account
 * the OBJECT (and its name) to the system heap. 
 * 
*/
	kill_restrict(tmp_obj.mem.mm.restrict);
	return r;
}

//RUSH1: permission checking? 
int
memmgr_tymem_open(const char *name, OBJECT **obpp, PROCESS *prp) {
	OBJECT						*obp;
	unsigned					name_len;
	int							r;
	memclass_attr_t  dummy_attr;
	memclass_entry_t  *e;
	mempart_id_t  mpart_id;

	name_len = strlen(name);
	pthread_mutex_lock(&rl_mux);
/*
	for(obp = ty_head; obp != NULL; obp = obp->hdr.next) {
		if(memcmp(obp->tymem.name, name, name_len + 1) == 0) {
			goto done;
		}
	}
*/
	r = ENOMEM;
	if ((e = memclass_find(name, NULL)) == NULL) {
		kprintf("memory class (%s) not configured\n", name);
		r = ENODEV;
		goto fail1;
	}
	if ((mpart_id = mempart_getid(prp, e->data.info.id)) == NULL) {
		r = ENOENT;
		goto fail1;
	}
	/*
	 * although this OBJECT will be created in order to represent a partition
	 * of a different class of memory, the OBJECT itself will be allocated from
	 * the callers sysram memory partition
	*/
	obp = object_create(OBJECT_MEM_TYPED, NULL, mpart_id);
	if(obp == NULL) goto fail1;

	// use _smalloc() since we already had to get the name_len for restrict_create()
	if ((obp->tymem.name = _smalloc(name_len + 1)) == NULL) {
		goto fail2;
	}
	STRLCPY(obp->tymem.name, name, name_len + 1);

	r = restrict_create(name, name_len, obp, &dummy_attr);
	if(r != EOK) goto fail3;

	/* add to queue */
/* ty_head no longer required
	obp->hdr.next = ty_head;
	ty_head = obp;
*/

	*obpp = obp;
	pthread_mutex_unlock(&rl_mux);
	return EOK;
	
fail3:
	_sfree(obp->tymem.name, name_len + 1);

fail2:
	object_done(obp);

fail1:	
	pthread_mutex_unlock(&rl_mux);
	return r;
}


void
memmgr_tymem_close(OBJECT *obp) {
	memobj_lock(obp);
	kill_restrict(obp->mem.mm.restrict);
	shmem_done(obp); // Free up pmem list if possible
	
	/*
	 * because memmgr.resize() invoked by shmem_done() doesn't do anything on
	 * typed memory objects, pretend we are done
	*/
	obp->mem.mm.flags &= ~MM_MEM_INPROGRESS;
	memobj_unlock(obp);
}


struct pa_quantum *
tymem_rdb_alloc(OBJECT *obp, size_t size, unsigned flags) {
	rsrc_request_t		req;
	int					r;
	struct pa_quantum	*head;
	struct pa_quantum	**owner;
	struct pa_quantum	*pq;
	size_t				requested_size;
	struct pa_restrict	*rl;
	memsize_t			resv_size = 0;
	memclass_entry_t *e;
	memclass_sizeinfo_t prev;

	head = NULL;
	owner = &head;
	requested_size = size;

	/* check to see if the partition will allow the requested allocation */
	if (MEMPART_CHK_and_INCR(obp->hdr.mpid, requested_size, &resv_size) != EOK) {
		return NULL;
	}

	/* FIX ME - this brute force lookup of the mclass_entry_t (in order
	 * to get the 'memclass_sizeinfo_t' data needs to be fixed. Instead
	 * of the memclass_id_t being stored in the object, probably need
	 * to store an memclass_entry_t ** (similar to mempart_t *)
	 * This way the allocation functions would be available as well
	*/
	e = memclass_find(NULL, mempart_get_classid(obp->hdr.mpid));

#ifndef NDEBUG
	if (e == NULL) crash();
	if ((r = pthread_mutex_lock(&e->lock.mutex)) != EOK) {
		kprintf("class lock failed with errno %d\n", r);
		crash();
	}
#else	/* NDEBUG */
	/* this lock won't work if the kernel ever uses non sysram memory for any internal objects */
	KASSERT(!KerextAmInKernel());
	pthread_mutex_lock(&e->lock.mutex);
#endif	/* NDEBUG */

	prev = e->data.info.size;

	do {
restart:
		rl = obp->mem.mm.restrict;

		for( ;; ) {
			req.length = size;
			req.start = rl->start;
			req.end = rl->end;
			req.align = __PAGESIZE;
			req.flags = RSRCDBMGR_MEMORY | RSRCDBMGR_FLAG_RANGE | RSRCDBMGR_FLAG_ALIGN;
			
			/* can the allocator satisfy the request ? */
			if ((e->data.info.size.reserved.free < resv_size) ||
				(e->data.info.size.unreserved.free < (size - resv_size))) {
				goto fail1;
			}
			if ((r = rsrcdbmgr_proc_interface(&req, 1, RSRCDBMGR_REQ_ATTACH)) == EOK) {
				if (size <= resv_size) {
					e->data.info.size.reserved.free -= size;
					e->data.info.size.reserved.used += size;
				}
				else {
					e->data.info.size.reserved.free -= resv_size;
					e->data.info.size.reserved.used += resv_size;
					e->data.info.size.unreserved.free -= (size - resv_size);
					e->data.info.size.unreserved.used += (size - resv_size);
				}
				pq = pa_alloc_fake(req.start, size);
				if(pq == NULL) {
					pthread_mutex_unlock(&e->lock.mutex);
					goto fail2;
				}
				pq->u.inuse.next = NULL;
				pq->flags |= PAQ_FLAG_RDB;
				*owner = pq;
				owner = &pq->u.inuse.next;
				break;
			}
			rl = rl->next;
			if(rl == NULL) {
				if(flags & PAA_FLAG_CONTIG) goto fail1;
				if(size <= __PAGESIZE) goto fail1;
				size >>= 1;
				goto restart;
			}
		}
		requested_size -= size;
	} while(requested_size != 0);

	pthread_mutex_unlock(&e->lock.mutex);
	MEMCLASSMGR_EVENT(&e->data, memclass_evttype_t_DELTA_TU_INCR, &e->data.info.size, &prev);
	return head;

fail2:
	tymem_rdb_free(obp, req.start, size);

fail1:
	while(head != NULL) {
		pq = head->u.inuse.next;
		tymem_rdb_free(obp, pa_quantum_to_paddr(head), NQUANTUM_TO_LEN(head->run));
		pa_free_fake(head);
		head = pq;
	}

	MEMPART_UNDO_INCR(obp->hdr.mpid, requested_size, resv_size);
	pthread_mutex_unlock(&e->lock.mutex);
	return NULL;
}


struct pa_quantum *
tymem_rdb_alloc_given(OBJECT *obp, paddr_t paddr, size_t size, int *rp) {
	rsrc_request_t		req;
	int					r;
	struct pa_quantum	*pq;
	memsize_t			resv = 0;

	if (MEMPART_CHK_and_INCR(obp->hdr.mpid, size, &resv) != EOK) {
		return NULL;
	}

	req.length = size;
	req.start = paddr;
	req.end = paddr + size - 1;
	req.flags = RSRCDBMGR_MEMORY | RSRCDBMGR_FLAG_RANGE;
	r = rsrcdbmgr_proc_interface(&req, 1, RSRCDBMGR_REQ_ATTACH);
	if(r != EOK) {
		MEMPART_UNDO_INCR(obp->hdr.mpid, size, resv);
		goto fail1;
	}
	r = ENOMEM;
	pq = pa_alloc_fake(paddr, size);
	if(pq == NULL) goto fail2;

	pq->flags |= PAQ_FLAG_RDB;
	return pq;

fail2:		
	tymem_rdb_free(obp, paddr, size);

fail1:	
	*rp = r;
	return NULL;
}


void
tymem_rdb_free(OBJECT *obp, paddr_t paddr, size_t size) {
	rsrc_request_t	req;
	int  ret;

	//RUSH2: handle partial frees
	req.start = paddr;
	req.end = paddr + size - 1;
	req.length = size;
	req.flags = RSRCDBMGR_MEMORY;
	ret = rsrcdbmgr_proc_interface(&req, 1, RSRCDBMGR_REQ_DETACH);
	if (ret == EOK) {
		memsize_t  resv_size = MEMPART_DECR(obp->hdr.mpid, size);
		memclass_sizeinfo_t prev;
		/* FIX ME - this brute force lookup of the mclass_entry_t (in order
		 * to get the 'memclass_sizeinfo_t' data needs to be fixed. Instead
		 * of the memclass_id_t being stored in the object, probably need
		 * to store an memclass_entry_t ** (similar to mempart_t *)
		 * This way the allocation functions would be available as well
		*/
		memclass_entry_t *e = memclass_find(NULL, mempart_get_classid(obp->hdr.mpid));

		prev = e->data.info.size;

		KASSERT(size >= resv_size);

		/* LOCK */		
		/* this lock won't work if the kernel ever uses non sysram memory for any internal objects */
		KASSERT(!KerextAmInKernel());
		if (pthread_mutex_lock(&e->lock.mutex) != EOK) crash();

		e->data.info.size.reserved.free += resv_size;
		e->data.info.size.reserved.used -= resv_size;
		e->data.info.size.unreserved.free += (size - resv_size);
		e->data.info.size.unreserved.used -= (size - resv_size);

		/* UNLOCK */
		pthread_mutex_unlock(&e->lock.mutex);
		MEMCLASSMGR_EVENT(&e->data, memclass_evttype_t_DELTA_TU_DECR, &e->data.info.size, &prev);
	}
#ifndef NDEBUG
	else {
		kprintf("%d: rsrcdbmgr_proc_interface(sz=0x%x) failed e=%d for obp %p (%p)\n",
					__LINE__, size,	ret, obp, obp->hdr.mpid);
	}
#endif	/* NDEBUG */
}

static void
tymem_rdb_free_info(struct pa_restrict *rlp, paddr_t *totalp, paddr_t *contigp) {
	struct pa_restrict	*rp;
	int					num;
	unsigned			idx;
	unsigned			i;
	rsrc_alloc_t		list[20];
	paddr_t				start;
	paddr_t				end;
	paddr_t				total;
	paddr_t				contig;

	total = 0;
	contig = 0;

	idx = 0;
	for( ;; ) {
		num = rsrcdbmgr_proc_query(list, NUM_ELTS(list), idx, RSRCDBMGR_MEMORY);
		if(num <= 0) break;
		idx += num;
		for(i = 0; i < num; ++i) {
			if(!(list[i].flags & RSRCDBMGR_FLAG_USED)) {
				for(rp = rlp; rp != NULL; rp = rp->next) {
					//RUSH3: If the list is in ascending order, kick out early
					//RUSH3: if list[i].end < rp->start
					start = max(list[i].start, rp->start);
					end   = min(list[i].end, rp->end);		
					if(start < end) {
						paddr_t		size;

						size = (end - start) + 1;
						total += size;
						if(size > contig) contig = size;
					}
				}
			}
		}
	}
	if(totalp != NULL) *totalp = total;
	if(contigp != NULL) *contigp = contig;
}


void
tymem_free_info(OBJECT *obp, paddr_t *total, paddr_t *contig) {
	if(obp->mem.mm.flags & MM_MEM_RDB) {
		tymem_rdb_free_info(obp->mem.mm.restrict, total, contig);	
	} else {
		//RUSH3: This doesn't work for a physical system - the pa_*
		//RUSH3: routines are only used by the virtual memmgr code.
		pa_free_info(obp->mem.mm.restrict, total, contig);
	}
}

size_t rdb_reserve(size_t size)
{
	return size;
}
