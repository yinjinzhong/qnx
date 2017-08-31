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
//
// NOTE: The order of manipulation of the queued_event_priority and
//		intrevent_pending variables in the intrevent_add() and
//		intrevent_drain() routines are _extremely_ critical. Consider
//		the case on an SMP box where one CPU is in intrevent_add() while
//		the other is in intrevent_drain(). If you're not careful, you can
//		end up with queued_event_priority > 0 while intrevent_pending == NULL.
//		That can lead to bouncing around in __ker_exit without ever getting
//		out. Alternatively, you may miss draining the queue when you should.
//

#define INTERNAL_CONFIG_FLAG_MASKED	0x8000
static volatile int drain_active;
static volatile int	drain_last_mask;


/*
 * We've run out of intrevent structures. This means that some process 
 * has messed up its ISR. We'll find the errant process and kill it 
 * without mercy.
 * 
 * This code is actually bogus, since it doesn't really solve the
 * problem - the messed up ISR may not be adding any events to the
 * intrevent_pending queue, so it won't be found by scanning the
 * list. We'd need to keep track of active interrupts and find the
 * deepest nesting one that keeps on asserting when we re-enable interrupts
 * in the kernel interrupt exit processing, but I can't see any way
 * of doing that without slowing down the normal interrupt handling code,
 * which we don't want to do. It's also got race conditions - think
 * about what happens if another nested interrupt goes off while in
 * here and the code is re-entrantly started. Things to think about...
 *
 * Beginings of an idea. 
 *  In intrevent_add(), when get down to a critical number of
 *  free INTREVENT's (~20), turn on a flag. In the interrupt()
 *  processing loop, if that flag is on, mask any interrupt that
 *  occurs and set a flag on the INTERRUPT structure saying that
 *  it's been masked. Eventually the problem interrupt will be masked
 *  and forward progress will be made. Once we get into intrevent_drain(),
 *  and have drained all the pending events, check the global flag. If
 *  it's on, scan the INTERRUPT structures looking for ones that have
 *  been masked by the interrupt() loop. For each, clear a state flag,
 *  unmask the level and then set the state flag. In the interrupt() loop,
 *  more code counts the number of times it gets entered. If we get above
 *  a predetermined number (~100) without seeing the state flag gets set,
 *  we assume that this is the permanently asserted interrupt and
 *  remask it. All the processes with ISR's attached to that interrupt
 *  need to be killed. Where this has problems is with SMP, since the 
 *  interrupt() loop may be handled by a different CPU than the one
 *  that's doing intrevent_drain().
 
 */
