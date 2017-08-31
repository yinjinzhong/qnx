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

#define EXTERN


//#define TRACKPMEM
#include "vmm.h"
#include "apm.h"

#define TAIL_CALC(_runsize)		(-((_runsize)-1))
#define SET_TAIL(_qp,_runsize)	if((_qp)->run > 1) ((_qp)[(_qp)->run-1].run = (_runsize))
#define NON_HEAD_QUANTUM		(0)
#define IS_BACKQ(run)			(run < NON_HEAD_QUANTUM)
#define IS_HEADQ(run)			(run > NON_HEAD_QUANTUM)
#define ALIGN_64K				0x10000


// These are the bits that have to be preserved when the quantum is freed
#define PAQ_FLAG_PRESERVE_BITS	(PAQ_FLAG_INIT_REQUIRED | PAQ_FLAG_COLOUR_MASK)

struct sysram_entry {
	paddr_t			start;
	paddr_t			end;
};

struct sysram_list {
	unsigned			count;
#ifdef __WATCOMC__
	unsigned dummy; // align to avoid warning message about padding.
#endif
	struct sysram_entry list[1]; // variable size
};

static struct block_head	**blk_head;
static paddr_t				sysram_end;
static memsize_t mem_reserved_size = 0;		// current reserved size
static memsize_t mem_reserved_configured = 0;	// configured reserved size // FIX ME - do I really need this?

static memclass_sizeinfo_t *pa_size_info(memclass_sizeinfo_t *s, memclass_info_t *i);
static memsize_t pa_reserve(memsize_t s, memclass_info_t *i);
static memsize_t pa_unreserve(memsize_t s, memclass_info_t *i);
static void pa_resv_adjust(memsize_t s, int sign, memclass_info_t *i);
static void pa_memclass_init(memclass_attr_t *attr, const char *sysmem_name, allocator_accessfncs_t *f);

static allocator_accessfncs_t  afncs =
{
	STRUCT_FLD(reserve) pa_reserve,
	STRUCT_FLD(unreserve) pa_unreserve,
	STRUCT_FLD(size_info) pa_size_info,
	STRUCT_FLD(resv_adjust) pa_resv_adjust
};
static memclass_attr_t  sysram_attr =
{
	STRUCT_FLD(size) 0,	// filled in in pa_init()
	STRUCT_FLD(limits)
		{STRUCT_FLD(alloc)
			{STRUCT_FLD(size)
				{STRUCT_FLD(min) __PAGESIZE, STRUCT_FLD(max) memsize_t_INFINITY}}},
};

/* used for events */
static memclass_entry_t *sys_memclass = NULL;

static const struct kdump_private  kdump_private = {
	&blk_head,
	&colour_mask,
	offsetof(ADDRESS, cpu.pgdir),
	sizeof(paddr_t),
};

/*	if UBER_DEBUG is defined, we will do a lot of extra checking which can cause
	a lot of problems with timing, and not being able to finish allocations
	in a preemption case, when we're doing all this verification on each allocation
	and free etc.  So by default, the debug variant build not do this extra checking
	but will still enable the sanity checks which may be in the code as well as compile
	in the verification code for later debuging.
*/
#ifdef UBER_DEBUG
	#define PA_DEBUG
	#define	dump_all()				debug_dump_all()
	#define	verify_blk(a1)			debug_verify_blk(a1)
	#define verify_all()			debug_verify_all()
#else
	#ifndef NDEBUG
		#define PA_DEBUG
	#endif
	#define dump_all()
	#define verify_blk(a1)
	#define verify_all()
#endif

#ifdef PA_DEBUG
void
dump_q_list(struct pa_quantum *qp) {
	struct pa_quantum	*np;

	if (!qp) kprintf("Run is EMPTY!\n");
	while(qp != NULL) {
		np = qp->u.flink.next;
		kprintf("Run %08x r/f/b/p %d/%08x/%d/%08x\n",
			(unsigned)qp, qp->run,qp->flags, qp[qp->run-1].run,
			qp->u.inuse.qpos);
		qp = np;
	}
}

void 
debug_dump_blkheadrun(struct block_head *bh) {
	struct pa_quantum *curr;
	paddr_t start,end;

	KerextLock();
	kprintf("Freelist dump for bh %x\n", bh);
	for (curr = bh->free.next ; curr ; curr = curr->u.flink.next) {
		start = pa_quantum_to_paddr(curr);
		end = pa_quantum_to_paddr(curr+((unsigned)curr->run-1));
		end += __PAGESIZE-1;
		kprintf("Size : %d Quantums, Paddr range : [0x%08x - 0x%08x] - %s\n",
			curr->run,(unsigned)start,(unsigned)end,
			(curr->flags & PAQ_FLAG_INUSE)? "INUSE!" : "Free");
	}

}

void
debug_dump_all() {
	struct block_head	**bhp;
	struct block_head	*bh;

	bhp = blk_head;
	for( ;; ) {
		bh = *bhp;
		if(bh == NULL) break;
		debug_dump_blkheadrun(bh);
		++bhp;
	}
}

void
debug_verify_blk(struct block_head *bh) {
	unsigned					i;
	struct pa_quantum			*chk;
	struct pa_quantum			**owner;
	volatile struct pa_quantum	*prev;
	unsigned					flags;

	// Verify that the runs are good
	prev = NULL;
	chk = bh->quanta;
	while(chk < &bh->quanta[bh->num_quanta]) {
		if(chk->run <= 0) {
			struct pa_quantum* pq;
			// start of run should be positive
			//kprintf("RUN %d %d [%d] %d %d\n",chk[-2].run,chk[-1].run,chk->run,chk[1].run,chk[2].run);
			for (pq=chk+1; pq->run == 0; pq++) {
				// nothing to do
			}
			//kprintf("Next run val %d\n", pq->run);
			crash();
		}
		if ((chk->run > 1) && (chk[(chk->run)-1].run >= 0) ) {
			// end of run should be negative displacement
			//kprintf("RUN %d %d [%d] %d %d ... %d [%d] %d\n",
			//	chk[-2].run,chk[-1].run,chk->run,chk[1].run,chk[2].run,
			//	chk[chk->run-2].run,chk[chk->run-1].run,chk[chk->run].run);
			crash();
		}
		flags = chk->flags & PAQ_FLAG_INUSE;
		if(!flags && (prev != NULL) && !(prev->flags & PAQ_FLAG_INUSE)) {
			// two free runs in a row - should have coalesed
			crash();
		}
		for(i = 1; i < chk->run; ++i) {
			if(chk[i].run > NON_HEAD_QUANTUM) {
				// doesn't point to begining of run
				//kprintf("RUN blk #%d %d %d [%d] %d %d (%d)\n",chk->blk,chk[i-2].run,chk[i-1].run,chk[i].run,chk[i+1].run,chk[i+2].run, chk[chk[i].run].run);
				crash();
			}
		}
		prev = chk;
		chk += chk->run;
	}

	// Verify that the free list is good
	owner = &bh->free.next;
	for(chk = bh->free.next; chk != NULL; chk = chk->u.flink.next) {
		if(chk->flags & PAQ_FLAG_INUSE) {
			// entry not free
			//kprintf("run,flags,endrun (%d,%08x,%d)\n", chk->run, chk->flags, chk[chk->run-1].run);
			crash();
		}
		if(owner != chk->u.flink.owner) {
			// back pointer not correct
			crash();
		}
		owner = &chk->u.flink.next;
	}
	if(bh->free.owner != owner) {
		// block head doesn't point to last entry on free list
		crash();
	}
}

void
debug_verify_all() {
	struct block_head	**bhp;
	struct block_head	*bh;

	bhp = blk_head;
	for( ;; ) {
		bh = *bhp;
		if(bh == NULL) break;
		debug_verify_blk(bh);
		++bhp;
	}
}
#endif /* PA_DEBUG */


static void *
early_alloc(struct sysram_list *sl, unsigned size) {
	unsigned			max = 0;
	paddr_t				max_size = 0;
	paddr_t				curr;
	paddr_t				end;
	unsigned			i;

	size = ROUNDUP(size, sizeof(uint64_t));

#if CPU_SYSTEM_PADDR_START != 0
	#error code does not support CPU_SYSTEM_PADDR_START != 0
#endif

	for(i = 0; i < sl->count; ++i) {
		if(sl->list[i].start < CPU_SYSTEM_PADDR_END) {
			end = sl->list[i].end;
			if(end > CPU_SYSTEM_PADDR_END) end = CPU_SYSTEM_PADDR_END;
			curr = end - sl->list[i].start;
			if(curr > max_size) {
				max_size = curr;
				max = i;
			}
		}
	}
	if(size > max_size) {
		crash();
	}
	return cpu_early_paddr_to_vaddr(sl->list[max].start, 
				ROUNDUP(size, QUANTUM_SIZE), &sl->list[max].end);
}


