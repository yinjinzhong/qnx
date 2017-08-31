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

#undef FILE_DEBUG
/* #define FILE_DEBUG */


// used with walk_asinfo to find the first baseaddr-TMU record
static int find_base_address( struct asinfo_entry* as, char* name, void* variable_addr)
{
    *((paddr_t *)variable_addr) = (paddr_t)as->start;
    return 0;  // Return 0 so we stop searching after the first match.
}

/*
 * CPU specific initialization of syspage pointers & stuff
 */
void
cpu_syspage_init(void) {
	struct cpuinfo_entry	*cpu;
	uint16_t	*kc_addr;
	uint16_t	temp;
	int			i;

	cpu = SYSPAGE_ENTRY(cpuinfo);
	if(cpu->flags & CPU_FLAG_MMU) {
		privateptr->user_cpupageptr = (void *)SYSP_ADDCOLOR(VM_CPUPAGE_ADDR, SYSP_GETCOLOR(_cpupage_ptr));
		privateptr->user_syspageptr = (void *)SYSP_ADDCOLOR(VM_SYSPAGE_ADDR, SYSP_GETCOLOR(_syspage_ptr));
		for (i = 0; i < _syspage_ptr->num_cpu; i++) {
			cpupageptr[i]->syspage = (void *)SYSP_ADDCOLOR(VM_SYSPAGE_ADDR, SYSP_GETCOLOR(_syspage_ptr));
		}
		kercallptr = (void *)((unsigned)privateptr->user_syspageptr /*VM_SYSPAGE_ADDR*/
				+ ((unsigned)kercallptr - (unsigned)SH_P1_TO_PHYS(_syspage_ptr)));
	} else {
		privateptr->user_cpupageptr = (void *)_cpupage_ptr;
		privateptr->user_syspageptr = (void *)_syspage_ptr;
		kercallptr = (void *)SH_PHYS_TO_P1
				((uintptr_t)kercallptr);
	}


	/*
	 * Set up the code used to force kernel calls.
	 *
	 * Startup has initialised the first two instructions:
	 * kc_addr[0] = trapa
	 * kc_addr[1] = 0xfffd
	 *
	 * We set these up to generate the correct system calls:
	 *
	 * kc_addr[0] = [kercall+0] trapa #__KER_THREAD_DESTROYALL
	 * kc_addr[1] = [kercall+2] 0xfffd
	 *
	 * For SMP, we need to force a __KER_NOP system call, so we use the
	 * 2nd set of instruction locations (not initialised by startup):
	 *
	 * kc_addr[2] = [kercall+4] trapa #__KER_NOP
	 * kc_addr[3] = [kercall+6] 0xfffd
	 */
	kc_addr = (uint16_t *)&SYSPAGE_ENTRY(system_private)->kercall;
	temp = kc_addr[0];
	temp = (temp & 0xff00) | __KER_THREAD_DESTROYALL;
	kc_addr[0] = temp;
	icache_flush(&kc_addr[0]);
#ifdef	VARIANT_smp
	kc_addr[2] = (temp & 0xff00) | __KER_NOP;
	kc_addr[3] = kc_addr[1];
	icache_flush(&kc_addr[2]);
#endif

	/*
	 * Set up per-cpu shadow_imask location:
	 * - for uniprocessor, use _syspage_ptr->un.sh.imask
	 * - for multiprocessor, use _cpupage_ptr->un.sh.imask
	 */
	if (_syspage_ptr->num_cpu > 1) {
		for (i = 0; i < _syspage_ptr->num_cpu; i++) {
			cpupageptr[i]->un.sh.imask = 0;
			__cpu_imask[i] = &cpupageptr[i]->un.sh.imask;
		}
		__shadow_imask = &privateptr->user_cpupageptr->un.sh.imask;
	}
	else {
		_syspage_ptr->un.sh.imask = 0;
		__cpu_imask[0] = &_syspage_ptr->un.sh.imask;
		__shadow_imask = &_syspage_ptr->un.sh.imask;
	}

    /*
     * Set __cpu_flags atomic/smp features in case startup didn't
     */
    if (_syspage_ptr->num_cpu > 1) {
    	cpu->flags |= SH_CPU_FLAG_SMP;
    }
    if (SH4_PVR_FAM(SYSPAGE_ENTRY(cpuinfo)->cpu) == SH4_PVR_SH4A) {
    	cpu->flags |= SH_CPU_FLAG_MOVLICO;
    }
    __cpu_flags = cpu->flags;
    
    /*
     * Note hardware base addresses given to us from startup.  Assign
     * default values as appropriate, in case we're using an older startup
     * that doesn't create the asinfo records.
     */
    sh_mmr_tmu_base_address = SH4_MMR_TMU_BASE_ADDRESS;
    walk_asinfo("baseaddr-TMU", find_base_address, &sh_mmr_tmu_base_address);
}

/*
 * CPU specific stuff for initializing a thread's registers
 */
void
cpu_thread_init(THREAD *act, THREAD *thp, int align) {

#ifdef FILE_DEBUG 
kprintf("CPU thread init. act=%x, thp=%x, pc=%x\n",actives[KERNCPU],thp, thp->reg.pc );
#endif

	thp->reg.sr = SH_SR_FD;

	if(act != NULL) {
		thp->reg.sr |= (act->reg.sr & SH_SR_MD);
	} else {
		/* procmgr thread */
		thp->reg.sr |= SH_SR_MD;
	}
	switch(align) {
	case 0:
	case 1:
		thp->flags |= _NTO_TF_ALIGN_FAULT;
		break;
	default: break;
	}

}