static int
intrevent_error(THREAD *thp, INTERRUPT *isr) {
	INTREVENT	*curr_intr;
	PROCESS		*curr_proc;
	PROCESS		*high_intrs_proc;
	pid_t		curr_pid;
	int			high_intrs_count;
	INTRLEVEL	*intr_level;
	unsigned	intr_vector;
	INTREVENT	*irp;
	INTREVENT	*killer;

	/*
	 * First, run thru the pending interrupt list and 'mark' each process
	 * with the number of pending events.
	 */
	INTR_LOCK(&intrevent_lock);
	for(curr_intr = (INTREVENT *)intrevent_pending.head; curr_intr != NULL; curr_intr = curr_intr->next) {
		curr_intr->thread->process->pending_interrupts++;
	}
	INTR_UNLOCK(&intrevent_lock);

	/*
	 * Walk the process table and find the process with the most pending
	 * interrupts. Zero the list behind us (so we don't have to do another
	 * pass later).
	 */
	high_intrs_proc = NULL;
	high_intrs_count = 0;
	for(curr_pid = 2; curr_pid < process_vector.nentries; ++curr_pid) {
		if(VECP(curr_proc, &process_vector, curr_pid)) {
			if(curr_proc->pending_interrupts > high_intrs_count) {
				high_intrs_count = curr_proc->pending_interrupts;
				high_intrs_proc = curr_proc;
			}
			curr_proc->pending_interrupts = 0;
		}
	}

	intr_level = &interrupt_level[isr->level];
	intr_vector = intr_level->info->vector_base + isr->level - intr_level->level_base;
#define MIN_INTRS_BAD 10
	if(high_intrs_count < MIN_INTRS_BAD) {
		/* There wasn't enough pending interrupts on any particular process to justify
		 * canceling them.
		 */
		char	*name = thp->process->debug_name;

		if(name == NULL) name = "";
		kprintf("Out of interrupt events! (vector=%u process=%u [%s])\n", 
				intr_vector, thp->process->pid, name);

		if(ker_verbose >= 2) {
		/* debug assist information when of out interrupt events occurs */
			unsigned  i;
			INTREVENT * irplocal = (INTREVENT *)intrevent_pending.head;
#if defined(__GNUC__)
/* this enum order safe init is not supported by the WATCOM compiler */
	#define ARRAY_EL(e)	[(e)] =
#else
	#define ARRAY_EL(e)	 		
#endif
			static const char * const fmt[] =
			{
				ARRAY_EL(SIGEV_NONE) "t-%02u:  SIGEV_NONE(%u) -> %u (%s)  (--/--/--/--)\n",
				ARRAY_EL(SIGEV_SIGNAL) "t-%02u:  SIGEV_SIGNAL(%u) -> %u (%s)  (0x%x/0x%x/--/--)\n",
				ARRAY_EL(SIGEV_SIGNAL_CODE) "t-%02u:  SIGEV_SIGNAL_CODE(%u) -> %u (%s)  (0x%x/0x%x/0x%x/--)\n",
				ARRAY_EL(SIGEV_SIGNAL_THREAD) "t-%02u:  SIGEV_SIGNAL_THREAD(%u) -> %u (%s)  (0x%x/0x%x/0x%x/--)\n",
				ARRAY_EL(SIGEV_PULSE) "t-%02u:  SIGEV_PULSE(%u) -> %u (%s)  (0x%x/0x%x/0x%x/0x%x)\n",
				ARRAY_EL(SIGEV_UNBLOCK) "t-%02u:  SIGEV_UNBLOCK(%u) -> %u (%s)  (--/--/--/--)\n",
				ARRAY_EL(SIGEV_INTR) "t-%02u:  SIGEV_INTR(%u) -> %u (%s)  (--/--/--/--)\n",
				ARRAY_EL(SIGEV_THREAD) "t-%02u:  SIGEV_THREAD(%u) -> %u (%s)  (--/--/--/--)\n",
			};
			kprintf("Last %u:   event  ->   pid  (signo/val/code/pri)\n", 2 * MIN_INTRS_BAD);
			for (i=0; i<(2 * MIN_INTRS_BAD); i++, irplocal=irplocal->next)
				kprintf(fmt[SIGEV_GET_TYPE(&irplocal->event) % NUM_ELTS(fmt)],
							i+1, SIGEV_GET_TYPE(&irplocal->event),
							(irplocal->thread && irplocal->thread->process) ? irplocal->thread->process->pid : 0,
							(irplocal->thread && irplocal->thread->process) ? irplocal->thread->process->debug_name : "?",
							irplocal->event.sigev_signo, irplocal->event.sigev_value.sival_int,
							irplocal->event.sigev_code, irplocal->event.sigev_priority);
		}
		return 0;
	}


	/*
	 * Cancel all interrupts pending for that process.
	 */
	killer = NULL;
	INTR_LOCK(&intrevent_lock);
	curr_intr = (INTREVENT *)intrevent_pending.head;
	while(curr_intr != NULL) {
		irp = curr_intr;
		curr_intr = curr_intr->next;
		if(irp->thread->process == high_intrs_proc) {
			LINK3_REM(intrevent_pending, irp, INTREVENT);
			if(killer == NULL) {
				killer = irp;
			} else {
				LINK3_END(intrevent_free, irp, INTREVENT);
			}
			--queued_intrevents;
		}
	}

	if(killer == NULL) crash();
	killer->thread = high_intrs_proc->valid_thp;
	// Use a uniqe code that people can recognize
	SIGEV_SIGNAL_CODE_INIT(&killer->event, SIGKILL, 1, SI_IRQ);
    LINK3_BEG(intrevent_pending, killer, INTREVENT);

	INTR_UNLOCK(&intrevent_lock);

	if(high_intrs_proc == thp->process) {
		if(intr_vector != SYSPAGE_ENTRY(qtime)->intr) {
			// The current interrupt came from the failed process. Mask it.
			//NYI: There should be a tracevent for this....
			(void) interrupt_mask(isr->level, isr);
		}
	} 

	// Tell intrevent_add() to try again, we've freed stuff up.
	return 1;
}