// Restrict ranges to allowed values (think 32-bit paddr_t's)
#define TOP_PADDR	(~(paddr_t)0)

static int
find_last_paddr(struct asinfo_entry *as, char *name, void *data) {
	paddr64_t	end;

	if(as->owner == AS_NULL_OFF) {
		end = as->end;
		if(end > TOP_PADDR) end = TOP_PADDR;
		last_paddr = end;
		return 0;
	}
	return 1;
}


static int
make_sysram_list(struct asinfo_entry *as, char *name, void *d) {
	struct sysram_list	*data = d;
	unsigned			i;
	struct sysram_entry	se;
	paddr64_t			start_addr;
	paddr64_t			end_addr;

	start_addr = as->start;
	if(start_addr <= last_paddr) {
		end_addr = as->end;
		if(end_addr > last_paddr) end_addr = last_paddr;

		//Round everything to quantum boundries
		se.start = ROUNDUP(start_addr, QUANTUM_SIZE);
		se.end = ROUNDDOWN(end_addr+1, QUANTUM_SIZE) - 1;

		if(se.start < se.end) {
			// Sort the list of sysram's into descending order.
			// Ignore anything smaller than a page.
			i = data->count;
			for( ;; ) {
				if(i == 0) break;
				if(data->list[i-1].start > se.start) break;
				data->list[i] = data->list[i-1];
				--i;
			}
			data->list[i] = se;
			data->count++;
			if(se.end > sysram_end) sysram_end = se.end;
		}
	}

	return 1;
}

static unsigned
init_block(struct block_head **bhp, unsigned blk_idx, struct sysram_entry *se, struct pa_quantum *quanta) {
	unsigned			num;
	struct pa_quantum	*pq;
	unsigned			i;
	paddr_t				block_size;
	struct block_head	*bh = bhp[blk_idx];

	bh->start.paddr = se->start;
	bh->end.paddr   = se->end;
	block_size = (bh->end.paddr - bh->start.paddr) + 1;
	mem_free_size += block_size;

	bh->generation = 0;
	num = block_size >> QUANTUM_BITS;
	bh->num_to_clean = num;
	bh->num_quanta = num;
	bh->quanta = quanta;
	pq = quanta;
	for(i = 0; i < num; ++i, ++pq) {
		if(((i % 1024) == 0) && (bhp == blk_head)) {
			// Initial initialization (from pa_init). Interrupts haven't
			// been set up yet, so we have to poll the minidriver callouts
			mdriver_check();
		}	
		pq->flags = PAQ_COLOUR_NONE;
		pq->blk = blk_idx;
		pq->u.flink.next = NULL;
		pq->u.flink.owner = NULL;
		pq->run = NON_HEAD_QUANTUM;
	}

	quanta->run = num;

	/* set the last Q to be a neg. displacment to the head */
	SET_TAIL(quanta, TAIL_CALC(quanta->run));

	quanta->u.flink.owner = &bh->free.next;
	bh->free.next = quanta;
	bh->free.owner = &quanta->u.flink.next;

	return num;
}

void
pa_init(unsigned colours) {
	struct sysram_list	*sl;
	unsigned			num_asinfos;
	unsigned			i;
	unsigned			num_quanta;
	struct pa_quantum	*quanta;
	struct pa_restrict	*res;
	void				*early;
	void				*early_start;
	unsigned			early_size;
	paddr_t				early_paddr;
	struct block_head	*blk;

	#define ADVANCE_EARLY(size)	(early = (uint8_t *)early + (size))

	//Assuming that 'colours' is a power of two.
	colour_mask = colours - 1;
	colour_mask_shifted = colour_mask << ADDR_OFFSET_BITS;
	if(colours >= PAQ_FLAG_COLOUR_MASK) {
		kprintf("%d exceeds supported colour limit\n", colours);
		crash();
	}

	walk_asinfo("memory", find_last_paddr, NULL);

	num_asinfos = _syspage_ptr->asinfo.entry_size / sizeof(struct asinfo_entry);
	sl = _alloca(sizeof(*sl) + sizeof(sl->list[0]) * num_asinfos);
	sl->count = 0;
	walk_asinfo("sysram", make_sysram_list, sl);

	// Allocate one extra quantum for zero sized allocations
	num_quanta = 1;
	for(i = 0; i < sl->count; ++i) {
		num_quanta += ((sl->list[i].end - sl->list[i].start) + 1) >> QUANTUM_BITS;
	}

	// Calculate how much memory we need for all the early allocation
	// structures so we can do just one big alloc - keeps things
	// contiguous and simple

	// Memory needed for pa_quantum's
	early_size = num_quanta * sizeof(*quanta);
	// Memory for block head array.
	early_size += (sl->count+1) * sizeof(*blk_head);
	// Memory for block head entries.
	early_size += sl->count * sizeof(**blk_head);
	// Memory for restriction lists
	early_size +=  sizeof(*res) * ((2 - CPU_SYSTEM_PADDR_MUST)+3);

	early = early_start = early_alloc(sl, early_size);

	// Init the default restriction lists.
	res = early;
	ADVANCE_EARLY(sizeof(*res) * (2 - CPU_SYSTEM_PADDR_MUST));
	restrict_proc = res;
	res->start   = CPU_SYSTEM_PADDR_START;
	res->end     = CPU_SYSTEM_PADDR_END;
	res->checker = 0;
	#if !CPU_SYSTEM_PADDR_MUST
		res->next = &res[1];
		++res;
		res->start   = 0;
		res->end     = ~(paddr_t)0;
		res->checker = 0;
	#endif
	res->next = NULL;

#if CPU_SYSTEM_PADDR_START != 0
	#error code does not support CPU_SYSTEM_PADDR_START != 0
#endif
	res = early;
	ADVANCE_EARLY(sizeof(*res) * 3);
	restrict_user = res;

	res->start = CPU_SYSTEM_PADDR_END + 1;
	res->end = ~(paddr_t)0;
	res->checker = 0;
	res->next = &res[1];
	++res;
	res->start = 0;
	res->end = CPU_SYSTEM_PADDR_END;
	res->checker = 0;
	res->next = NULL;

	/* NYI
		This kludge shouldn't be needed once typed memory is in
		since drivers could request the correct type on their
		own.  (This restriction allows us to avoid alloc'ing
		paddrs > 4G when drivers etc alloc buffers for hardware
		with the assumption the RAM location is always 32bit 
		addressable
	*/
	++res;
	restrict_user_4G_first = res;
	res->start = restrict_user->start;
	res->end = ~(paddr32_t)0;
	res->checker = 0;
	res->next = restrict_user; /* link the old restriction */

	quanta = early;
	ADVANCE_EARLY(num_quanta * sizeof(*quanta));

	blk = early;
	ADVANCE_EARLY((sl->count+1)*sizeof(**blk_head));
	blk_head = early;
	i = 0;
	do {
		blk_head[i] = blk++;
	} while(++i < sl->count);
	blk_head[i] = NULL;

	for(i=0; i < sl->count; ++i) {
		quanta += init_block(blk_head, i, &sl->list[i], quanta);
		if(i == 0) {
			// Allocate an extra quanta at the end of the first block
			// that we can use to handle zero size physical allocations
			quanta->flags = PAQ_COLOUR_NONE;
			quanta->blk = 0;
			quanta->u.flink.next = NULL;
			quanta->u.flink.owner = NULL;
			quanta->run = 0;
			++quanta;
		}
	}

	// The early allocation area is in one of the blocks - we need
	// to mark it as allocated so that it doesn't get re-used. Keeping
	// the area in the phys allocator structures simpifies things
	// for the kernel dumper and vmm_mapinfo() code.

	(void) cpu_vmm_vaddrinfo(NULL, (uintptr_t)early_start, &early_paddr, NULL);
	quanta = pa_paddr_to_quantum(early_paddr);
	CRASHCHECK(quanta==NULL);
	(void) pa_alloc_given(quanta, LEN_TO_NQUANTUM(early_size), NULL);
	quanta->flags |= PAQ_FLAG_SYSTEM;
	system_heap_vaddr_lo = (uintptr_t)early_start;
	system_heap_vaddr_hi = (uintptr_t)early_start + early_size - 1;

	verify_all();

	sysram_attr.size = sysram_end + 1;
	pa_memclass_init(&sysram_attr, "sysram", &afncs);
}

