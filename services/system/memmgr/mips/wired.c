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


struct wire_entry {
	uintptr_t			vaddr;
	uint32_t			lo0;
	uint32_t			lo1;
	uint32_t			pm;
};

static unsigned					wire_cookie;
static unsigned					wire_max;


void
wire_sync(ADDRESS *adp) {
	struct wire_entry	*we;
	unsigned			i;
	unsigned			asid;
	unsigned			cpu_bit;
	unsigned			cpu;

	cpu = RUNCPU;
	cpu_bit = 1 << cpu;
	asid = adp->cpu.asid[cpu] << TLB_HI_ASIDSHIFT;
	we = adp->cpu.wires;
	INTR_LOCK(&adp->cpu.slock);
	if(adp->cpu.pending_wire_deletes & cpu_bit) {
		adp->cpu.pending_wire_deletes &= ~cpu_bit;
		i = getcp0_wired();
		for( ;; ) {
			if(i == 0) break;
			--i;
			// Kill the entry by pointing into a set kseg0 location
			r4k_settlb(MIPS_R4K_K0BASE + (i * (__PAGESIZE*2)),
					0, 0, pgmask_4k, i << TLB_IDX_SHIFT);
		}
	}

	i = 0;
	for( ;; ) {
		if(i == wire_max) break;
		if(we->vaddr == VA_INVALID) break;
		r4k_settlb((we->vaddr & VPN_MASK(we->pm)) | asid,
				we->lo0, we->lo1, we->pm, i  << TLB_IDX_SHIFT);
		++we;
		++i;
	}
	setcp0_wired(i);
	INTR_UNLOCK(&adp->cpu.slock);
}


static void
wire_aspace(PROCESS *aspaceprp, PROCESS **paspaceprp) {
	vmm_aspace(aspaceprp, paspaceprp);
	wire_sync(aspaceprp->memory);
}


//We're going to hide a flag in the bottom bit of the we->vaddr field to
//indicate an entry that's permanently set, no matter what happens to the
//page tables after the initial setting (SHMCTL_GLOBAL on in the shm_ctl()
//flags indicates this). 
#define PERMANENT_FLAG	0x1

void
wire_check(struct mm_pte_manipulate *data) {
	struct wire_entry	*we;
	unsigned			i;
	uintptr_t			vaddr;
	pte_t				*pt;
	ADDRESS				*adp;
	unsigned			cpu;
	unsigned			mask;

	adp = data->adp;
	we = adp->cpu.wires;
	i = wire_max;
	vaddr = data->first & ~(__PAGESIZE*2-1);
	pt = &adp->cpu.pgdir[L1IDX(vaddr)][L2IDX(vaddr)];
	mask = VPN_MASK(pt->pm);
	for( ;; ) {
		if(i == 0) return;
		if(we->vaddr == VA_INVALID) break;
		if((vaddr & mask) == (we->vaddr & mask)) {
			if(!(we->vaddr & PERMANENT_FLAG) && 
			     ((we->lo0 != pt[0].lo) 
			  || (we->lo1 != pt[1].lo)
			  || (we->pm  != pt[0].pm))) {
				// Something's changed, have to update
				if(!((pt[0].lo | pt[1].lo) & TLB_VALID)) {
					// Entry deleted, shift stuff down
					--i;
					INTR_LOCK(&adp->cpu.slock);
					adp->cpu.pending_wire_deletes = LEGAL_CPU_BITMASK;
					memmove(&we[0], &we[1], i*sizeof(*we));
					we[i].vaddr = VA_INVALID;
					INTR_UNLOCK(&adp->cpu.slock);
					goto need_sync;
				}
				break;
			}
			return;
		}
		++we;
		--i;
	}

	if(!((pt[0].lo | pt[1].lo) & TLB_VALID)) {
		// We don't want to wire unmapped entries.
		return;
	}

	memmgr.aspace = wire_aspace;	
	we->lo0 = pt[0].lo;	
	we->lo1 = pt[1].lo;	
	we->pm = pt[0].pm;
	if(data->shmem_flags & SHMCTL_GLOBAL) {
		vaddr |= PERMANENT_FLAG;
	}
	// Assign 'vaddr' last so wire_sync() doesn't
	// try to load a partially set up entry.
	we->vaddr = vaddr;

need_sync:	
	cpu = RUNCPU;
	atomic_set(&adp->cpu.pending_wire_syncs, LEGAL_CPU_BITMASK & ~(1 << cpu));
	for(i = 0; i < NUM_PROCESSORS; ++i) {
		if(i != cpu) {
			SENDIPI(i, IPI_TLB_FLUSH);
		} else if((getcp0_tlb_hi() & TLB_HI_ASIDMASK) == adp->cpu.asid[i]) {
			wire_sync(adp);
		}
	}
}


void
wire_mcreate(PROCESS *prp) {
	ADDRESS				*adp = prp->memory;
	struct wire_entry	*wires;
	unsigned			i;
	
	adp->cpu.wires = wires = object_to_data(prp, wire_cookie);
	for(i = 0; i < wire_max; ++i) {
		wires[i].vaddr = VA_INVALID;
	}
}


void
wire_init(void) {
#if defined(VARIANT_r3k)
	// The rest of the code uses R4K features, so disable things
	// for R3K's.
	wire_max = 0;
#else
	// Don't allow wired entries to take more than one-eighth of the TLB
	wire_max = num_tlbs/8;
#endif	

	wire_cookie = object_register_data(&process_souls, wire_max*sizeof(struct wire_entry));
}

__SRCVERSION("wired.c $Rev$");
