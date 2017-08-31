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


// Support routines for the kernel.
void rdecl
kererr(THREAD *thp, int err) {

	if(thp->type == TYPE_VTHREAD) {
		return;
	}

	if(get_inkernel() & INKERNEL_NOW) {
		lock_kernel();
	}

	// Last error wins unless KERERR_LOCK is set.
	// The last error winning is used when the condvar
	// code calls the mutex code so the condvar code
	// can override the error.
	// The lock is used in ker_connect_detach to allow
	// the kernel call to be exited then restarted
	// during network connection terminations.
	if((thp->flags & _NTO_TF_KERERR_LOCK) == 0) {
		SETKSTATUS(thp, err);
	}

	// Check for multiple errors in one kernel call.
	if(err != EOK && (thp->flags & _NTO_TF_KERERR_SET) == 0) {
		thp->flags |= _NTO_TF_KERERR_SET;
		thp->restart = NULL;
		timeout_stop(thp);
		SETKIP(thp, KIP(thp)+KERERR_SKIPAHEAD);
	}
}


void rdecl
kerunerr(THREAD *thp) {

	// Ignore unless an error is already set;
	thp->flags &= ~_NTO_TF_KERERR_LOCK;
	if(thp->flags & _NTO_TF_KERERR_SET) {
		thp->flags &= ~_NTO_TF_KERERR_SET;
		SETKIP(thp, KIP(thp)-KERERR_SKIPAHEAD);
	}
}


int rdecl
kerisroot(THREAD *thp) {

	if(thp->process->cred->info.euid == 0) {
		return(1);
	}

	kererr(thp, EPERM);
	return(0);
}


int rdecl
kerisusr(THREAD *srcthp, PROCESS *dstprp) {
	CREDENTIAL	*src = srcthp->process->cred;
	CREDENTIAL	*dst = dstprp->cred;

	if(src->info.euid == 0  ||
	   src->info.ruid == dst->info.ruid  ||
	   src->info.ruid == dst->info.suid  ||
	   src->info.euid == dst->info.ruid  ||
	   src->info.euid == dst->info.suid) {
		return(1);
	}

	kererr(srcthp, EPERM);
	return(0);
}

int rdecl
keriskill(THREAD *srcthp, PROCESS *dstprp, int signo) {
	if(signo == SIGCONT && dstprp->session == srcthp->process->session) {
		return(1);
	}
	return kerisusr(srcthp, dstprp);
}

int rdecl
kerschedok(THREAD *thp, int policy, const struct sched_param *param) {
	if(param->sched_priority < 0 || param->sched_priority >= NUM_PRI) return EINVAL;
	if(param->sched_priority == 0 && (thp->process->flags & (_NTO_PF_LOADING | _NTO_PF_RING0)) != _NTO_PF_RING0) return EINVAL;
	if(thp->process->cred->info.euid != 0 && param->sched_priority >= priv_prio) return EPERM;
	if(policy < SCHED_NOCHANGE || policy > SCHED_SPORADIC) return EINVAL;
	if(policy != SCHED_SPORADIC) return EOK;
	if(param->sched_ss_max_repl < 1 || param->sched_ss_max_repl > SS_REPL_MAX) return EINVAL;
	if(param->sched_ss_low_priority <= 0 || param->sched_ss_low_priority > param->sched_priority) return EINVAL;
	if(timespec2nsec(&param->sched_ss_repl_period) < timespec2nsec(&param->sched_ss_init_budget)) return EINVAL;
	return EOK;
}

int
kercall_attach(unsigned callnum, int kdecl (*func)()) {

	if(callnum >= __KER_BAD)
		return(-1);

	ker_call_table[callnum] = func;
#if defined(VARIANT_instr)
	_trace_ker_call_table[callnum] = func;
	_trace_call_table[callnum] = func;
#endif

	return(0);
}

/*
 * This function is there to reference
 * the get_cpunum() and send_ipi stub functions
 * in the non-SMP kernel. This is needed for linking optional
 * modules such as APS.
 */
void __reference_smp_funcs(void) {
	(void)get_cpunum();
	send_ipi(0, 0);
}

// Can be set to one by the kernel debugger so we can continue on
// from a crash() message
int kdebug_walkout;

void rdecl
(crash)(const char *file, int line) {

	InterruptDisable();
	kprintf("Crash[%d,%d] at %s line %d.\n", RUNCPU, KERNCPU, file, line);
	for(;;) {
		InterruptDisable();
		DebugKDBreak();
		if(privateptr->kdebug_call == NULL) {
			RebootSystem(1);
		}
		if(kdebug_walkout) break;
	}
}

__SRCVERSION("nano_misc.c $Rev: 153052 $");