void
pa_start_background(void) {
	//NYI: Initialize for pulse that will start a priority 1 thread
	//that zeroes/flushes pages in the background.

	kdebug_kdump_private(&kdump_private);
}

/*
	AM Note:
	Watch out, as on boards with lots of mem (ie. testlab board
	with 6gigs of ram) it is possible that a single run will be
	> 2Gigs which means pointer arithmetic (eg. end-start) will
	result in a negative difference.  We do some funky casting
	to address that issue.
*/
//FUTURE: Have to convert to paddr_t before upshifting in case the block
//FUTURE: is bigger than 4G. Could avoid if we split >4G blocks when 
//FUTURE: initializing.
#define PQ_TO_PADDR(bh, pq)	((bh)->start.paddr + (((paddr_t)(unsigned)((pq) - (bh)->quanta)) << QUANTUM_BITS))

paddr_t
pa_quantum_to_paddr(struct pa_quantum *pq) {
	struct block_head	*bh;

	if(pq->blk == PAQ_BLK_FAKE) {
		return ((struct pa_quantum_fake *)pq)->paddr;
	}

	bh = blk_head[pq->blk];
	return PQ_TO_PADDR(bh, pq);
}

struct pa_quantum *
pa_paddr_to_quantum(paddr_t paddr) {
	struct block_head	*bh;
	struct block_head	**bhp;
	struct block_head	**rover;
	struct block_head	**start;
	static unsigned		last_blk; 
	
	bhp = blk_head;
	start = rover = bhp + last_blk;
	do {
		bh = *rover;
		if((paddr >= bh->start.paddr) && (paddr <= bh->end.paddr)) {
			last_blk = rover - bhp;
			return &bh->quanta[((paddr_t)(paddr - bh->start.paddr)) >> QUANTUM_BITS];
		}
		if(*++rover == NULL) rover = bhp;
	} while(rover != start);
	
	return NULL;
}

struct pa_quantum *
pa_getrunhead(struct pa_quantum *pq) {
	struct pa_quantum *curr = pq;

	if (IS_BACKQ(curr->run)) return (curr+curr->run);
	if (IS_HEADQ(curr->run)) return (curr);

	/* find the head */
	/* NYI - perhaps we should keep going until it's a positive num rather than nonzero */
	for (curr-- ; curr->run == NON_HEAD_QUANTUM ; curr-- ) {
		/* nothing to do */
	}
	return (curr);
}

/* 
	Carve out a continous run of quanta from start to len from the free list.
	'start' does not have to be the start of a run, could be
	anywhere
*/
										
static unsigned
pa_carve(struct block_head *bhp, struct pa_quantum *head, struct pa_quantum* start, size_t len, 
				unsigned *colour) {
	struct pa_quantum 	*lhs;
	struct pa_quantum	*rhs;
	size_t 				total_runlen;
	int 				nrun;
	unsigned			status=0;

    verify_all();

	CRASHCHECK(start == NULL);

	len = ROUNDUP(len, QUANTUM_SIZE);
	nrun = len >> (QUANTUM_BITS);

	/* can only check head of the run for flags due to run optimizations */
	CRASHCHECK(head->flags & PAQ_FLAG_INUSE);
	
	total_runlen = head->run;

	/* 
		Linked list structure:

	           +-----------+ +-----------------------------+ +-----------~~~
	           |           | |                             | |
	           |           V |                             V |
	~~~------+-+-+         +-+-+--------------------+      +-+-+---------~~~
	         | N |<--+  +->| N |                    |      | N |
	         | O |   |  |  | O |                    |      | O |
	~~~------+-+-+   |  |  +-+-+--------------------+      +-+-+---------~~~
	           |     |  |    |                                |
	           |     |  +-------------------------------------+
	~~~--------+     +-------+
	
	N = next pointer
	O = owner pointer (points to the next pointer pointing to you)

	Run carving:

	3 cases to consider here:
		a) Cutting of the top of a run
		b) Cutting from the middle of a run
		c) Cutting from the end of a run

	    Left         Centre        Right
	/------------\/----------\/-------------\
	              |          |
	+---+---------|----------|--------------+
	| N |         |          |              |
	| O |         |          |              |
	+---+---------|----------|--------------+
    	          |          |

	*/

	if (IS_HEADQ(start->run)) {
		start->run = nrun;
		SET_TAIL(start, TAIL_CALC(start->run));
		
		if(total_runlen == nrun) {
			/* taking whole run */

			// remove 'start' from free list;
			*start->u.flink.owner = start->u.flink.next;
			if(start->u.flink.next) {
				start->u.flink.next->u.flink.owner = start->u.flink.owner;
			} else {
				*start->u.flink.owner = NULL;
				bhp->free.owner = start->u.flink.owner;
			}
		} else {
			rhs = start+nrun;

			rhs->run = total_runlen-nrun; /* just set the head to some value */
			rhs->flags &= ~PAQ_FLAG_INUSE; /* turn off the inuse flag that might be set (since rhs is made from the middle */
			SET_TAIL(rhs, TAIL_CALC(rhs->run));	

			/* relink remaining piece */
			rhs->u.flink.owner = start->u.flink.owner;
			*rhs->u.flink.owner = rhs; 
			rhs->u.flink.next = start->u.flink.next;
			/* there might not be another run after us */
			if (rhs->u.flink.next) {
				rhs->u.flink.next->u.flink.owner = &rhs->u.flink.next;
			} else {
				bhp->free.owner = &rhs->u.flink.next;
			}
		}
	
	} else if (&head[head->run] == &start[nrun]) { 
		/* carve from the end */
		head->run -= nrun;
		start->run = nrun;
		SET_TAIL(start, TAIL_CALC(start->run));
		SET_TAIL(head, TAIL_CALC(head->run));
	} else { 
		/* carve from middle */
		lhs = head;
		rhs = start+nrun;
	
		/* 
			Remake the runs in the middle and RHS.
			For the LHS just stuff the new runlen since the neg. displacements are ok.
		*/
		lhs->run = start-lhs;
		SET_TAIL(lhs, TAIL_CALC(lhs->run));

		start->run = nrun;
		SET_TAIL(start, TAIL_CALC(start->run));

		rhs->run = total_runlen-(lhs->run+nrun);
		SET_TAIL(rhs, TAIL_CALC(rhs->run));

		/* relink LHS and RHS */

		rhs->u.flink.next = lhs->u.flink.next;
		rhs->u.flink.owner = &lhs->u.flink.next;
		*rhs->u.flink.owner = rhs;

		if (rhs->u.flink.next) {
			rhs->u.flink.next->u.flink.owner = &rhs->u.flink.next;
		} else {
			bhp->free.owner = &rhs->u.flink.next;
		}
	}

	//NYI: Need atomic? locking?
//	bhp->generation++;

	start->u.flink.next = NULL;
	start->u.flink.owner = NULL;
	start->flags |= PAQ_FLAG_INUSE;

	verify_blk(blk_head[start->blk]);
	
	/* 
		NYI - Since we can't determine if we need to zero certain blocks (b/c we
		don't update the information in the middle quantums any longer for speed)
		we force a block zero for now, needed or not.  In the future we should come
		up with a scheme, such as looking at the head and tail to determine if the
		run is fully zero'd
	*/
	status |= PAA_STATUS_NOT_ZEROED|PAA_STATUS_COLOUR_MISMATCH;

	return status;
}

static paddr_t
pa_largest_pow2(paddr_t size) {
	paddr_t max;

	for (
#if _PADDR_BITS - 0 == 64
		/* Optimization : check if the upper 32bits have any bits, and if not, skip the entire half */
		max = ( (size>>32) & 0xffffffffU ) ? ((paddr_t)0x1 << (_PADDR_BITS-1)) : ((paddr_t)0x1 << 32);
#else
		max = ((paddr_t)0x1 << (_PADDR_BITS-1));
#endif
		!(max & size) ;
		max>>=1) {
		// nothing to do
	}
	return max;
}


/*
 * Allocate some physical memory. There are several conditions that
 * must be satisfied for usable memory to result:
 *
 *	1. Be within the given restriction list
 *  2. Satisfy contiguous/alignment/size requirements.
 *  3. Conditioned by the 'checker' function of the restriction entry
 *		(not currently implemented).
 *	4. Match the requested colour.
 *  5. Contain all zeros.
 *
 * The last two conditions aren't enforced by this code since they're
 * correctable. The pa_alloc() function merely indicates whether 
 * they're satisfied or not with the PAA_STATUS_COLOUR_MISMATCH and
 * PAA_STATUS_NOT_ZEROED bits. The upper level code is responsible for
 * dealing with them as needed.
 */