//
// This routine is called from an interrupt handler.
// If it is safe, we do the work immediately.
// Otherwise we queue the event which will be acted
// on at the first safe moment. Probably when we are
// about to leave the microkernel.
//
// The low bit of *thp may be set to indicate intrevent_add was called from an IO interrupt. (Yes, it's ugly.) 
// use kermacros.c:  AP_INTREVENT_FROM_IO(thp), which sets the low bit, to pass the thp parm, if you're calling 
// intrevent_add from IO.
int intrevent_add_attr
intrevent_add(const struct sigevent *evp, THREAD *thp, INTERRUPT *isr) {
	INTREVENT		*irp;
	unsigned		inker;
	int		 		prio;
	struct sigevent	ev_copy; 


	//make events sent from IO interrupt critical, but dont clobber the user's sigevent 
	if(((uintptr_t)(thp)) & AP_INTREVENT_FROM_IO_FLAG) { 
		thp = (THREAD*) ((uintptr_t)(thp) & ~AP_INTREVENT_FROM_IO_FLAG); 
		if(intrs_aps_critical) {
			ev_copy = *evp; 
			SIGEV_MAKE_CRITICAL(&ev_copy);
			evp = &ev_copy; 
		}
	}; 
	
	inker = get_inkernel();

	// must be in an interrupt handler (or HOOK_TRACE for instrumented)
	if((inker & INKERNEL_INTRMASK) == 0) {
#if defined(VARIANT_instr)
		INTERRUPT	*itp;
		if((itp = interrupt_level[HOOK_TO_LEVEL(_NTO_HOOK_TRACE)].queue) == NULL) crash();
		if(itp->thread != thp) // Controls the crash() below
#endif
			crash();
	}

#if !defined(VARIANT_smp)
	//
	// If we were in user mode before, we can deliver the event right away.
	// We can't, however, allocate any memory. We might need to allocate
	// up to two pulses - one for the actual event being delivered, and
	// one for an UNBLOCK pulse to be delivered to the server that the first
	// thread is replied blocked on. We check for 4 available entries as
	// an extra security blanket.
	//
	if((inker == 1) && ((pulse_souls.total - pulse_souls.used) >= 4)) {
		int				status;

		// Probably safe to handle it right away (SIGEV_THREAD is a problem).
		if((status = sigevent_exe(evp, thp, 1)) >= 0) {
			return(status);
		}
	}
#endif

	for( ;; ) {
		INTR_LOCK(&intrevent_lock);

		// get a free event from the head of the list and fill it in
		irp = (INTREVENT *)intrevent_free.head;
		if(irp != NULL) break;
		if(ker_verbose >= 2) {
			DebugKDBreak();
		}
		INTR_UNLOCK(&intrevent_lock);
		if(!intrevent_error(thp, isr)) {
			return(-1);
		}
	}
    LINK3_REM(intrevent_free, irp, INTREVENT)

	irp->thread = thp;
	irp->event = *evp;

	// Keep track of the maximum queued event priority for pre-emption.
	if(++queued_intrevents > grow_intrevents) {
		if(drain_active) {
			int		level =	isr->level;

			// We're trying to drain the queue, don't let this interrupt
			// happen again until we're finished
			INTR_UNLOCK(&intrevent_lock);
			(void) interrupt_mask(level, NULL);
			INTR_LOCK(&intrevent_lock);
			interrupt_level[level].config |= INTERNAL_CONFIG_FLAG_MASKED;
			if(drain_last_mask < level) drain_last_mask = level;
		}
		// If we are using too many intrevents, try to preempt sooner
		// to drain the queue....
		prio = NUM_PRI-1;
	} else if(nopreempt) {
		prio = 0;
	} else if(SIGEV_GET_TYPE(evp) == SIGEV_PULSE && evp->sigev_priority >= 0) {
		prio = evp->sigev_priority;
	} else {
		prio = thp->priority;
	}

	if(queued_event_priority < prio) {
		queued_event_priority = prio;
	}

    // now put this event on the pending list (at the head)
    LINK3_BEG(intrevent_pending, irp, INTREVENT);
    INTR_UNLOCK(&intrevent_lock);

	return(0);
}

void intrevent_drain_attr
intrevent_drain_check() {
  if(intrevent_pending.tail != NULL) {
	  intrevent_drain();
  }
}

