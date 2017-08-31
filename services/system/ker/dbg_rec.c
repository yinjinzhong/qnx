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

// This file is only used for kernel debugging purposes

//NYI: We probably should have a common API for saving and restoring
//interrupt state. Something to do later...
#if defined(__ARM__)
	//NYI: dunno how to implement
	#define intr_state()	0
	#define intr_restore(p)
#elif defined(__MIPS__)
	#define intr_state()	getcp0_sreg()
	#define intr_restore(p)	setcp0_sreg(p)
#elif defined(__PPC__)
	#define intr_state()	get_msr()
	#define intr_restore(p)	set_msr(p)
#elif defined(__SH__)
	//NYI: dunno how to implement
	#define intr_state()	0
	#define intr_restore(p)
#elif defined(__X86__)
	#define intr_state()	pswget()
	#define intr_restore(p)	restore(p)
#else
	#error CPU not supported
#endif

#define RSIZE 0x100
int rbuff[RSIZE];
unsigned ridx;

static 
#if defined(__GNUC__)
inline 
#endif
unsigned
get_ridx() {
	unsigned 	result;
	unsigned	new;

#if defined(VARIANT_smp)	
retry:
#endif
	result = ridx;
	new = (result+1) & (RSIZE-1);
#if defined(VARIANT_smp)	
	if(_smp_cmpxchg(&ridx, result, new) != result) goto retry;
#else
	ridx = new;
#endif
	return(result);
}
			
void
rec(int num) {
	unsigned prev;

	prev = intr_state();
	InterruptDisable();
	rbuff[get_ridx()] = num;
	intr_restore(prev);
}
			
void
rec_who(int num) {
	unsigned			my_cpu;
	unsigned			prev;

	my_cpu = RUNCPU;
	prev = intr_state();
	InterruptDisable();
	rbuff[get_ridx()] = num;
	rbuff[get_ridx()] = (my_cpu << 28) + ((actives[my_cpu]->process->pid & 0xffff) << 4) + actives[my_cpu]->tid;
	intr_restore(prev);
}

void
rec_who2(int num, int num2) {
	unsigned			my_cpu;
	unsigned			prev;
	PROCESS				*prp;

	my_cpu = RUNCPU;
	prp = actives[my_cpu]->aspace_prp;
	if(prp == NULL) prp = actives[my_cpu]->process;
	prev = intr_state();
	InterruptDisable();
	rbuff[get_ridx()] = num;
	rbuff[get_ridx()] = num2;
	rbuff[get_ridx()] = (my_cpu << 28) + ((prp->pid & 0xffff) << 4) + actives[my_cpu]->tid;
	intr_restore(prev);
}

void
rec_who3(int num, int num2, int num3) {
	unsigned			my_cpu;
	unsigned			prev;
	PROCESS				*prp;

	my_cpu = RUNCPU;
	prp = actives[my_cpu]->aspace_prp;
	if(prp == NULL) prp = actives[my_cpu]->process;
	prev = intr_state();
	InterruptDisable();
	rbuff[get_ridx()] = num;
	rbuff[get_ridx()] = num2;
	rbuff[get_ridx()] = num3;
	rbuff[get_ridx()] = (my_cpu << 28) + ((prp->pid & 0xffff) << 4) + actives[my_cpu]->tid;
	intr_restore(prev);
}

__SRCVERSION("dbg_rec.c $Rev: 153052 $");