#define PADDR_NEXT64K(p)	ROUNDUP((p)+1, ALIGN_64K)
#define PADDR_PREV64K(p)	ROUNDDOWN((p), ALIGN_64K)


#define PAS_PREEMPT		0x01
#define PAS_EFREE		0x02
#define PAS_DONE		0x04

#define PA_MAX_RETRIES	50

struct kerargs_pa_alloc {
    paddr_t 			size;
    paddr_t				resv_size;
    paddr_t 			align;
    unsigned 			colour;
    unsigned 			flags;
    unsigned 			status;
    struct pa_restrict	*restrict;
	struct pa_quantum 	*alloc;
	unsigned			state;
	unsigned			retries;
};

static void
kerext_pa_alloc(void	*d) {
	struct kerargs_pa_alloc *kap = d;
	paddr_t 			size = kap->size;
	const paddr_t		resv_size = kap->resv_size;
	paddr_t				curr_resv_size = resv_size;
	paddr_t 			align = kap->align;
	unsigned 			colour = kap->colour;
	unsigned 			flags = kap->flags;
	struct pa_restrict	*curr_res;
	struct pa_quantum	*qp, *rq_start, *rq_end;
	struct pa_quantum	**prev_qp;
	struct pa_quantum	*mod_rq_start, *mod_rq_end;
	struct pa_quantum	*qblk_start, *qblk_end;
	struct pa_quantum	*found_run,**tail;
	struct block_head	*bhp;
	struct block_head	**rover;
	paddr_t 			curr_size,curr_align, rp_start,rp_end;
	paddr_t 			size_left;
	int 				valid_restrict;

    verify_all();

	if(!(kap->state & PAS_PREEMPT) || (++kap->retries > PA_MAX_RETRIES)) {
		// Lock for kernel allocations, or if we've retried too
		// many times without making any forward progress (interrupt
		// load is high).
		KerextLock();
	}

	tail = &kap->alloc;
	/* Get 'tail' to end of allocated list, (we've preempted and restarted) */
	while(*tail != NULL) {
		tail = &(*tail)->u.flink.next;
	}


//{
//static unsigned alloc_count;
//kprintf("c=%d size=%x, align=%x, flags=%x\n", ++alloc_count, (unsigned)size, (unsigned)align, flags);
//}

	/* round up size to next quantum */
	size = ROUNDUP(size, QUANTUM_SIZE);
	if(size == QUANTUM_SIZE) flags |= PAA_FLAG_CONTIG; /* because it is contig by definition */

	//Check and make sure there's enough memory in the system for
	//the request.
	//NYI: Possible holes if we start doing multiple allocations at
	//the same time.
	if(mem_reserved_size < resv_size) goto fail; // must be first
	if(mem_free_size < (size - resv_size)) goto fail;
	
	if(flags & PAA_FLAG_CONTIG) {
		align = ROUNDUP(align, QUANTUM_SIZE);
	} /* else alignment get changed for each block farther down */

	
	CRASHCHECK(flags & MAP_RESERVMASK);

	size_left = size;
	
	for(curr_res = kap->restrict ; curr_res ; curr_res=curr_res->next) {
		rq_start = pa_paddr_to_quantum(curr_res->start);
		rq_end = pa_paddr_to_quantum(curr_res->end);
		
		for(	curr_size = (flags & PAA_FLAG_CONTIG) ? size_left : pa_largest_pow2(size_left) ;
				curr_size >= QUANTUM_SIZE ; curr_size >>= 1) {

			if(curr_size > size_left) continue; /* skip over sizes that aren't applicable */
			/*
				if contig, we always have the same alignment, otherwise we're hunting for power
				of 2 sizes, and our alignment should be as such also.
			*/			
			curr_align=	(flags & PAA_FLAG_CONTIG) ? align : curr_size;

			valid_restrict = 0;
			rover = blk_head;
			for( ;; ) {
				bhp = *rover++;
				if(bhp == NULL) break;

				/* 
					AM Note: Must use paddrs for comparison, since the actual location of Quantum descrptors
					outside of the blk has no real location/meaning in reference to others from other
					blks.  This is just for figuring out if the restriction is even part of the blk.
				*/
				/* restriction falls completely outside of this blk */
				if((curr_res->start > bhp->end.paddr) || (curr_res->end < bhp->start.paddr)) {
					continue;
				}
				valid_restrict = 1;
		
				found_run = NULL;	
				qblk_start = bhp->quanta;
				qblk_end = &bhp->quanta[bhp->num_quanta-1];
				mod_rq_start = rq_start;	
				mod_rq_end = rq_end;
		
				/* restriction ends outside (or is NULL b/c its way out), 
				   take up end slack */
				if((mod_rq_end==NULL) 
				  || (mod_rq_end > qblk_end)
				  || (mod_rq_end->blk != qblk_end->blk)) {
					mod_rq_end = qblk_end;
				}
				/* restriction starts outside the blk, but ends inside, 
				   tighten the start */
				if((mod_rq_start==NULL)
				  || (mod_rq_start < qblk_start)
				  || (mod_rq_start->blk != qblk_start->blk)) {
					mod_rq_start = qblk_start;
				} 
				/* 
					by elimination, if the restriction doesn't fall completely outside, 
					or somewhat inside (left or right), then we must be completly inside
					or exactly the same size
				*/
		
				if(!((mod_rq_start >= qblk_start) && (mod_rq_end <= qblk_end))) {
					/*
						we're allocting contig, and the restriction doesn't fit inside blk
						or something really messed up happen with the checking above
						since this can't be true for the non-contig case anymore.
					*/
					continue; /* try next block */
				}

				/* 
					AM Note: Watch out, as carving runs out from the freelist cause the list
					to be modified such that qp->u.flink.next will not be valid.  Also note
					the freelist snaked in the quantums isn't going to always be in accending
					order (or any particular order) so make no assumptions about the location
					of a 'found' run
				*/

				qp = bhp->free.next;

				/* these are subset restrictions that are applicable only to this blk */
				rp_start = pa_quantum_to_paddr(mod_rq_start);
				rp_end = pa_quantum_to_paddr(mod_rq_end);

				for(;;) { /* manually control the loop */
					paddr_t s,e,c; /* start, end, cut at */
					paddr_t coloured_c,x86_c; /* coloured cut and x86 paddr */
					struct pa_quantum	*qp_end;

					if(qp == NULL) break;

					prev_qp = qp->u.flink.owner;
					CRASHCHECK(qp->flags & PAQ_FLAG_INUSE);
					/* check if we're interested in this run (ie. a piece of it could be within the restriction */	
					qp_end = qp + qp->run - 1;

					if((qp->run < LEN_TO_NQUANTUM(curr_size)) || 
						((qp_end < mod_rq_start) || (qp > mod_rq_end)) ) {
						qp=qp->u.flink.next;
						continue; /* nope we're not */
					}
			
					s = pa_quantum_to_paddr(qp);

					/* we cut from the end of the run, or the end of the restriction within the run */
					e = (qp_end >= mod_rq_end) ? rp_end : pa_quantum_to_paddr(qp_end);

					/* AM Note:
						                 (c)
						                  +----------------------------+
						                  |                            |
						                  v                            v
						+-----+-----+-----+-----+-----+-----+-----+-----+
						|  8  |  -  |  -  |  -  |  -  |  -  |  -  |  -7 |
						+-----+-----+-----+-----+-----+-----+-----+-----+
						^                                         ^
						|                                         |
					   (s)                                       (e)

					Since we can't use quantum pointers that are off the edge of a run (as it might
					not be addressable (or even a valid pointer), we use qp[qp->run-1] as our end
					quantum pointer.  *But* we add 0xfff to the paddr we get when translating
					that end quantum pointer, so that we have a paddr as close to the end as we can
					safely get (and still translate back to a quantum).
					*/

					coloured_c = x86_c = 0;

					/* Adjust down for alignment */
					c = ROUNDDOWN(((e+(QUANTUM_SIZE-1))-(curr_size-1)), curr_align);

					/* Adjust down for colour (only if we don't already match) */
					coloured_c = COLOUR_ADJUST_PA(c, (colour << ADDR_OFFSET_BITS));
					if(coloured_c > c) { /* we'll catch other conditions farther down */
						qp=qp->u.flink.next;
						continue; /* doesn't fit with alignment + colour adj */
					}
					c = coloured_c;

					/* check if we're going to need to back up for the X64K case (grrr) */ 
					if((flags & PAA_FLAG_NOX64K) && ((c+curr_size) > PADDR_NEXT64K(c) )) {
						/*  we crossed a 64k boundary, backup the amount we cross */
						x86_c = c - ROUNDUP((c+curr_size - PADDR_NEXT64K(c)), curr_align);

						/* if >64K requested, must ensure the paddr is aligned on 64K */
						if((curr_size > ALIGN_64K) && (x86_c & (ALIGN_64K-1))) {
							x86_c = PADDR_PREV64K(x86_c);
						}
 
						if((x86_c > c) || (x86_c < s) || (x86_c < rp_start)) {
							qp=qp->u.flink.next;
							continue; /* doesn't fit with the NOX64K adj */
						}
						c = x86_c;
					}

					/*	check to see if we fell outside of the run.  The adjustments can cause
						us to have a c (cut point) less than the start, or greater (in the case
						of a wrap from 0x0000000).
					*/
					if((c < s) || (c < rp_start) || ((c+curr_size-QUANTUM_SIZE) > rp_end)) {
						qp=qp->u.flink.next;
						continue; /* doesn't fit with alignment adjusted */
					}

					found_run = qp+(LEN_TO_NQUANTUM(c-s)); /* avoid a paddr2quant conversion by doing this */

					/* === Need to be locked for this small area === */
					if(kap->state & PAS_PREEMPT) KerextLock();

					CRASHCHECK((found_run < qp) || ((found_run+LEN_TO_NQUANTUM(curr_size)) > qp+qp->run));

					kap->status |= pa_carve(bhp, qp, found_run, curr_size, &colour);
					if(colour != PAQ_COLOUR_NONE) {
						colour = (colour + COLOUR_PA(curr_size)) & colour_mask; /* adjust colour for next carve */
					}
					if (curr_size <= curr_resv_size) {
						mem_reserved_size -= curr_size;
						curr_resv_size -= curr_size;
					}
					else
					{
						mem_reserved_size -= curr_resv_size;
						mem_free_size -= (curr_size - curr_resv_size);
						curr_resv_size = 0;	// cur_resv_size -= cur_resv_size : )
					}
					size_left -= curr_size;
					// Update sizes to allocate in case we preempt and restart
					kap->size = size_left;
					kap->resv_size = curr_resv_size;
					CRASHCHECK(!(found_run->flags & PAQ_FLAG_INUSE));

					/* reload new qp to try from the same (or what was the same) */
					qp = *prev_qp;
				
					/* link onto our found list */
					found_run->u.flink.next = NULL;
					found_run->u.flink.owner = tail;
					*tail = found_run;
					tail = &found_run->u.flink.next;

					if(size_left == 0) goto done;

					// We've managed to allocate another chunk of memory,
					// so reset the retry count since we're now making some
					// forward progress on satifying the allocation request
					kap->retries = 0;

					/*	AM Note:  Watch out, as there are _two_ preemption paths out from
						this point on (after the KerextUnlock).  We could unlock, and then
						preemption could restart the kernel call before we make it to the
						KerextNeedPreempt() check.  The other is self-inflicted preemption,
						where we return and the caller loop outside
						will recall us with the saved list.  In either case, the logic
						at the begining of kerext_pa_alloc() will restore the working list.

						This extra preemption path (ie. the check) is to avoid the case where
						we take the preemption request while locked (while small and fixed, could
						happen).  If that happened, we wouldn't kick out until the allocation
						completed (or another preemption request booted us out).
					*/
						
						
					if(kap->state & PAS_PREEMPT) {
						KerextUnlock();
						if(KerextNeedPreempt()) { /* check for pending pre-emption */
							return;
						}
					}
					/* === End of the locked area for carving, we can take preemption again === */

					if(size_left < curr_size) break; /* try the next size */
	
					/*	Looks like we can still try to grab more likesized runs,
						so we use the same current run, and try again
					*/
	
				} /* for each free run */

				if((found_run != NULL) && (size_left < curr_size)) {
					break; /* cause us to re-evaluate our next size */
				}
					
			} /* for each blkhead */

			if(!valid_restrict || (flags & PAA_FLAG_CONTIG)) {
				/*
					it seems we've iterated through all the blks and	
					the restriction doesn't fit any of them - short circuit out
					so we don't spin through all the sizes in the non-contig case
				*/
				break;
			}

	
		} /* for each size */

	} /* for each restriction */

	// If we get to this point, we didn't manage to allocate everything
	// we needed (size_left != 0). 
	CRASHCHECK(size_left == 0);

fail:	
	KerextLock(); // make sure we're locked
	kap->state |= PAS_EFREE; /* trigger a pa_free outside */

done: 
	/* still locked from last carve */
	found_run = kap->alloc;
	if(found_run != NULL) {
		// When we linked the first allocated entry into the list, we put in a 
		// back link from the first entry to &kap->alloc that we don't really 
		// want. It's more efficient to just do that and then correct it here.
		found_run->u.flink.owner = NULL;
		if(found_run->u.flink.next == NULL) {
			kap->status |= PAA_STATUS_ISCONTIG;
		}
	}

	/* NYI - Forced miscolour and zero */
	kap->status |= (PAA_STATUS_NOT_ZEROED|PAA_STATUS_COLOUR_MISMATCH);

	verify_all();
	kap->state |= PAS_DONE;
}

