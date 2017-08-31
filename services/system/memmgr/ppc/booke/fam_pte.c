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
#include <ppc/bookecpu.h>


#if defined(__LITTLEENDIAN__)
	#define TLB_DEFAULT_ENDIAN	PPCBKE_TLB_ATTR_E
#else
	#define TLB_DEFAULT_ENDIAN	0
#endif

#define PPC_ASID_FIRST	1		// 0 is global
static unsigned	num_asids;
static ADDRESS	**asid_map;
static uintptr_t pgszlist[17];


void
fam_pte_init(int phase) {
	struct ppc_tlbinfo_entry	*tlbinfo;
	unsigned					i;
	unsigned					num;
	unsigned					set;
	int							idx;
	uintptr_t					size;

	if(phase == 0) {
		zp_flags &= ~ZP_CACHE_OFF;

		num_asids = (1 << SYSPAGE_CPU_ENTRY(ppc, kerinfo)->asid_bits);
		asid_map = _scalloc(num_asids * sizeof(*asid_map));

		num = _syspage_ptr->un.ppc.tlbinfo.entry_size / sizeof(*tlbinfo);
		tlbinfo = SYSPAGE_CPU_ENTRY(ppc, tlbinfo);
		set = 0;
		for(i = 0; i < num; ++i) {
			set |= tlbinfo[i].page_sizes;
		}
		i = 0;
		for(idx = 15; idx >= 0; --idx) {
			if(set & (1 << idx)) {
				// have a supported page size
				size = 0x400 << (idx*2);
				if(size >= __PAGESIZE) {
					pgszlist[i++] = size;
				}
			}
		}
		wire_init();
	}
}


void
fam_pte_flush_all() {
	tlbop.flush_all();
}


void
fam_pte_mapping_add(uintptr_t vaddr, paddr_t paddr, unsigned prot, unsigned flags) {
	ppcbke_tlb_t	new;

	new.rpn = paddr;
	new.epn = vaddr;
	if(flags & VM_FAULT_GLOBAL) {
		new.tid = 0;
	} else {
		new.tid = get_spr(PPCBKE_SPR_PID);
	}

	new.attr = TLB_DEFAULT_ENDIAN | PPCBKE_TLB_ATTR_M | PPCBKEM_TLB_ATTR_SHAREN;
	if(prot & PROT_NOCACHE) {
		new.attr |= PPCBKE_TLB_ATTR_I;
		if(!(prot & PROT_EXEC)) {
			new.attr |= PPCBKE_TLB_ATTR_G;
		}
	}
		
	new.access = 0;
	if(prot & PROT_READ)  new.access |= PPCBKE_TLB_ACCESS_SR|PPCBKE_TLB_ACCESS_UR;
	if(prot & PROT_WRITE) new.access |= PPCBKE_TLB_ACCESS_SW|PPCBKE_TLB_ACCESS_UW;
	if(prot & PROT_EXEC)  new.access |= PPCBKE_TLB_ACCESS_SX|PPCBKE_TLB_ACCESS_UX;

	new.size = PPCBKE_TLB_SIZE_4K;
	new.ts = PPCBKE_TLB_TS;
	new.v = PPCBKE_TLB_V;
	tlbop.write_entry(-1, -1, &new);
}

void
fam_pte_mapping_del(ADDRESS *adp, uintptr_t vaddr, uint32_t size) {
	if(size <= 10*__PAGESIZE) {
		while(size > 0) {
			tlbop.flush_vaddr(vaddr);
			vaddr += __PAGESIZE;
			size -= __PAGESIZE;
		}
	} else {
		tlbop.flush_all();
	}
}

/*
* fam_pte_asid_alloc:
*
* Assign an asid to a process- may steal one from
* another process. Since this code is called from
* virtual_map_address when switching in a process
* we are already in the kernel and there is no need
* for KerextAmInKernel checks.
*
*/