void intrevent_drain_attr
intrevent_drain(void) {
	INTREVENT		*irp=0;
	unsigned		saved_queue_num;
	int				level;
	THREAD			*thp;

	drain_last_mask = -1;
	drain_active = 1;

	// Skip the first part of the loop, since we don't have a previous
	// entry to free
	INTR_LOCK(&intrevent_lock);
	goto first;

	/*lint -e(827) turn off erroneous lint message for next line*/
	for(;;) {
		unsigned				id;

		INTR_LOCK(&intrevent_lock);

		// free up the previous irp
		LINK3_BEG(intrevent_free, irp, INTREVENT);

first:		
        irp = (INTREVENT *)intrevent_pending.tail;
        if(irp == NULL) break;

		// pluck off the tail of the pending list
		LINK3_REM(intrevent_pending, irp, INTREVENT);

		INTR_UNLOCK(&intrevent_lock);

		thp = irp->thread;
		if((id = (irp->event.sigev_notify & 0xffff0000))) {
			// Try and act on the event associated with this timer.
			overrun = 0;
			sigevent_exe(&irp->event, thp, 1);
			if(overrun) {
				TIMER	*tip;

				if((tip = vector_lookup(&thp->process->timers, (id >> 16) - 1))) {
					if(tip->overruns < DELAYTIMER_MAX - 1) {
						++tip->overruns;
					}
				}
			}
		} else {
			sigevent_exe(&irp->event, thp, 1);
		}

	} 
	// Nothing left in the queue.
	saved_queue_num = queued_intrevents;
	queued_intrevents = 0;
	queued_event_priority = 0;

	drain_active = 0;
	INTR_UNLOCK(&intrevent_lock);

	// If we hit 75% of the event pool grab some more.
	if(saved_queue_num > grow_intrevents) {
		if(ker_verbose >= 2) {
			kprintf("Grow intrevents by 25%%\n");
		}
		num_intrevents += intrevent_alloc(num_intrevents/4, 0);
		grow_intrevents = num_intrevents - num_intrevents / 4;
	}

	// intrevent_add() [might have] turned off some interrupts while we were 
	// draining the queue. Look through the list and unmask them now that 
	// we're done.
	for(level = drain_last_mask; level >= 0; --level) {
		if(interrupt_level[level].config & INTERNAL_CONFIG_FLAG_MASKED) {
			interrupt_level[level].config &= ~INTERNAL_CONFIG_FLAG_MASKED;
			(void) interrupt_unmask(level, NULL);
		}
	}
}