static struct pa_quantum *
_pa_alloc(paddr_t size, paddr_t align, unsigned colour, unsigned flags,
			unsigned *status_p, struct pa_restrict *restrict, paddr_t resv_size) {
	struct kerargs_pa_alloc data;

	if(size == 0) {
		// For a size zero allocation, just return a valid address.
		if(status_p != NULL) *status_p = PAA_STATUS_ISCONTIG;
		return &blk_head[0]->quanta[blk_head[0]->num_quanta];
	}

	// Look for restriction entries that can't possibly work
	// (e.g. possibly first restrict_user entry) and skip over them.
	for( ;; ) {
		if(restrict->start < sysram_end) break;
		restrict = restrict->next;
		if(restrict == NULL) return NULL;
	}

	data.size = size;
	data.resv_size = resv_size;
	data.align = align;
	data.colour = colour;
	data.flags = flags;
	data.restrict = restrict;
	data.status = 0;
	data.alloc = NULL;
	data.retries = 0;

	if(KerextAmInKernel()) {
		data.state = 0;
		kerext_pa_alloc(&data);
	} else {
		data.state = PAS_PREEMPT;

		/* 	loop (due to preemption) until we've completed 
			the allocation or an error occurs 
		*/
		do {
			__Ring0(kerext_pa_alloc, &data);
		} while(!(data.state & PAS_DONE));
	}

#ifndef NDEBUG
	{
		struct pa_quantum	*p;
		unsigned			i;
		for (p=data.alloc; p ; p=p->u.flink.next) {
			if(!(p->flags & PAQ_FLAG_INUSE)) crash();
			for(i = 0; i < p->run; ++i) {
				if(p[i].flags & (PAQ_FLAG_INITIALIZED|PAQ_FLAG_HAS_SYNC|PAQ_FLAG_LOCKED|PAQ_FLAG_MODIFIED)) {
					crash();
				}
			}
		}
	}
#endif

	if(data.state & PAS_EFREE) {
		pa_free_list(data.alloc, resv_size);
		data.alloc = NULL;
	}

#ifdef TRACKPMEM	
	{
		struct pa_quantum* p;
		for(p=data.alloc; p ; p=p->u.flink.next) {
			if(p == data.alloc) 
			kprintf("%s ALLOC 0x%x for %d [free=0x%x]\n", (p==data.alloc) ? "PMEM" : "    ", (uintptr_t)p, p->run, (unsigned)mem_free_size);
		}
	}
#endif	

	if(status_p != NULL) *status_p = data.status;

	return data.alloc;
}

struct pa_quantum *
pa_alloc(paddr_t size, paddr_t align, unsigned colour, unsigned flags,
			unsigned *status_p, struct pa_restrict *restrict, paddr_t resv_size)
{
	struct pa_quantum *p;
	memclass_sizeinfo_t  prev, cur;
	pa_size_info(&prev, NULL);
	if ((p = _pa_alloc(size, align, colour, flags, status_p, restrict, resv_size)) != NULL)
	{
		pa_size_info(&cur, NULL);
		MEMCLASSMGR_EVENT(&sys_memclass->data, memclass_evttype_t_DELTA_TU_INCR, &cur, &prev);
	}
	return p;
}