// NIY: think about real int safe
void
fam_pte_asid_alloc(ADDRESS *adp) {
	static int	asid_rotor = PPC_ASID_FIRST;
	int 		asid;
	int			i;
	ADDRESS 	*oldadp;

	/*
	 * Be very, very careful in here. We can be allocating an asid for
	 * somebody and have an interrupt go off. If the interrupt has a
	 * handler routine, we will re-enterantly execute this code to set up
	 * the address space for the routine.
	 */
	
	/*
	 * Do a quick scan through the asid_map and see 
	 * if there are any unallocated entries. The
	 * reason why we do this is because it is cheaper 
	 * to do this one scan than to steal an asid,
	 * do a MemPageFlushAsid, and also possibly incur
	 * tlb refills for the poor guy we stole from.
	 * If there are no unallocated asids, then go 
	 * back to where we were in the asid_map.
	 */
    for(i = PPC_ASID_FIRST; i < num_asids; ++i) {

/*		if (asid_map[i] == 0
		  && (_smp_cmpxchg((unsigned *)(&asid_map[i]), 0, (unsigned)adp) == 0)) {
*/
//kprintf("i=%x, asid_map[i]=%x\n",i,asid_map[i]);
		if(asid_map[i] == 0) {
			asid_map[i] = adp;
			adp->cpu.asid = i;
			return;
		}
    }
    /* have to steal one */
    if(++asid_rotor >= num_asids) {
		asid_rotor = PPC_ASID_FIRST;
    }
    asid = asid_rotor;
    adp->cpu.asid = asid;
//	oldadp = (ADDRESS *)_smp_xchg((unsigned *)&asid_map[asid], (unsigned)adp);
    oldadp = asid_map[asid];
	asid_map[asid] = adp;
    if(oldadp) {
		/* 
		 * Mark his asid as invalid so
		 * that when we switch to him he'll
		 * pick up another one.
		 */
		oldadp->cpu.asid = PPC_INVALID_ASID;
    }
	tlbop.flush_asid(asid);
}

void
fam_pte_asid_release(ADDRESS *adp) {
	int		flush;

	// Have to do the following atomically, so that an interrupt can't
	// come in and invalidate the asid on us.
	InterruptDisable();
	flush = adp->cpu.asid;
	if(flush != PPC_INVALID_ASID) {
		adp->cpu.asid = PPC_INVALID_ASID;
		asid_map[flush] = NULL;
	}
	InterruptEnable();
	if(flush != PPC_INVALID_ASID) {
		tlbop.flush_asid(flush);
	}
}

#define MAKE_PTE_PADDR(p) ((uint32_t)(p) | (((uint32_t)((p) >> 32) & 0xfff)))


int
cpu_pte_split(uintptr_t vaddr, struct mm_pte_manipulate *data) {
	pte_t					*pde;
	pte_t					*ptep;
	pte_t					**pgdir;
	uintptr_t				pgsz;
	uintptr_t				check;
	uintptr_t				check_end;
	paddr_t					paddr;

	pgdir = data->adp->cpu.pgdir;

	pde = pgdir[L1PAGEIDX(vaddr)];
	if(pde != NULL) {
		ptep = &pde[L2PAGEIDX(vaddr)];
		if(PTE_PRESENT(ptep)) {
			pgsz = PTE_PGSIZE(ptep);
			check = ROUNDDOWN(vaddr, pgsz);
			check_end = check + pgsz - 1;
			if((check < vaddr) || ((vaddr == data->start) && (check_end > data->end))) {
				unsigned	new_flags;

				// We need to split the entries into smaller sizes.
				// For right now, just do 4K

				if(check_end > data->split_end) data->split_end = check_end;
				new_flags = (ptep->flags & ~PPCBKE_PTE_PGSZ_MASK)
						| (PPCBKE_TLB_SIZE_4K << PPCBKE_PTE_PGSZ_SHIFT);
				paddr = PTE_PADDR(ptep);	
//kprintf("splitting %x from size %x\n", check, pgsz);
				for( ;; ) {
					ptep = &pgdir[L1PAGEIDX(check)][L2PAGEIDX(check)];
					//temporarily unmap the entry first so that we
					//don't get a partially built PTE loaded
					ptep->flags = 0;
					ptep->paddr = MAKE_PTE_PADDR(paddr);
					ptep->flags = new_flags;
					pgsz -= __PAGESIZE;
					if(pgsz == 0) break;
					paddr += __PAGESIZE;
					check += __PAGESIZE;
				}
				tlbop.flush_vaddr(vaddr);
				// Since cpu_pte_split gets called multiple times, 
				// this gives the upper level a chance to preempt
				if((data->op & PTE_OP_PREEMPT) && KerextNeedPreempt()) {
					return EINTR;
				}
			}
		}
	}
	if(!(data->op & PTE_OP_TEMP) 
		&& (data->op & PTE_OP_UNMAP)	
			&& (data->shmem_flags & SHMCTL_HIGHUSAGE)) {
		// Have to go into the merge code to clean up any wired entries
		data->op |= PTE_OP_FORCEMERGE;
	}
	return EOK;
}