//
// This routine will call the low level primitive to execute a
// sigevent. The src may be past directly from an interrupt handler or
// from an event which was queued.
//
int rdecl
sigevent_exe(const struct sigevent *evp, THREAD *thp, int deliver) {
	CONNECT	*cop;
	CHANNEL	*chp;
	PROCESS	*prp;
	int		status;
	int		prio;

	switch(SIGEV_GET_TYPE(evp)) {
	case SIGEV_NONE:
		if(thp == procnto_prp->threads.vector[0]) {
			if(!(get_inkernel() & INKERNEL_NOW)) {
				/*
					If we're being invoked by an interrupt handler, we
					must queue the event - timer_pending() needs to be
					able to free memory.
				*/
				return -1;
			}
			// If the thread that signaled the event is the idle
			// thread, that was an indicator that we have pending
			// timer operations that need to be dealt with.
			timer_pending();
		}
		break;

	case SIGEV_SIGNAL_THREAD:
		if(evp->sigev_signo <= 0  ||  evp->sigev_signo > _SIGMAX  || evp->sigev_code > SI_USER) {
			return(EINVAL);
		}
		if(deliver) {
			prp = thp->process;
			(void)signal_kill_thread(prp,
					thp,
					evp->sigev_signo,
					evp->sigev_code,
					evp->sigev_value.sival_int,
					prp->pid,
					evp->sigev_notify & SIGEV_FLAG_CRITICAL ? SIGNAL_KILL_APS_CRITICAL_FLAG : 0 
					);
		}
		break;

	case SIGEV_SIGNAL:
		if(evp->sigev_signo <= 0  ||  evp->sigev_signo > _SIGMAX) {
			return(EINVAL);
		}
		if(deliver) {
			prp = thp->process;
			signal_kill_process(prp,
					evp->sigev_signo,
					SI_USER,
					evp->sigev_value.sival_int,
					prp->pid,
					evp->sigev_notify & SIGEV_FLAG_CRITICAL ? SIGNAL_KILL_APS_CRITICAL_FLAG : 0 
					);
		}
		break;

	case SIGEV_SIGNAL_CODE:
		if(evp->sigev_signo <= 0  ||  evp->sigev_signo > _SIGMAX  || evp->sigev_code > SI_USER) {
			return(EINVAL);
		}
		if(deliver) {
			prp = thp->process;
			signal_kill_process(prp,
					evp->sigev_signo,
					evp->sigev_code,
					evp->sigev_value.sival_int,
					prp->pid,
					evp->sigev_notify & SIGEV_FLAG_CRITICAL ? SIGNAL_KILL_APS_CRITICAL_FLAG : 0 
					);
		}
		break;

	case SIGEV_THREAD:
		if(thp->process->flags & _NTO_PF_DESTROYALL) {
			return EBUSY;
		}
		if(!(get_inkernel() & INKERNEL_NOW)) {
			/*
				If we're being invoked by an interrupt handler, we
				must queue the event - thread_create() might need to
				allocate memory.
			*/
			return -1;
		}
		if(!deliver || (status = thread_create(thp, thp->process, evp, 
			evp->sigev_notify & SIGEV_FLAG_CRITICAL ? THREAD_CREATE_APS_CRITICAL_FLAG : 0 )) == ENOERROR) {
			status = EOK;
		}
		return status;

	case SIGEV_PULSE:
		{
		VECTOR		*vec;
		unsigned	coid = evp->sigev_coid;

		prp = thp->process;
		vec = &prp->fdcons;
		if(coid & _NTO_SIDE_CHANNEL) {
			coid &= ~_NTO_SIDE_CHANNEL;
			vec = &prp->chancons;
		}

		if(vec && (coid < vec->nentries) && (VECAND(cop = VEC(vec, coid), 1) == 0)) {
			cop = VECAND(cop, ~3);
		} else {
			cop = NULL;
		}

		if(cop == NULL  ||  cop->type != TYPE_CONNECTION) {
			return(ESRCH);
		}

		if((chp = cop->channel) == NULL) {
			return(EBADF);
		}

		if(deliver) {
			//A -1 pulse priority means use the priority of the *process* of the receiver
			prio = (evp->sigev_priority == -1) ? chp->process->process_priority : evp->sigev_priority & _PULSE_PRIO_PUBLIC_MASK;
			_TRACE_COMM_EMIT_SPULSE_EXE(cop, cop->scoid | _NTO_SIDE_CHANNEL, prio);
			if ((status = pulse_deliver(chp, prio, evp->sigev_code, evp->sigev_value.sival_int, 
				cop->scoid | _NTO_SIDE_CHANNEL, 
				(evp->sigev_notify&SIGEV_FLAG_CRITICAL ? PULSE_DELIVER_APS_CRITICAL_FLAG : 0) ))) {
				return(status);
			}
		}
		break;
	}

	case SIGEV_UNBLOCK:
		if(deliver) {
			// a critical unblock event seems unlikely, but wth.
			if (evp->sigev_notify & SIGEV_FLAG_CRITICAL) AP_MARK_THREAD_CRITICAL(thp); 
			
			if(thp->state == STATE_NANOSLEEP) {
				ready(thp);
			} else {
				force_ready(thp, ETIMEDOUT);
			}
		}
		break;

	case SIGEV_INTR:
		if(deliver) {
			if(thp->state == STATE_INTR) {
				if (evp->sigev_notify & SIGEV_FLAG_CRITICAL) AP_MARK_THREAD_CRITICAL(thp); 
				ready(thp);
			} else {
				thp->flags |= _NTO_TF_INTR_PENDING;
			}
		}
		break;

	default:
		if(deliver && ker_verbose > 1) {
			kprintf("Pid %d: Invalid event queued (%d)\n", thp->process->pid, evp->sigev_notify);
		}
		return(EINVAL);
	}

	
	return(EOK);
}

//
// This routine is used by kernel routines to enqueue a sigevent to
// the process manager. It assumes the first thread is the idle thread
// and will always exist.
//
int rdecl
sigevent_proc(const struct sigevent *evp) {
	if(evp) {
#ifndef NDEBUG
if(SIGEV_GET_TYPE(evp) == SIGEV_PULSE && evp->sigev_priority == -1) crash();
#endif
		return sigevent_exe(evp, sysmgr_prp->threads.vector[0], 1);
	}

	return -1;
}

__SRCVERSION("nano_event.c $Rev: 153052 $");