struct kerargs_pa_alloc_given {
	struct pa_quantum	*pq;
	unsigned			num;
	unsigned			*status;
};

static void
kerext_pa_alloc_given(void *data) {
	struct kerargs_pa_alloc_given *kap = data;
	unsigned				r;

	r = pa_alloc_given(kap->pq, kap->num, kap->status);
	KerextStatus(0, r);
}


int
pa_alloc_given(struct pa_quantum *pq, unsigned num, unsigned *status_p) {
	unsigned 			tmp_status;
	struct pa_quantum	*start;
	struct pa_quantum	*next;
	struct pa_quantum	*pq_end;

	if(!KerextAmInKernel()) {
		struct kerargs_pa_alloc_given data;

		data.pq = pq;
		data.num = num;
		data.status = status_p;
		return __Ring0(kerext_pa_alloc_given, &data);
	}

	start = pq;
	while(start->run == 0) {
		--start;
	}
	if(start->run < 0) start += start->run;
	if(start->flags & PAQ_FLAG_INUSE) return ENXIO; 
	next = start + start->run;
	pq_end = pq + num;
	if(pq_end > next) return ENXIO;
	KerextLock();
	tmp_status = pa_carve(blk_head[start->blk], start, pq, num << QUANTUM_BITS, NULL);
	mem_free_size -= NQUANTUM_TO_LEN(num);
	pq->u.inuse.next = NULL;
	if(status_p != NULL) *status_p = tmp_status;
	return EOK;
}


struct kerargs_pa_free {
    struct pa_quantum *paq;
    unsigned num;
    paddr_t  resv_size;
};

static void
kerext_pa_free(void *data) {
	struct kerargs_pa_free *kap = data;

	pa_free(kap->paq, kap->num, kap->resv_size);
	KerextStatus(0, 0);
}


void
_pa_free(struct pa_quantum *paq, unsigned num, paddr_t resv_size) {
	struct pa_quantum	*prev = NULL; /* GCC silence */
	struct pa_quantum	*curr;
	struct pa_quantum	*t1;
	struct pa_quantum	*t2;
	struct pa_quantum	*next;
	struct pa_quantum	*run_start;
	struct pa_quantum	*paq_end;
	struct block_head	*bh;
	int					coalesed = 0;
	unsigned			i;

	#define COL_LEFT	0x1
	#define COL_RIGHT	0x2

	if(num == 0) {
		return;
	}

#ifndef NDEBUG
	if(paq == NULL) crash();
	if(paq->blk == PAQ_BLK_FAKE) crash();
	run_start = pa_getrunhead(paq);
	if(!(run_start->flags & PAQ_FLAG_INUSE)) {
		kprintf("paq %p paq_head %p, paq->run %d, runhead r/f/bl = %d/%08x/%d\n", 
			paq, run_start, paq->run, 
			run_start->run, run_start->flags, run_start[run_start->run-1].run);
		crash();
	}
#endif

	if(!KerextAmInKernel()) {
		struct kerargs_pa_free data;

		data.paq = paq;
		data.num = num;
		data.resv_size = resv_size;
		__Ring0(kerext_pa_free, &data);
		return;
	}

	bh = blk_head[paq->blk];

	/* NYI - smarter locking */
	KerextLock();
#ifdef TRACKPMEM	
	kprintf("PMEM FREE  0x%x for %d [free=0x%x]\n", (uintptr_t)paq, num, (unsigned)mem_free_size);
#endif	

	// Turn off (almost) all the flags
	for(i = 0; i < num; ++i) {
		paq[i].flags &= PAQ_FLAG_PRESERVE_BITS;
	}

	//NYI: Need atomic? locking?
//	bh->generation++;

	run_start = paq;
	paq_end = paq + num;

	if(IS_HEADQ(paq->run)) {
		// start of object
		curr = paq;
		next = &curr[curr->run];

		if(curr > bh->quanta) {
			prev = pa_getrunhead(curr-1);

			if(!(prev->flags & PAQ_FLAG_INUSE)) {
				// coalase with previous entry
				// (we'll adjust the paq to &paq[num] run numbers down below)
				SET_TAIL(prev, 0);
				curr->run = 0;
				prev->run += num;
				SET_TAIL(prev, TAIL_CALC(prev->run));
				run_start = prev;
				coalesed = COL_LEFT;
			}
		}
	} else {
		// middle of object - not a very high runner case 
//		kprintf("pa_free from middle!\n");
		curr = pa_getrunhead(paq);
		next = &curr[curr->run];

		//turn quanta in front into their own run
		curr->run = paq - curr;
		if (curr->run > 1) curr[curr->run-1].run = -(curr->run-1);
		
		//turn quanta on the end into their own run is done farther down
	}

	//make the run free
	t1 = paq;
	if(!coalesed) {
		paq->run = num;
		paq->flags &= PAQ_FLAG_PRESERVE_BITS;
		SET_TAIL(paq, TAIL_CALC(paq->run));
		++t1;
	}
#if 0
 else {
		/* only reset the head if we have more that 1, since the tail is the head in num==1 */
		if (num > 1) {
			paq->run = NON_HEAD_QUANTUM;
		}
	}
#endif

	if(paq_end < next) {
		//Turn quanta on end into their own run
		t1 = paq_end;
		t1->run = next - paq_end; 
		t1->flags |= PAQ_FLAG_INUSE; /* NYI is this really needed?  The Qs are already 'in use' */
		SET_TAIL(t1, TAIL_CALC(t1->run));
#ifndef NDEBUG
	} else if(paq_end > next) {
		crash();
#endif
	} else if((next < &bh->quanta[bh->num_quanta]) 
		&& !(next->flags & PAQ_FLAG_INUSE)) {
		// coalese with following block
		// remove 'next' from free list;
		*next->u.flink.owner = next->u.flink.next;
		if(next->u.flink.next) {
			next->u.flink.next->u.flink.owner = next->u.flink.owner;
		} else {
			*next->u.flink.owner = NULL;
			bh->free.owner = next->u.flink.owner;
		}

		// Add the right hand side to the run
		SET_TAIL(run_start, 0); /* remove old tail */
		run_start->run += next->run;
		t1 = next;
		t2 = &next[next->run];
		if (t1->run > 1) {
			t1->run = NON_HEAD_QUANTUM;
			t1->flags &= ~PAQ_FLAG_INUSE;
		}
		SET_TAIL(run_start, TAIL_CALC(run_start->run)); 
		coalesed |= COL_RIGHT;
	} 

	if(!(coalesed & COL_LEFT)) {
		// we didn't coalese to the left, add to tail of free list
		paq->u.flink.next = 0;
		paq->u.flink.owner = bh->free.owner;
		*bh->free.owner = paq;
		bh->free.owner = &paq->u.flink.next;
	}

	verify_all();

	{
		paddr_t amount_freed = (paddr_t)num << QUANTUM_BITS;
#ifndef NDEBUG
		if (amount_freed < resv_size)
		{
			kprintf("amount_freed = %P, resv_size = %P\n",
					(paddr_t)amount_freed, (paddr_t)resv_size);
		}
#endif	/* NDEBUG */
		mem_reserved_size += resv_size;
		mem_free_size += (amount_freed - resv_size);
	}
#if 0
	if(atomic_add_value(&bh->num_to_clean, num) == 0) {
		//NYI: kick prio 1 thread to clean new pages
	}
#endif	
}

void pa_free(struct pa_quantum *paq, unsigned num, paddr_t resv_size)
{
	memclass_sizeinfo_t  prev, cur;
	pa_size_info(&prev, NULL);
	_pa_free(paq, num, resv_size);
	pa_size_info(&cur, NULL);
	MEMCLASSMGR_EVENT(&sys_memclass->data, memclass_evttype_t_DELTA_TU_DECR, &cur, &prev);
}