int
cpu_pte_merge(struct mm_pte_manipulate *data) {
	pte_t			**pgdir;
	pte_t			*pde;
	pte_t			*ptep;
	pte_t			*chk_ptep;
	uintptr_t		*pgszp;
	unsigned		pgsz;
	uintptr_t		run_start;
	uintptr_t		run_last;
	uintptr_t		chk_end;
	uintptr_t		chk_vaddr;
	uintptr_t		merge_start;
	uintptr_t		merge_end;
	paddr_t			paddr;
	unsigned		tlbsize;
	unsigned		pte_flags;
	unsigned		pte_paddr;

	run_start = run_last = 0; // Shut up GCC warning.

	pgdir = data->adp->cpu.pgdir;
	if(!(data->op & PTE_OP_MERGESTARTED)) {
		// look at the PTE's in front of data->start and see if they're
		// contiguous - we might be able to merge them in now. 
		chk_vaddr = data->start;
		// try merging to the start of the largest available pagesize in
		// front of the manipulated region.
		merge_start = ROUNDDOWN(chk_vaddr, pgszlist[0]);
		chk_ptep = NULL;
		for( ;; ) {
			pde = pgdir[L1PAGEIDX(chk_vaddr)];
			if(pde == NULL) break;
			ptep = &pde[L2PAGEIDX(chk_vaddr)];
			if(!PTE_PRESENT(ptep)) break;
			if(chk_ptep != NULL) {
				pgsz = PTE_PGSIZE(ptep);
				chk_vaddr = chk_vaddr & ~(pgsz - 1);
				ptep = &pgdir[L1PAGEIDX(chk_vaddr)][L2PAGEIDX(chk_vaddr)];
				if(PTE_PERMS(ptep) != PTE_PERMS(chk_ptep)) break;
				if((PTE_PADDR(ptep) + pgsz) != PTE_PADDR(chk_ptep)) break;
				data->start = chk_vaddr;
			}
			if(chk_vaddr == merge_start) break;
			chk_ptep = ptep;
			chk_vaddr -= __PAGESIZE;
		}
		data->op |= PTE_OP_MERGESTARTED;
	}
	// try merging to the end of the largest available pagesize beyond
	// the end of the manipulated region.
	merge_end = ROUNDUP(data->split_end + 1, pgszlist[0]) - 1;
	do {
		chk_ptep = NULL;
		for( ;; ) {
			if(data->start >= merge_end) break;
			pde = pgdir[L1PAGEIDX(data->start)];
			if(pde == NULL) break;
			ptep = &pde[L2PAGEIDX(data->start)];
			if(!PTE_PRESENT(ptep)) break;
			if(chk_ptep == NULL) {
				run_start = data->start;
			} else {
				if(PTE_PERMS(ptep) != PTE_PERMS(chk_ptep)) break;
				if((PTE_PADDR(chk_ptep) + PTE_PGSIZE(chk_ptep)) != PTE_PADDR(ptep)) break;
			}
			run_last = data->start;
			data->start += PTE_PGSIZE(ptep);
			if(data->start == 0) data->start = ~(uintptr_t)0;
			chk_ptep = ptep;
		}

		if(chk_ptep != NULL) {
			// We've got a run of PTE's with perms that are the same and the
			// paddrs are contiguous. Start building up larger page sizes
			// based on the vaddr/paddr alignment
			while(run_start < run_last) {
				ptep = &pgdir[L1PAGEIDX(run_start)][L2PAGEIDX(run_start)];
				paddr = PTE_PADDR(ptep);
				pgszp = pgszlist;
				// Find a page size that works
				do {
					pgsz = *pgszp++;
				} while((pgsz > ((run_last-run_start)+__PAGESIZE)) 
					  || ((run_start & (pgsz-1)) != 0)
					  || ((paddr & (pgsz-1)) != 0));

				if(pgsz > PTE_PGSIZE(ptep)) {
					pte_paddr = MAKE_PTE_PADDR(paddr);
					// If this for loop becomes a bottleneck, we can
					// turn pgszlist[] into a two element structure and
					// save the tlbsize value in that in fam_pte_init().
					for(tlbsize = PPCBKE_TLB_SIZE_4K; tlbsize <= PPCBKE_TLB_SIZE_1T; ++tlbsize) {
						if(pgsz == (0x400 << (tlbsize*2))) {
							break;
						}
					}
					pte_flags = (ptep->flags & ~PPCBKE_PTE_PGSZ_MASK) | (tlbsize << PPCBKE_PTE_PGSZ_SHIFT);

					// merge the entries.

//kprintf("merging %x to size %x (tlb=%x)\n", run_start, pgsz, tlbsize);		
					// Temporarily unmap and flush the TLB for the pages that 
					// we're merging. That way nobody gets an inconsistent
					// view of the set.
					chk_vaddr = run_start;
					chk_end = run_start + pgsz;
					for(;;) {
						ptep->flags = 0;
						tlbop.flush_vaddr(chk_vaddr);
						chk_vaddr += __PAGESIZE;
						if(chk_vaddr == chk_end) break;
						ptep = &pgdir[L1PAGEIDX(chk_vaddr)][L2PAGEIDX(chk_vaddr)];
					}
					// Now actually do the merging of the PTE's into
					// a big page.
					chk_vaddr = run_start;
					for(;;) {
						ptep = &pgdir[L1PAGEIDX(chk_vaddr)][L2PAGEIDX(chk_vaddr)];
						ptep->paddr = pte_paddr;
						ptep->flags = pte_flags;
						chk_vaddr += __PAGESIZE;
						if(chk_vaddr == chk_end) break;
					}
				}
				run_start += pgsz;
			}
		} else {
			data->start += __PAGESIZE;
			if(data->start == 0) data->start = ~(uintptr_t)0;
		}
		if((data->op & PTE_OP_PREEMPT) && KerextNeedPreempt()) {
			return EINTR;
		}
	} while(data->start < data->split_end);
	if(!(data->op & PTE_OP_TEMP) && (data->shmem_flags & SHMCTL_HIGHUSAGE)) {
		wire_check(data);
	}
	return EOK;
}