/*
 * CPU specific stuff for putting a thread into execution for the
 * first time. We have to arrange a stack frame such that the function
 * thinks it needs to return to thp->args.wa->exitfunc and that it has
 * been passed thp->args.wa.arg.
 */
void
cpu_thread_waaa(THREAD *thp) {
	thp->reg.pr = (uintptr_t)thp->un.lcl.tls->__exitfunc;
	thp->reg.gr[4] = (uintptr_t)thp->un.lcl.tls->__arg;
}

/*
 * CPU specific stuff for allowing a thread to do priviledged operations (I/O priviledge)
 */
void
cpu_thread_priv(THREAD *thp) {
	/* Nothing needed */
}

/*
 * CPU specific stuff for destroying a thread
 */
void
cpu_thread_destroy(THREAD *thp) {
	/* Nothing needed */
}

/*
 * CPU specific stuff for allowing/disallowing alignment faults on a thread
 */
void
cpu_thread_align_fault(THREAD *thp) {
	/* Nothing needed for SH */
}

/*
 * CPU specific stuff for saving registers on signal delivery
 */
void
cpu_signal_save(SIGSTACK *ssp, THREAD *thp) {
	ucontext_t	*uc;

	uc = ssp->info.context;
	uc->uc_mcontext.cpu.gr[14] 	= thp->reg.gr[14];
	uc->uc_mcontext.cpu.gr[4] 	= thp->reg.gr[4];
	uc->uc_mcontext.cpu.gr[3] 	= thp->reg.gr[3];
	uc->uc_mcontext.cpu.gr[2] 	= thp->reg.gr[2];
	uc->uc_mcontext.cpu.gr[1] 	= thp->reg.gr[1];
	uc->uc_mcontext.cpu.gr[0] 	= thp->reg.gr[0];
	uc->uc_mcontext.cpu.sr		= thp->reg.sr;
}

/*
 * CPU specific stuff for restoring from a signal
 */
void
cpu_signal_restore(THREAD *thp, SIGSTACK *ssp) {
	ucontext_t	*uc;

	uc = ssp->info.context;
	thp->reg.gr[14]	= uc->uc_mcontext.cpu.gr[14];
	thp->reg.gr[4]	= uc->uc_mcontext.cpu.gr[4];
	thp->reg.gr[3]	= uc->uc_mcontext.cpu.gr[3];
	thp->reg.gr[2]	= uc->uc_mcontext.cpu.gr[2];
	thp->reg.gr[1]	= uc->uc_mcontext.cpu.gr[1];
	thp->reg.gr[0] 	= uc->uc_mcontext.cpu.gr[0];
	thp->reg.sr		= uc->uc_mcontext.cpu.sr;
}

/*
 * CPU specific stuff for saving registers needed for interrupt delivery
 */
void
cpu_intr_attach(INTERRUPT *itp, THREAD *thp) {
	/* Nothing needed for SH */
}

/*
 * This copies the register sets, but makes sure there are no
 * security holes
 */
void cpu_greg_load(THREAD *thp, CPU_REGISTERS *regs) {
	uint32_t		sr = thp->reg.sr;

	thp->reg = *regs;
	/*
	 * Don't restore MSR_PR, it is a security hole
	 */
	thp->reg.sr = (regs->sr & ~SH_SR_MD) | (sr & SH_SR_MD);
}

/*
 * CPU specific stuff for initializing a process's registers
 */
void
cpu_process_startup(THREAD *thp, int forking) {
	if(!forking) {
		thp->reg.sr = SH_SR_FD;

		/* use the mips protocal to pass process startup args */
		/*
		 * Set atexit() pointer to inactive
		 */
		thp->reg.gr[0] = thp->reg.gr[4] = 0;				/* arg atexit */
		thp->reg.gr[5] = thp->reg.gr[15]; 					/* args */

		thp->reg.pr = 0;
	}
}

/*
 * CPU specific stuff for cleaning up before rebooting
 */
void
cpu_reboot(void) {
}


/*
 * cpu_start_ap(void)
 *	CPU specific stuff for getting next AP in an SMP system initialized
 *	and into the kernel.
 */
void
cpu_start_ap(uintptr_t start) {
	if (_syspage_ptr->smp.entry_size > 0) {
		SYSPAGE_ENTRY(smp)->start_addr = (void (*)())start;
	}
}

/*
 * CPU specific code to force a thread to execute the destroyall kernel
 * call.
 */
void
cpu_force_thread_destroyall(THREAD *thp) {
	void		*kc_addr;

#ifdef FILE_DEBUG 
kprintf("CPU force thread destroyall. thp=%x,sr=%x\n",thp,thp->reg.sr);
#endif

	if((thp->reg.sr & SH_SR_MD) == 0) {
		SETKIP(thp, kercallptr);
	} else {
		kc_addr = &SYSPAGE_ENTRY(system_private)->kercall;
		SETKIP(thp, kc_addr);
	}
#ifdef FILE_DEBUG 
kprintf("CPU force thread destroyall. addr=%x,inst=%x\n",kc_addr,*(uint16_t*)kc_addr);
#endif
}

void cpu_mutex_adjust(THREAD *act) {
 	
  	if(KSP(act) & 1) {
 		// possible mutex operation
 		uintptr_t ip, up, low;
 
 		ip = KIP(act);
 		low = act->reg.gr[0];
 		up = low + act->reg.gr[1];
 		if(ip>=low && ip<up) {
			if(up - low < 20) {
				//mutex implementation should be short
 				SETKIP(act, low);
			}
 		}
 	}
}
 

__SRCVERSION("cpu_misc.c $Rev: 156873 $");