/*
 * pa_free_paddr() used to be the following define in mm_internal.h
 * 
 * #define 			pa_free_paddr(p, s, r)	\
 *						pa_free(pa_paddr_to_quantum(p), LEN_TO_NQUANTUM(s), r)
 *
 * I had to change it because the following condition was observed ...
 * A call to vmm_munmap() with a system address would call this macro at line
 * 321 of vmm_munmap.c (the prp == NULL case). This call came from the
 * _release_heap_pages() in a heap_destroy(). Everything was legit as far as
 * the vaddr and sizes were concerned. The 12KB heap being destroyed happen to
 * consist of 1 8KB block + 1 4KB block (most of the time 3 - 4KB blocks made
 * up the typical heap and that causes no problem as you will see when you read
 * on). The pa_paddr_to_quantum() call would return the pa_quantum * as expected
 * however it so happened that the 2 4KB pages of the 8KB mapping consisted of
 * 2 runs of 1 block (4KB each). This caused pa_free() to fail because we were
 * telling it to free 2 quantums (LEN_TO_NQUANTUM(8192) == 2) but passing it a
 * pa_quantum pointer with a run of 1. I guess the assumption had always been
 * that mappings were deliberately contiguous (ie. a virtual address range which
 * was contiguous and > 1 page would be represented by 1 run of the appropriate
 * number of quanta) for system memory allocations and not accidentally contiguous
 * (a contiguous mapping of > 1 page which was actually created from 2 or more
 * independent physical allocations and that the timing was such that they
 * appear contiguous but are actually made up of 2 single block runs as described
 * above). The following function corrects that assumption.
 * 
 * Note that the _release_heap_pages() could have been modified so that it
 * umaps 1 4KB block at a time but that does not solve the problem in the general
 * case and is far less efficient since this code will release whatever the run
 * is
*/ 
void pa_free_paddr(paddr_t p, paddr_t s, paddr_t resv_size)
{
	int num = LEN_TO_NQUANTUM(s);
	while (num > 0)
	{
		struct pa_quantum *paq = pa_paddr_to_quantum(p);
		unsigned int num_to_free = min(paq->run, num);	// grab this before paq is gone
		paddr_t  free_size = NQUANTUM_TO_LEN(num_to_free);
		pa_free(paq, num_to_free, min(resv_size, free_size));
		num -= num_to_free;
		p += free_size;
		if (resv_size >= free_size)
			resv_size -= free_size;

		KASSERT(num >= 0);
		KASSERT((resv_size & (NQUANTUM_TO_LEN(1) - 1)) == 0);
	}
}
						
void
pa_free_list(struct pa_quantum *qp, paddr_t resv_size) {
	struct pa_quantum	*np;

	while(qp != NULL) {
		np = qp->u.flink.next;
		pa_free(qp, qp->run, resv_size);
		qp = np;
	}
}


struct pa_quantum *
pa_alloc_fake(paddr_t paddr, paddr_t size) {
	struct pa_quantum_fake	*pq;

	pq = _scalloc(sizeof(*pq));
	if(pq != NULL) {
		pq->paddr = paddr;
		pq->q.run = LEN_TO_NQUANTUM(size);
		pq->q.flags = PAQ_FLAG_INUSE | PAQ_COLOUR_NONE;
		pq->q.blk = PAQ_BLK_FAKE;
	}
	return &pq->q;
}


void
pa_free_fake(struct pa_quantum *pq) {
	if(pq->blk != PAQ_BLK_FAKE) crash();
	_sfree(pq, sizeof(struct pa_quantum_fake));
}


struct kerargs_pa_free_info {
	paddr_t				run_start;
	paddr_t				run_end;
	paddr_t				total;
	paddr_t				contig;
    struct pa_quantum 	*pq;
	struct block_head	*bh;
	unsigned			generation;
};

static void
kerext_pa_free_info(void *data) {
	struct kerargs_pa_free_info *kap = data;
	struct block_head	*bh;
	struct pa_quantum	*pq;

	bh = kap->bh;
	KerextLock();
	if(kap->generation != bh->generation) {
		kap->generation = bh->generation;
		kap->pq = bh->free.next;
		kap->total = 0;
		kap->contig = 0;
	}
	pq = kap->pq;
	if(pq == NULL) {
		KerextStatus(0, 0);
	} else {
		kap->run_start = PQ_TO_PADDR(bh, pq);
		kap->run_end   = kap->run_start + NQUANTUM_TO_LEN(pq->run) - 1;
		kap->pq = pq->u.flink.next;
		KerextStatus(0, 1);
	}
}

void
pa_free_info(struct pa_restrict *restrict, paddr_t *totalp, paddr_t *contigp) {
	paddr_t						total;
	paddr_t						contig;
	paddr_t						start;
	paddr_t						end;
	struct block_head			**bhp;
	struct pa_restrict			*rp;
	struct kerargs_pa_free_info	data;

	//FUTURE: generate a preconditioned restriction list
	//FUTURE:   - no overlapping regions
	//FUTURE:   - ascending start addresses
	total = 0;
	contig = 0;

	bhp = blk_head;
	for( ;; ) {
		data.bh = *bhp++;
		if(data.bh == NULL) break;
		data.generation = data.bh->generation - 1; // force restart
		while(__Ring0(kerext_pa_free_info, &data)) {
			for(rp = restrict; rp != NULL; rp = rp->next) {
				//RUSH3: If the list is in ascending order, kick out early
				//RUSH3: if data.run_end < rp->start
				start = max(data.run_start, rp->start);
				end   = min(data.run_end, rp->end);		
				if(start < end) {
					paddr_t		size;

					size = (end - start) + 1;
					data.total += size;
					if(size > data.contig) data.contig = size;
				}
			}
		}
		total += data.total;
		if(data.contig > contig) contig = data.contig;
	}
	if(totalp != NULL) *totalp = total;
	if(contigp != NULL) *contigp = contig;
}


int
pa_sysram_overlap(struct pa_restrict *res) {
	struct block_head	*bh;
	struct block_head	**bhp;

	bhp = blk_head;
	for( ;; ) {
		bh = *bhp++;
		if(bh == NULL) break;
		if(!((res->end < bh->start.paddr) || (res->start > bh->end.paddr))) {
			return 1;
		}
	}
	return 0;
}


static int add_code;

static int 
pmem_add_cleanup(message_context_t *ctp, int code, unsigned flags, void *handle) {
	 struct block_head	**bhp = ctp->msg->pulse.value.sival_ptr;
	 struct block_head	**curr;
	 unsigned			size;
	 int				timerid;
	 struct _timer_info	info;

	 curr = bhp;
	 size = sizeof(*curr);
	 do {
		 size += sizeof(*curr);
		 ++curr;
	 } while(*curr != NULL);
	 _sfree(bhp, size);
	 
	 // Find the timer and delete it....
	 timerid = 0;
	 for( ;; ) {
		 timerid = TimerInfo(PROCMGR_PID, timerid, _NTO_TIMER_SEARCH, &info);
		 if(timerid < 0) return EOK;
		 if((SIGEV_GET_TYPE(&info.event) == SIGEV_PULSE)
			&& (info.event.sigev_code == add_code)
			&& (info.event.sigev_value.sival_ptr == bhp)) break;
		 timerid = timerid + 1;
	 }
	 (void) TimerDestroy(timerid);
	 return EOK;
}


int
pa_pmem_add(paddr_t start, paddr_t len) {
	struct sysram_entry		se;
	unsigned				idx;
	unsigned				num;
	struct block_head		**bhp;
	struct block_head		**old;
	struct block_head		*bh;
	static pthread_mutex_t	add_mux;
	struct pa_quantum		*quanta;
	struct sigevent			event;
	int						timerid;

	se.start = start;
	if(start <= TOP_PADDR) {
		se.end = start + len - 1;
		if(se.end > TOP_PADDR) se.end = TOP_PADDR;

		//Round everything to quantum boundries
		se.start = ROUNDUP(se.start, QUANTUM_SIZE);
		se.end = ROUNDDOWN(se.end+1, QUANTUM_SIZE) - 1;

		if(se.start < se.end) {
			num = ((se.end - se.start) + 1) >> QUANTUM_BITS;
			quanta = _smalloc(num * sizeof(*quanta));
			if(quanta == NULL) goto fail1;
			bh = _smalloc(sizeof(*bh));
			if(bh == NULL) goto fail2;
			pthread_mutex_lock(&add_mux);
			idx = 0;
			bhp = blk_head;
			do {
				++idx;
				++bhp;
			} while(*bhp != NULL);
			bhp = _smalloc((idx + 2) * sizeof(*bhp));
			if(bhp == NULL) goto fail3;
			memcpy(bhp, blk_head, idx * sizeof(*bhp));
			bhp[idx] = bh;
			bhp[idx+1] = NULL;

			(void) init_block(bhp, idx, &se, quanta);

			if(se.end > sysram_end) sysram_end = se.end;
			// Switch to new set of block heads...
			old = blk_head;
			blk_head = bhp;
			pthread_mutex_unlock(&add_mux);

			if(add_code <= 0) {
				add_code = pulse_attach(dpp, MSG_FLAG_ALLOC_PULSE, 0, pmem_add_cleanup, NULL);
			}
			if(add_code > 0) {
				// Fire a pulse in two seconds that will free the old
				// block head array - by that time everybody should be done
				// using it. If the TimerCreate or TimerSettime fails
				// we'll leak a little memory but, other than that, things
				// will be OK.
				SIGEV_PULSE_INIT(&event, PROCMGR_COID, 1, add_code, old);

				timerid = TimerCreate(CLOCK_MONOTONIC, &event);
				if(timerid != -1) {
					struct _itimer value;

					value.nsec = 2000000000;
					value.interval_nsec = 0;
					if(TimerSettime(timerid, 0, &value, NULL) != 0) {
						(void) TimerDestroy(timerid);
					}
				}
			}
		}
	}

	return EOK;

fail3:
	pthread_mutex_unlock(&add_mux);
	_sfree(bh, sizeof(*bh));

fail2:
	_sfree(quanta, num * sizeof(*quanta));

fail1:
	return ENOMEM;

}

