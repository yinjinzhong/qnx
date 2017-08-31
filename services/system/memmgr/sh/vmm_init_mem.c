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



/* 
 * TLB in32/out32 routines
 * 
 * The following routines are used to initialize the sh_tlb_in32 and
 * sh_tlb_out32 function pointers.
 * 
 * They differ in that for SH-4a architectures, we use the normal
 * in32/out32 routines.  For SH-4 architectures, the mov instruction
 * that does the in or out must run from the P2 area, and we must
 * delay for 8 instructions after the move before we branch out of
 * the P2 area.  To satisfy these requirements for SH-4, we assign
 * the function pointer with the P2-equivalent of the function
 * address, and we add some NOPs before we return from the routine.
 * 
 * The SH-4a documentation also says that it is necessary to run out
 * of the P2 area, but consultation with Renesas confirms it isn't
 * necessary.  For speed, we will run the SH-4a routine out of the
 * P1 area so that it is cached.
 *
 * We could use the SH-4 routine for the SH-4a but run it out of 
 * the P1 area, but the extra NOPs will still slow the SH-4a down 
 * unnecessarily.  We'll keep the routines separate in order to 
 * improve the performance of the 4a at the expense of a few extra 
 * bytes of code.
 */
 
unsigned sh4a_tlb_in32(unsigned p) {
	return in32(p);
}
void sh4a_tlb_out32(unsigned p, unsigned v) {
	out32(p,v);
}
unsigned sh4_tlb_in32(unsigned p) {
	unsigned v;
	__asm__ __volatile__(
		"	mov.l	@%1,%0;"
		"	nop;nop;nop;nop;"  // h/w manual says we can't branch out of the p2 area for 8
		"	nop;nop;nop;nop;"  // instructions after the mov (7751 h/w manual, section 3.7)
		:"=&r" (v)
		: "r" (p)
		: "pr", "memory"
	);
	return v;
}
void sh4_tlb_out32(unsigned p, unsigned v) {
	__asm__ __volatile__(
		"	mov.l	%1,@%0;"
		"	nop;nop;nop;nop;"   // h/w manual says we can't branch out of the p2 area for 8
		"	nop;nop;nop;nop;"   // instructions after the mov (7751 h/w manual, section 3.7)
		: 
		: "r" (p), "r" (v)
		: "pr", "memory"
	);
}



/*
 * For the SH, we want our address space to look like what is below:
 *
 *   TOP  ->			0xffffffff
 *	 System			|   0x80000000
 *	 reserved area		0x7bffffff
 *		CPU page
 *		Sys page	|
 *	 shared libs		0x7bff0000
 *   				|
 *					V
 *						0x70300000
 *					^
 *					|
 *   Shared objects		0x38100000
 *					^
 *					|	("brk")
 *   Heap			|
 * 					^
 *	BSS				|
 *	Data			|
 *	Text			|
 *   Process image		0x08020000
 *   Stack			|
 *					V
 * Thread stacks	|
 *					V
 *	 System				0x00000000
 *
 *	Note: P2 area is for inkernel i/o mapping only, since it does not 
 *  have any cache.
 *	
 */