int
cpu_pte_manipulate(struct mm_pte_manipulate *data) {
	pte_t					**pdep;
	pte_t					*pdep_l2base;
	pte_t					*ptep;
	unsigned				orig_flags;
	unsigned				new_paddr;
	uintptr_t				l2_vaddr;
	unsigned				bits;
	pte_t					**l1pagetable;
	struct pa_quantum		*pq;
	unsigned				pa_status;
	int						flushed;
	ADDRESS					*adp;
	int						r;
	mempart_id_t			mpid;

	CHECK_PTE_ALIGNMENT(data);

	adp = data->adp;
	if(adp == NULL) crash();
	l1pagetable = adp->cpu.pgdir;

	if(data->op & PTE_OP_BAD) {
		bits = PPCBKE_PTE_ATTR_I | (PPCBKE_TLB_SIZE_4K << PPCBKE_PTE_PGSZ_SHIFT);
	} else if(data->prot & (PROT_READ|PROT_WRITE|PROT_EXEC)) {
		bits = (PPCBKE_TLB_SIZE_4K << PPCBKE_PTE_PGSZ_SHIFT);
		//RUSH3: if PTE_OP_TEMP, mark PTE as accessable from procnto only if possible
		if(data->shmem_flags & SHMCTL_HAS_SPECIAL) {
			if(data->special & ~PPC_SPECIAL_MASK) {
				return EINVAL;
			}
			bits |= data->special;
		} else {
			bits |= PPCBKE_PTE_ATTR_M | TLB_DEFAULT_ENDIAN;
		}
		if(data->prot & PROT_EXEC)	bits |= PPCBKE_PTE_ACCESS_X;
		if(data->prot & PROT_WRITE)	bits |= PPCBKE_PTE_ACCESS_W;
		if(data->prot & PROT_READ)	bits |= PPCBKE_PTE_ACCESS_R;
		if(data->prot & PROT_NOCACHE) {
			bits |= PPCBKE_PTE_ATTR_I;
			if(!(data->prot & PROT_EXEC)) {
				bits |= PPCBKE_PTE_ATTR_G;
			}
		}
	} else {
		bits = 0;
	} 

	flushed = 0;
	r = EOK;
	mpid = mempart_getid(object_from_data(adp, address_cookie), sys_memclass_id);
	for( ;; ) {
		if(data->start >= data->end) break;
		pdep = &l1pagetable[L1PAGEIDX(data->start)];
		if(*pdep == NULL) {
			memsize_t  resv = 0;

			if(!(data->op & (PTE_OP_MAP|PTE_OP_PREALLOC|PTE_OP_BAD))) {
				//Move vaddr to next page directory
				data->start = (data->start + PDE_SIZE) & ~(PDE_SIZE - 1);
				if(data->start == 0) data->start = ~0;
				continue;
			}
			if (MEMPART_CHK_and_INCR(mpid, __PAGESIZE, &resv) != EOK) {
				r = ENOMEM;
				break;
			}
			pq = pa_alloc(__PAGESIZE, __PAGESIZE, 0, 0, &pa_status, restrict_proc, resv);
			if(pq == NULL) {
				MEMPART_UNDO_INCR(mpid, __PAGESIZE, resv);
				r = ENOMEM;
				break;
			}
			pq->u.inuse.next = adp->cpu.l2_list;
			adp->cpu.l2_list = pq;
			l2_vaddr = pa_quantum_to_paddr(pq);
			if(pa_status & PAA_STATUS_NOT_ZEROED) {
				zero_page((uint32_t *)l2_vaddr, __PAGESIZE, NULL);
			}
			*pdep = (pte_t *)l2_vaddr;
		}
		if(data->op & PTE_OP_PREALLOC) {
			//Move vaddr to next page directory
			data->start = (data->start + PDE_SIZE) & ~(PDE_SIZE - 1);
			if(data->start == 0) data->start = ~0;
			continue;
		}
		pdep_l2base = *pdep;
		ptep = &(pdep_l2base[L2PAGEIDX(data->start)]);
		orig_flags = ptep->flags;
		if(data->op & (PTE_OP_MAP|PTE_OP_BAD)) {
			// temporarily unmap the entry so that we only
			// have a valid mapping once everything's filled in.
			ptep->flags = 0;
			new_paddr = MAKE_PTE_PADDR(data->paddr);
			if((orig_flags != 0) && (new_paddr != ptep->paddr)) {
				// Turn on a bit so that the test below will flush the vaddr
				orig_flags |= 0x80000000;
			}
			ptep->paddr = new_paddr;
			ptep->flags = bits;
		} else if(data->op & PTE_OP_UNMAP) {
			ptep->flags = 0;
			ptep->paddr = 0;
		} else if(ptep->flags & ~PPCBKE_PTE_ATTR_I) {
			ptep->flags = bits;
		}  else {
			// We don't change PTE permissions if we haven't mapped the
			// page yet...
		}
		if((orig_flags != 0) && (ptep->flags != orig_flags)) {
			flushed = 1;
			tlbop.flush_vaddr(data->start);
		}

		data->start += __PAGESIZE;
		data->paddr += __PAGESIZE;
		if((data->op & PTE_OP_PREEMPT) && KerextNeedPreempt()) {
			r = EINTR;
			break;
		}
	}
	if(flushed) {
		ppc_isync();
	}
	return r;
}

__SRCVERSION("fam_pte.c $Rev: 156323 $");