/*
 * pa_reserve
 * 
 * Mark <size> bytes of memory as reserved
 * 
 * Returns: the amount of memory which was reserved
 * 
 * FIX ME -lock
*/
static memsize_t pa_reserve(memsize_t size, memclass_info_t *info)
{
	if (size <= mem_free_size)
	{
		memclass_sizeinfo_t  prev, cur;
		pa_size_info(&prev, NULL);
		mem_free_size -= size;
		mem_reserved_size += size;
		mem_reserved_configured += size;

		pa_size_info(&cur, NULL);
		MEMCLASSMGR_EVENT(&sys_memclass->data, memclass_evttype_t_DELTA_RF_INCR, &cur, &prev);
		return size;
	}
	else
		return 0;
}

/*
 * pa_unreserve
 * 
 * Mark <size> bytes of memory as no longer part of a partition reservation
 * 
 * Returns: the amount of memory which was unreserved
 * 
 * FIX ME -lock
*/
static memsize_t pa_unreserve(memsize_t size, memclass_info_t *info)
{
	if (size <= mem_reserved_size)
	{
		memclass_sizeinfo_t  prev, cur;
		pa_size_info(&prev, NULL);

		mem_free_size += size;
		mem_reserved_size -= size;
		mem_reserved_configured -= size;

		pa_size_info(&cur, NULL);
		MEMCLASSMGR_EVENT(&sys_memclass->data, memclass_evttype_t_DELTA_RF_DECR, &cur, &prev);
		return size;
	}
	else
		return 0;
}

/*
 * pa_resv_adjust
 *
 * Transfer used memory between reserved and unreserved 
 * 
 * FIX ME -lock
*/
static void pa_resv_adjust(memsize_t size, int sign, memclass_info_t *i)
{
	memclass_sizeinfo_t  prev, cur;
	memclass_evttype_t evt = memclass_evttype_t_INVALID;
	pa_size_info(&prev, NULL);

	if (size == 0)
		return;
	else if (sign == +1)
	{
		/* transfer used unreserved to used reserved (while keeping totals constant) */
		KASSERT(size <= mem_reserved_size);

		mem_free_size += size;		// decrease used unreserved 
		mem_reserved_size -= size;	// increase used reserved
		evt = memclass_evttype_t_DELTA_RU_INCR;
	}
	else if (sign == -1)
	{
		/* transfer used reserved to used unreserved (while keeping totals constant) */
		KASSERT(size <= mem_free_size);

		mem_reserved_size += size;	// decrease used reserved 
		mem_free_size -= size;		// increase used unreserved
		evt = memclass_evttype_t_DELTA_RU_DECR;
	}
#ifndef NDEBUG
	else crash();
#endif	/* NDEBUG */

	pa_size_info(&cur, NULL);
	MEMCLASSMGR_EVENT(&sys_memclass->data, evt, &cur, &prev);
}

/*
 * pa_size_info
 * 
 * Get reserved/unreserved size information.
 * Returns NULL on error
*/
static memclass_sizeinfo_t *pa_size_info(memclass_sizeinfo_t *s, memclass_info_t *info)
{
	/*
	 * FIX ME
	 * The global variables mem_reserved_xxx, mem_free_size, etc should be done
	 * away with in favour of tracking them in the memclass_info_t structure
	 * however for now we do not do this because of the way in which the
	 * allocator was originally written and the fact that mem_free_size is a
	 * global.
	*/
	memclass_info_t  _info;
	if (info == NULL) info = &_info;
	info->size.reserved.free = mem_reserved_size;
	info->size.reserved.used = mem_reserved_configured - info->size.reserved.free;
	info->size.unreserved.free = mem_free_size;
	info->size.unreserved.used = (sysram_end + 1)		// total system ram
								 - (info->size.reserved.used
								 + (info->size.unreserved.free + info->size.reserved.free));	// total unallocated

	KASSERT(s != NULL);

//	s->reserved.free = mem_reserved_size;
//	s->unreserved.free = mem_free_size;

	KASSERT(mem_reserved_configured >= info->size.reserved.free);
//	s->reserved.used = mem_reserved_configured - s->reserved.free;

#ifndef NDEBUG
	/* if (total < reserved.used + total unallocated) */
	if ((sysram_end + 1) < (info->size.reserved.used + (info->size.unreserved.free + info->size.reserved.free)))
	{
		kprintf("total: %P, reserved.used: %P, total_unallocated: %P\n",
				(paddr_t)(sysram_end + 1), (paddr_t)info->size.reserved.used,
				(paddr_t)(info->size.unreserved.free + info->size.reserved.free));
//		crash();
	}
#endif	/* NDEBUG */
//	s->unreserved.used = total - (s->reserved.used + total_unallocated);
	
	*s = info->size;
	return s;
}


/* FIX ME - remove debug code below */
/*
 * pa_free_size
 * 
 * the amount of unallocated unreserved memory
*/
memsize_t pa_free_size(void)
{
#ifndef NDEBUG
	/* walk the quantum lists and calculate how much memory is free */
	struct pa_quantum *curr;
	struct block_head	**bhp = blk_head;
	memsize_t  total_free = 0;

	for( ;; )
	{
		struct block_head	*bh = *bhp;
		
		if(bh == NULL) break;
		for (curr = bh->free.next ; curr ; curr = curr->u.flink.next)
			if ((curr->flags & PAQ_FLAG_INUSE) == 0)
				total_free += NQUANTUM_TO_LEN(curr->run);
		++bhp;
	}
	if (total_free != (mem_free_size + mem_reserved_size))
	{
		kprintf("total_free (%P) != mem_free_size + mem_reserved_size (%P + %P)\n",
				(paddr_t)total_free, (paddr_t)mem_free_size, (paddr_t)mem_reserved_size);
//				crash();
	}
#endif	/* NDEBUG */
	return mem_free_size;
}

/*
 * pa_used_size
 * 
 * the amount of allocated memory (including reserved)
*/
memsize_t pa_used_size(void)
{
	memclass_sizeinfo_t  s;
	pa_size_info(&s, NULL);
	return (s.unreserved.used + s.reserved.used);
}

/*
 * pa_reserved_size
 * 
 * the amount of unallocated reserved memory
*/
memsize_t pa_reserved_size(void)
{
	memclass_sizeinfo_t  s;
	pa_size_info(&s, NULL);
	return s.reserved.free;
}

/*******************************************************************************
 * pa_memclass_init
 * 
 * Initialize the system ram memory class (the memory class handled by the
 * pa_xxx() routines)
 *
 * The system memory class will be created with the memory class of <sysmem_name>
 * and size specified by <sysmem_size>.
 * Other memory classes can be added to the system partition as they are
 * enumerated
 * 
 * Once the sysram memory class is created, a system partition will be created
 * depending on whether the memory partitioning module is installed or not
 *
*/
void pa_memclass_init(memclass_attr_t *attr, const char *sysmem_name, allocator_accessfncs_t *f)
{
	/* can only be called once */	
	if (sys_memclass_id != NULL) crash();

	sys_memclass_id = memclass_add(sysmem_name, attr, f);
	sys_memclass = memclass_find(NULL, sys_memclass_id);

	if (sys_memclass == NULL)
		kprintf("Unable to add system memory class name/size of '%s'/0x%x\n",
				sysmem_name, attr->size);
	else
		MEMPART_INIT(attr->size, sys_memclass_id);
}

__SRCVERSION("pa.c $Rev: 156323 $");