void
vmm_init_mem(int phase) {
	uint32_t						sh_pvr;
	int 							colour_size;
	int 							ccr;
	struct cacheattr_entry			*cache;
	unsigned 						ptel;
	unsigned 						pteh;
	struct system_private_entry		*pp;
	struct cpupage_entry			*cpupage;
	extern void 					copy_vm_code(void);

	/*
	 * Called with phase as follows:
	 *
	 * phase 0: from kmain() to initialise MMU for boot cpu:
	 *          - set up MMU
	 *          - initialise physical memory allocator
	 *
	 * phase 1: from memmgr_init():
	 *          - initialise background processing
	 *
	 * phase 2: from init_smp() to initialise MMU for secondary cpus:
	 *          - set up MMU
	 */
	switch (phase) {
	case 1:
		pa_start_background();
		fault_init();
		lock_init();
		break;

	case 0:
		cache = &SYSPAGE_ENTRY(cacheattr)[SYSPAGE_ENTRY(cpuinfo)->data_cache];
		if(cache->next != CACHE_LIST_END) {
			mm_flags |= MM_FLAG_MULTIPLE_DCACHE_LEVELS;
		}
		colour_size = (cache->line_size * cache->num_lines)/__PAGESIZE;

		if (cache->ways == 0) {
			// cache->ways wasn't defined by the startup.  Figure it out the hard way...
			sh_pvr = SH4_PVR_MASK(in32(SH_MMR_PVR));
	  
			switch(sh_pvr) {
			case SH4_PVR_7750_2_4:
			case SH4_PVR_7750_2_5:
			case SH4_PVR_7750S:
			case SH4_PVR_7751_1_2:
			case SH4_PVR_7751_3_4_5: 	// All these have 1 way cache
				break;
			case SH4_PVR_7751R:			// 7750R, 7751R, 7760 have 2 way cache
			case SH4_PVR_7760:
				colour_size >>= 1;
				break;
			case SH4_PVR_7780:			// 7780 has a 4 way cache
			case SH4_PVR_7780_2:			// 7780 has a 4 way cache
			case SH4_PVR_7770:			// 7770 has a 4 way cache
				colour_size >>= 2;
				break;
			case SH4_PVR_X3P:
				colour_size = 1;		// FIXME: assumes CCR.SNM is set
				break;
			default:					// Everything else must be un-supported
				//put here to reduce risk for SP2. will be moved out in 6.4.
				if(SH4_PVR_FAM(sh_pvr) == SH4_PVR_SH4A) {
						colour_size >>= 2;      // sh4a has a 4 way cache
						break;
				}	
				kprintf("Unsupported CPU Version (PVR=0x%x)\n", sh_pvr );
				crash();
				break;
			}
		} else {
			// The startup told us the cache attributes
			colour_size /= cache->ways;
		}

		// Initialize the tlb in32/out32 function pointers.	
		if(SH4_PVR_FAM(in32(SH_MMR_PVR)) == SH4_PVR_SH4A) {
			sh_tlb_in32 = sh4a_tlb_in32;
			sh_tlb_out32 = sh4a_tlb_out32;
		} else {
			// assign the P2 addresses of the function pointers to ensure that
			// the routines run out of the p2 area (see 7751 h/w manual, section 3.7)
			sh_tlb_in32 = (void*)SH_PHYS_TO_P2( SH_P1_TO_PHYS( sh4_tlb_in32 ) );
			sh_tlb_out32 = (void*)SH_PHYS_TO_P2( SH_P1_TO_PHYS( sh4_tlb_out32 ) );
		}
  
		pa_init(colour_size);

		copy_vm_code();

		cpu_pte_init();

		/* init perm map */
		if(_syspage_ptr->un.sh.flags & SH_SYSPAGE_FLAGS_PERMANENT_MAP) {
			perm_map_init();
		}

		/* FALL_THRU */

	case 2:
		if(((ccr = in32(SH_MMR_CCN_CCR)) & (SH_CCN_CCR_ICE | SH_CCN_CCR_OCE)) != (SH_CCN_CCR_ICE | SH_CCN_CCR_OCE)) {
			// FIXME: why mask here? Will remove SMP bits?
			ccr = ccr & (SH_CCN_CCR_ICE | SH_CCN_CCR_OCE | SH_CCN_CCR_CB);
			if(!(ccr & SH_CCN_CCR_OCE)) {
				_p2_out32(SH_MMR_CCN_CCR, in32(SH_MMR_CCN_CCR) | SH_CCN_CCR_OCI);
				ccr |= SH_CCN_CCR_CB | SH_CCN_CCR_OCE;
			}
			if(!(ccr & SH_CCN_CCR_ICE)) {
				_p2_out32(SH_MMR_CCN_CCR, in32(SH_MMR_CCN_CCR) | SH_CCN_CCR_ICI);
				ccr |= SH_CCN_CCR_ICE;
			}
			if(SH4_PVR_MASK(in32(SH_MMR_PVR)) == SH4_PVR_7751R) {
				// 7751R
				ccr |= SH_CCN_CCR_EMODE;
			} 
			_p2_out32(SH_MMR_CCN_CCR, ccr);
		}

		// ASID:  kernel 
		out32(SH_MMR_CCN_PTEH, (in32(SH_MMR_CCN_PTEH) & ~SH_CCN_PTEH_ASID_M) | SH_CCN_PTEH_ASID(0));

		// MMUCR
		out32(SH_MMR_CCN_MMUCR, SH_CCN_MMUCR_AT /*| SH_CCN_MMUCR_URB(30)*/);

		// flush all itlb and utlb entries
		tlb_flush_all();

		// reserve tlbs for user sys and cpu page
		ptel = SH_CCN_PTEL_V | SH_CCN_PTEL_SZ0 | SH_CCN_PTEL_SH | SH_CCN_PTEL_PR(2) | SH_CCN_PTEL_C;
		out32(SH_MMR_CCN_MMUCR, SH_CCN_MMUCR_AT | SH_CCN_MMUCR_URC(63));
		pteh = REAL_ADDR(SYSP_ADDCOLOR(VM_SYSPAGE_ADDR, SYSP_GETCOLOR(_syspage_ptr)));
		out32(SH_MMR_CCN_PTEH, pteh);
		out32(SH_MMR_CCN_PTEL, ptel | REAL_ADDR((paddr_t)SH_P1_TO_PHYS(_syspage_ptr)));
		load_tlb();
		
		/*
		 * Each CPU has its own cpupage
		 */
		pp = SYSPAGE_ENTRY(system_private);
		cpupage = (struct cpupage_entry *)((uintptr_t)pp->kern_cpupageptr + (RUNCPU * pp->cpupage_spacing));
		out32(SH_MMR_CCN_MMUCR, SH_CCN_MMUCR_AT | SH_CCN_MMUCR_URC(62));
		pteh = REAL_ADDR(SYSP_ADDCOLOR(VM_CPUPAGE_ADDR, SYSP_GETCOLOR(cpupage)));
		out32(SH_MMR_CCN_PTEH, pteh);
		out32(SH_MMR_CCN_PTEL, ptel | REAL_ADDR((paddr_t)SH_P1_TO_PHYS(cpupage)));
		load_tlb();

		out32(SH_MMR_CCN_MMUCR, SH_CCN_MMUCR_AT | SH_CCN_MMUCR_SQMD | SH_CCN_MMUCR_URB(64-2));

		break;
	}
}

__SRCVERSION("vmm_init_mem.c $Rev: 153052 $");
