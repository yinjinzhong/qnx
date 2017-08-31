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

#ifndef NDEBUG

static void rdecl ready_default(THREAD *thp);

static void
chk_dispatch(int chkstate) {
	int i;
	THREAD *thp;
	THREAD **owner;
	DISPATCH *dpp;
	THREAD	*act;

	if(ker_verbose < 3) {
		return;
	}

	//This version of chk_dispatch crashes on APS loads. Some of its assertions are incorreect for aps. PR28307
	//This is a temporary patch to make debug APS loads not crash for the beta
	if (ready_default != ready) return;
	
	act = actives[KERNCPU];
	if(act->process == NULL) crash();
	dpp = act->dpp;
	if(dpp == NULL) crash();
	if(chkstate  &&  act->state != STATE_RUNNING) crash();
	if(!DISPATCH_ISSET(dpp, 0) || (DISPATCH_THP(dpp, 0) == NULL)) {
		i = 0;
		for( ;; ) {
			if(i >= NUM_PROCESSORS) {
				// If all the idles are initialized (have set prio==0),
				// and the active thread isn't an idle, we should have
				// something on the prio==0 ready list.
				if(act->priority == 0) break;
				crash();
			}
			thp = procnto_prp->threads.vector[i];
			if(thp->priority != 0) break;
			++i;
		}
	}
	for(i = 0 ; i < NUM_PRI ; ++i) {
		if(DISPATCH_ISSET(dpp, i)) {
			owner = (THREAD **)&DISPATCH_LST(dpp, i).head;
			if(*owner == NULL) {
				crash();
			}

			for( ;; ) {
				thp = *owner;
				if(thp == NULL) break;
				if(!thp->process) {
					crash();
				}
				if(thp->priority != i) {
					crash();
				}
				if(thp->state != STATE_READY && (chkstate || thp != act)) {
					crash();
				}
				if(thp->type != TYPE_THREAD) {
					crash();
				}
				if(thp->prev != owner) {
					crash();
				}
				owner = &thp->next;
			}
		} else {
			if(DISPATCH_THP(dpp, i) != NULL) {
				crash();
			}
		}
	}
}

#define chk_cond(cond)	if(!(cond)) crash();

#else

#define chk_dispatch(chkstate)	

#define chk_cond(cond)	

#endif

#define chk_lock() 			chk_cond((get_inkernel() & (INKERNEL_NOW | INKERNEL_LOCK)) != INKERNEL_NOW)
#define chk_thread(thp)		chk_cond((thp)->type == TYPE_THREAD)

static void rdecl mark_running_default(THREAD *new);

#if defined(VARIANT_smp)

#define VALIDATE_NEEDTORUN(thp) \
		if (need_to_run && (need_to_run_cpu == KERNCPU || need_to_run == thp)) { \
			need_to_run = NULL; \
		}

static int rdecl
select_cpu(THREAD *thp) {
	THREAD			*act;
	int				i, j, cpu;
	unsigned		minprio;
	unsigned		alternates[PROCESSORS_MAX];

	for(i = NUM_PROCESSORS - 1 , j = 0, cpu = -1, minprio = thp->priority ; i >= 0 ; --i) {
		if((thp->runmask & (1 << i)) == 0) {
			act = actives[i];
			if(act->priority < minprio) {
				j = 0;
				cpu = i;
				minprio = act->priority;
			} else if(act->priority == minprio) {
				alternates[j++] = i;
			}
		}
	}

	// If we found a cpu make sure it is the best one in the case of
	// multiple choices!
	if(cpu != -1  &&  thp->runcpu != cpu  &&  j) {
		while(j) {
			int		new_cpu;

			new_cpu = alternates[--j];
			if(new_cpu == thp->runcpu) {
				cpu = new_cpu;
			}
		}
	}

	return cpu;
}

/*
 * Select_thread function for SMP.
 * Given act, cpu and prio, returns a thread pointer if a candidate equal
 * or better to act can be found (if act is non-NULL). 
 *
 * Note that act, if non-NULL, is assumed to be RUNNING and will not be taken
 * off any queue.
 */ 
static THREAD * rdecl
select_thread_default(THREAD *act, int cpu, int prio) {
	THREAD		*thp;
	uint32_t	runmask;
	int         hipri;
	// act may be NULL so we get the dpp from actives
	DISPATCH	*dpp = actives[KERNCPU]->dpp;

	// Get pointer to highest priority queue.
	hipri = DISPATCH_HIGHEST_PRI(dpp);

	runmask = 1 << cpu;

	for(; hipri >= prio && hipri > -1; hipri--) {
		// Run down this priority queue looking for a thread which will
		// run on this processor.
		
		for(thp = DISPATCH_THP(dpp, hipri); thp; thp = thp->next) {
			if((thp->runmask & runmask) == 0) {
				if((int)(unsigned)thp->priority >= prio) {

					LINK3_REM(DISPATCH_LST(dpp,thp->priority), thp, THREAD);

					// If it was alone on this ready queue clear the readybit.
					if(DISPATCH_THP(dpp, thp->priority) == NULL) {
						DISPATCH_CLR(dpp, thp);
					}
				}

				return thp;
			}
		}
	}

	if (hipri < 0 && prio == -1) {
		crash();
	}

	return NULL;
}

#define	FIND_HIGHEST(act)	\
	(act) = select_thread(NULL, KERNCPU, -1), (act)->runcpu = KERNCPU

#else	// uniprocessor
#define VALIDATE_NEEDTORUN(thp) 

#define FIND_HIGHEST(act) ((act) = select_thread(NULL, 0, -1))

/*
 * Select_thread function for UP.
 * Given act and prio, returns a thread pointer if a candidate equal
 * or better to act can be found (if act is non-NULL). 
 * cpu is ignored in the case of single-cpu.
 *
 * Note that act, if non-NULL, is assumed to be RUNNING and will not be taken
 * off any queue.
 */ 
static THREAD * rdecl
select_thread_default(THREAD *act, int cpu, int prio) {
	THREAD		*thp;
	// act may be NULL so we get the dpp from actives
	DISPATCH	*dpp = actives[KERNCPU]->dpp;
	int			hipri;

	hipri = DISPATCH_HIGHEST_PRI(dpp);
	if(hipri < prio) return NULL;

	thp = DISPATCH_THP(dpp, hipri);
	if(thp == NULL) {
		// May occur if there is nothing, not even idle
		return NULL;
	}

	LINK3_REM(DISPATCH_LST(dpp, (thp)->priority), (thp), THREAD);

	/* If that list is now empty, clear the readybit.	*/
    if (DISPATCH_THP(dpp, (thp)->priority) == NULL) {
		DISPATCH_CLR(dpp, (thp));
    }
	return thp;
}

#endif

/*
 * Mark a thread as the one being in execution on this CPU
 */
static void rdecl
mark_running_default(THREAD *new) {
	THREAD	*old;

	old = actives[KERNCPU];
	actives[KERNCPU] = new;

	new->state = STATE_RUNNING;
	new->runcpu = KERNCPU;

	_TRACE_TH_EMIT_STATE(new, RUNNING);

	VALIDATE_NEEDTORUN(new)

	while(ticker_preamble || old->ticker_using) {
		/* wait for clock handler to finish with the thread */
		__cpu_membarrier();
	}

	//We didn't account for any time to the previously running
	//thread. Up the running time by a bit so that watchdog programs
	//see that this thread/process is getting a chance to run.
	//We can be smarter in the future about the value that we add.
	//RUSH3: This actually a bug - we need to do this for any scheduling
	//RUSH3: algorithm, not just round robin
	if(IS_SCHED_RR(old) && (!old->schedinfo.rr_ticks)) {
		old->running_time += 1;
		old->process->running_time += 1;
	}
}


/*** Sporadic scheduler policy functions ***/
/*
 This code is used to update the replenishment object(s) adding
 them to the list if need be.  Due to the way that execution is
 currently measured (via timer tick), it is possible to call into
 this function with no consumed execution.  Since we want to do
 the replenishment bookkeeping here, simulate a small amount of
 execution to ensure that we do schedule a replenishment.

 We have chosen to deviate slightly from POSIX's "by the book" 
 algorithm where a replenishment is scheduled for each time that
 a thread blocks.  On a ukernel system this leads to excessive 
 fragmentation of the execution budget and a lot of time bookkeeping.
 Instead we choose to schedule one replenishment and then add to 
 it as long as it is pending.  Then we give back the whole amount
 later on.

 @@@ What happens when the period is already passed .. skip this?
*/
void rdecl
sched_ss_update(THREAD *thp, SSINFO *ssinfo, SSREPLENISH *ssrepl, int drop) {
	SSREPLENISH **pssrepl;

chk_lock();

	//We don't do replenishments in low priority mode
	if(ssinfo->org_priority != 0) {
		return;
	}

	if(ssrepl->amount == 0) {		//First replenishment in a period
		ssrepl->amount = ssinfo->consumed;
		if(ssrepl->amount == 0) {
			ssrepl->amount++;
		}
		ssrepl->repl_time = ssinfo->activation_time + ssinfo->repl_period;

		pssrepl = &ss_replenish_list;
		while(*pssrepl && (*pssrepl)->repl_time < ssrepl->repl_time) {
			pssrepl = &(*pssrepl)->next;
		}

		//Set the time before we set the head of the list
		if(pssrepl == &ss_replenish_list) {
			ss_replenish_time = ssrepl->repl_time;
		}

		ssrepl->next = *pssrepl;
		*pssrepl = ssrepl;
	} else {						//Replenishment already scheduled
		ssrepl->amount += ssinfo->consumed;
	}

	ssinfo->repl_count++;

#ifndef NDEBUG
if(ssrepl->amount == 0) crash();
#endif

	if(drop) {
		ssinfo->org_priority = thp->real_priority;	//Guarantee no more ss operations
		if(thp->priority <= thp->real_priority) {
			adjust_priority(thp, ssinfo->low_priority, thp->dpp);
		}
		thp->real_priority = ssinfo->low_priority;
	}
}

/* 
 We run through the pending replenishment list to take care of 
 business if there are any pending replenishments which need to be 
 cleaned out before we actually re-schedule.  This function returns
 0 if we have not made any adjustments to the priority.
*/
int rdecl
sched_ss_adjust() {
	int			adjusted = 0;

chk_lock();

	if(ss_replenish_list) {
		uint64_t	  bound;
		THREAD 		  *thp;
		SSREPLENISH   *ssrepl, **pssrepl;

		snap_time(&bound, 0);

		pssrepl = &ss_replenish_list;
		while((ssrepl = *pssrepl) && (*pssrepl)->repl_time <= bound) {
#ifndef NDEBUG
if(ssrepl->thp == NULL) crash();
if(ssrepl->thp->policy != SCHED_SPORADIC) crash();
if(ssrepl->thp->schedinfo.ss_info == NULL) crash();
#endif
			//Set the time before me move the head
			if(ssrepl->next) {
				ss_replenish_time = ssrepl->next->repl_time;
			}

			//Remove this item from the replenishment list
			*pssrepl = ssrepl->next;

			if((thp = ssrepl->thp) && IS_SCHED_SS(thp)) {
				SSINFO *ssinfo = thp->schedinfo.ss_info;

				//Since we gather all replenishments together, always
				//give back the entire budget.  This will change if we
				//do proper, per block, replenishment scheduling.
				ssinfo->curr_budget = ssinfo->init_budget;

				//Give back all of the replenishments
				ssinfo->repl_count = 0;

				//If the priority has been reduced, boost back to original
				if(ssinfo->org_priority != 0) {
					ssinfo->activation_time = bound;
					ssinfo->consumed = 0;

					if(thp->priority <= ssinfo->org_priority) {
						adjust_priority(thp, ssinfo->org_priority, thp->dpp);
					}
					thp->real_priority = ssinfo->org_priority;

					ssinfo->org_priority = 0;  

					adjusted++;
				}

				//Clear the "first replenishment" indicator
				ssrepl->amount = ssrepl->repl_time = 0;
			} 
#ifndef NDEBUG
			else {
				crash();
			}
#endif
		}
	}

	chk_dispatch(1);
	return adjusted;
}

/* 
 This function will go through and clean-up after all of the 
 SCHED_SPORADIC pieces which might be floating around the system.
*/
void rdecl
sched_ss_cleanup(THREAD *thp) {
	SSREPLENISH **pssrepl, *ssrepl;
	SSINFO		*ssinfo = thp->schedinfo.ss_info;
#ifndef NDEBUG
	int			found = 0;
#endif

chk_lock();

	pssrepl = &ss_replenish_list;
	while((ssrepl = *pssrepl)) {
		if(ssrepl->thp == thp) {
			if(pssrepl == &ss_replenish_list && ssrepl->next) {
				ss_replenish_time = ssrepl->next->repl_time;
			}
			*pssrepl = ssrepl->next;
			//Only one replenishment per thread 
#ifndef NDEBUG
			found++;
#else	
			break;	
#endif
		} else {
			pssrepl = &ssrepl->next;
		}
	}

#ifndef NDEBUG
if(found > 1) crash();
if(ssinfo->replenishment.amount != 0 && found == 0) crash();
#endif
	
	thp->schedinfo.ss_info = NULL;
	object_free(thp->process, &ssinfo_souls, ssinfo);
}

/*
 This function is called when this thread transitions from RUNNING to
 something else (presumably blocking!) so that we can cause a replenishment
 operation to happen.

 This assumes that the state of the thread which was running has already
 been set.  If the state is READY then we have been pre-empted and nothing
 has to occur.  
*/
void rdecl
sched_ss_block(THREAD *thp, int preempted) {
#ifndef NDEBUG
if(thp->policy != SCHED_SPORADIC) crash();
#endif
	if(thp->schedinfo.ss_info->org_priority != 0) {
		return;
	}

	/* TODO: Do the execution snapshot here and check for expiry */
	/* Only run this code if we are being blocked (not pre-empted) */
	if(preempted) {
		return;
	}

	//This will increment the replenishment count.  The rule is that we stay high
	//if we are strictly lower than the replenishment count.  This accounts for the +1
	sched_ss_update(thp, 
					thp->schedinfo.ss_info, 
					&thp->schedinfo.ss_info->replenishment,
					((thp->schedinfo.ss_info->repl_count + 1) >= thp->schedinfo.ss_info->max_repl));
}

/*
 This function is called when we put a thread at the tail of the READY 
 queue and we need to reset the activation time and clear the amount of 
 execution time it has used.

 This assumes that the state of the thread is _going_ to be set to READY
 and that if the thread is being boosted that it has already cleared the
 dropped flag.
*/
void rdecl
sched_ss_queue(THREAD *thp) {
#ifndef NDEBUG
if(thp->policy != SCHED_SPORADIC) crash();
#endif
	if(thp->schedinfo.ss_info->org_priority != 0) {
		return;
	}

	switch(thp->state) {
	case STATE_RUNNING:		//Time already snapped for these guys
	case STATE_READY:
		break;

	default:
		snap_time(&thp->schedinfo.ss_info->activation_time, 0);
		thp->schedinfo.ss_info->consumed = 0;
		break;
	}
}

/*** End of Sporadic scheduler attributes ***/

/*
 This function is called from thread_specret() and ker_sched_set()
*/
int rdecl
sched_thread(THREAD *thp, int policy, struct sched_param *param) {
	unsigned	new_prio;
	DISPATCH 	*dpp;
	int			verify;

chk_lock();

	switch(policy) {
	case SCHED_ADJTOHEAD:
	case SCHED_ADJTOTAIL:
		//Just a positional change, not a parameter change.
		dpp = thp->dpp;

		if(thp->state != STATE_READY) {
			return EINVAL;
		}

		LINK3_REM(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		if(policy == SCHED_ADJTOHEAD) {
			LINK3_BEG(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		} else {
			RR_RESET_TICK(thp);
			LINK3_END(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		}

		return EOK;
	case SCHED_NOCHANGE:
		policy = thp->policy;
		break;
	case SCHED_SETPRIO:
		new_prio = param->sched_priority;
		if (new_prio <= 0 || new_prio >= NUM_PRI) {
			return EINVAL;
		}
		if (thp->process->cred->info.euid != 0 && new_prio >= priv_prio) {
			return EPERM;
		}
		if (new_prio != thp->priority) {
			int	lowered = (new_prio < thp->priority);

			adjust_priority(thp, new_prio, thp->dpp);

			// If we lowered priority, ensure we will be head of priority list
			if (lowered && thp->state == STATE_READY) {
				LINK3_REM(DISPATCH_LST(thp->dpp, thp->priority), thp, THREAD);
				LINK3_BEG(DISPATCH_LST(thp->dpp, thp->priority), thp, THREAD);
			}
		}
		if (thp->policy == SCHED_SPORADIC && thp->schedinfo.ss_info->org_priority != 0) {
			thp->schedinfo.ss_info->org_priority = new_prio;
		}
		thp->real_priority = new_prio;
		return EOK;
	default:
		break;
	}
	new_prio = param->sched_priority;

	// Verify the policy and priority (and sporadic params) are valid
	if((verify = kerschedok(thp, policy, param)) != EOK) {
		return verify;
	}

	/*
	 If we are sporadic server, then we need to do some work.  We might already have
	 a data structure, in which case we may already be active.  If we were active, then
	 we only put the changes in place and expect that others will pick then up over time.
	*/
	if(policy == SCHED_SPORADIC) {
		/* 
			Sporatic was specified, and we're not running sporadic .. need ss_info alloced 

			AM Note: Orignally, was a check against !thp->schedinfo.ss_info, and if zero, alloc.
			not really valid, as it could be non-zero, since the thread is scheduled to
			run as the inherited policy of the parent, just until we actually force a new
			policy, thus making rr_ticks increment.  Since it's a union, this would result
			in a false positive

			So the check should be WAAA state, and that the thread policy isn't SPORADIC
			initially, since we would have allocated the structure at creation time already
		*/
		if(thp->policy != SCHED_SPORADIC) {
			/* 
				Fake an atomic operation - I need to change the schedinfo union,
				and if it's policy is SPORADIC/RR/OTHER, potentially it will get changed
				behind our back, and corrupt who ever was using the union.  The policy will
				change to whatever the thread is destined to be farther down

				policy == FIFO makes no use of scheduling info, so it ensures that the tick won't
				cause corruption.
			*/
			thp->policy = SCHED_FIFO;
	
			if(!(thp->schedinfo.ss_info = object_alloc(thp->process, &ssinfo_souls))) {
#ifndef NDEBUG
				crash(); // A bad thing ...
#endif
				thp->policy = SCHED_FIFO;
				return EINVAL;
			}
			memset(thp->schedinfo.ss_info, 0, sizeof(*thp->schedinfo.ss_info));
			thp->schedinfo.ss_info->replenishment.thp = thp;
		} 

		//Thread creation and newcomers to the policy need this info snapped
		if(thp->flags & _NTO_TF_WAAA || thp->policy != SCHED_SPORADIC) {
			snap_time(&thp->schedinfo.ss_info->activation_time, 0);
			thp->schedinfo.ss_info->curr_budget = timespec2nsec(&param->sched_ss_init_budget);
		}

		//If there is a change in the attributes, we must make the change and let the 
		//ramifications flow through to the other components that deal with the SS alg.
		if(thp->schedinfo.ss_info->low_priority == param->sched_ss_low_priority &&
		   thp->schedinfo.ss_info->max_repl == param->sched_ss_max_repl &&
		   thp->schedinfo.ss_info->repl_period == timespec2nsec(&param->sched_ss_repl_period) &&
		   thp->schedinfo.ss_info->init_budget == timespec2nsec(&param->sched_ss_init_budget)) {
			//No change, don't do anything
		} else {
			thp->schedinfo.ss_info->low_priority = param->sched_ss_low_priority;
			thp->schedinfo.ss_info->max_repl = param->sched_ss_max_repl;
			thp->schedinfo.ss_info->repl_period = timespec2nsec(&param->sched_ss_repl_period);
			thp->schedinfo.ss_info->init_budget = timespec2nsec(&param->sched_ss_init_budget);

			//If dropped, then adjust to the low priority, save hi priority
			if(thp->schedinfo.ss_info->org_priority != 0) {
				thp->schedinfo.ss_info->org_priority = new_prio;
				new_prio = param->sched_ss_low_priority;
			}
		}
	} else {
		if(thp->policy == SCHED_SPORADIC) {
			thp->policy = SCHED_FIFO;	//Temporary since we are nuking SS structures
			sched_ss_cleanup(thp);
		}
		RR_RESET_TICK(thp);
	}

	// Change policy and priority after fiddling with attributes
	thp->policy = policy;

	adjust_priority(thp, new_prio, thp->dpp);
	thp->real_priority = new_prio;

	return EOK;
}

/* send an ipi to notify that a blocked thread doing msg xfer is force_readied */
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
static void force_ready_msgxfer (THREAD *thp, int err) {
	int i;

	SETKSTATUS(thp, err);
	thp->internal_flags |= _NTO_ITF_MSG_FORCE_RDY;

/* only in a CPU which is not doing msg xfer could force_ready a thread
which is blocked and doing msg xfer */
/* search actives[] to see who is doing the msgxfer, and send ipi to preempt that one */
	for(i=0; i<NUM_PROCESSORS; i++) {
		if(actives[i]->internal_flags & _NTO_ITF_MSG_DELIVERY) {
			if(((THREAD*)actives[i]->blocked_on) == thp) {
				/* The thread is running on cpu i, ipi it */
				SENDIPI(i, IPI_RESCHED);
				break;
			}
		}
	}
}
#endif

/**
 Resettle the priorities of synchronization object owners after one of the elements
 in the chain has adjusted its prioirity (down).  The first pass is used really to determine
 what kind of synchronization object is being worked on (using the state as an overload)
 and the second pass uses the state of the owner of that synchronization object to 
 determine if there is a chain to follow or not.
*/
static void rdecl
priority_chain_drop(THREAD *thp, int why) {
 	SYNC		*syp;
 	THREAD 		*next_owner;
 	int 		restore_priority;
 
 	do {
 		//Synchronizaion object switch
 		switch(why) {
 		case STATE_MUTEX: {
 			restore_priority = thp->real_priority;
 			if((syp = thp->mutex_holdlist) && (next_owner = syp->waiting)) {
 				if(restore_priority < next_owner->priority) {
 					restore_priority = next_owner->priority;
 				}
 			}
 			if(thp->priority > restore_priority) {
 				adjust_priority(thp, restore_priority, thp->dpp);
 			} else {
 				thp = NULL;
 			}
 			break;
 		}
 
 		case STATE_REPLY:
 		case STATE_SEND:
 			//TODO: Chain through the resmgr
 		default:
 			thp = NULL;
 		}
 
 		//No change is expected 
 		if(thp == NULL) {
 			break;
 		}
 
 		//Synchronization object owner switch
 		switch(thp->state) {
 		case STATE_MUTEX: {
 			PROCESS		*prp;
 			int			 id, owner;
 
 			//If we are not at the head of the list, then we aren't affecting anything
 			syp = thp->blocked_on;
 			if(syp->waiting != thp) {
 				thp = NULL;		
 				break;
 			}
 
 			//Find the owner of the mutex we are blocked on and move to it
 			owner = thp->args.mu.owner;
 			id = SYNC_PINDEX(owner);
 			if(id  &&  id < process_vector.nentries  &&  VECP(prp, &process_vector, id)) {
 				thp = vector_lookup(&prp->threads, SYNC_TID(owner));
 				why = STATE_MUTEX;
 			} else {
				thp = NULL;
 			}
 			break;
 		}
 
 		case STATE_SEND:
 		case STATE_REPLY:
 			//TODO: Chain through the resmgr
 		default:
 			thp = NULL;
 		}
 
 
	} while(thp != NULL);
}

int rdecl
force_ready(THREAD *thp, int err) {

#if defined(VARIANT_smp) && defined(SMP_MSGOPT) 
#ifndef NDEBUG
	if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
		kprintf("try to force_ready() state %x.\n",thp->state);
	}
#endif
#endif

	chk_lock();

	switch(thp->type) {
	case TYPE_PULSE:
	case TYPE_VPULSE:
		LINK2_REM(thp);
		object_free(actives[KERNCPU]->process, &pulse_souls, thp);
		break;

	case TYPE_THREAD:
	case TYPE_VTHREAD:
		// Make sure short message flag is cleared
		if(thp->state != STATE_READY && thp->state != STATE_RUNNING && thp->state != STATE_REPLY) {
			thp->flags &= ~_NTO_TF_BUFF_MSG;
		}

		switch(thp->state) {
		case STATE_READY:
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_FORCE_RDY) {
				/* this means that the chanel has been destroyed, put the thread in ready queue */
				if(thp->type == TYPE_VTHREAD) {
					thp->state = STATE_STOPPED;
					snap_time(&thp->timestamp_last_block,0); 
					if(thp->flags & _NTO_TF_KILLSELF) {
						object_free(NULL, &vthread_souls, thp);
					}
				} else {
					ready(thp);
				}
			}
#endif
		//fall through
		case STATE_RUNNING:
			thp->restart = NULL;	// Abort any restart on a message pass
			/* Fall Through */

		case STATE_DEAD:
		case STATE_STOPPED:
			return 1;
		case STATE_WAITPAGE:
		case STATE_STACK:
			return 0;

		case STATE_NET_REPLY:
		case STATE_REPLY: {
			CONNECT *cop = thp->blocked_on;
			CHANNEL *chp;

			if((thp->type != TYPE_VTHREAD) && (cop->flags & COF_NETCON)) {
				chp = net.chp;
			} else {
				chp = cop->channel;
			}

#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
				force_ready_msgxfer(thp, err);
#ifndef NDEBUG
				kprintf("try to ready STATE_REPLY.\n");
#endif
				return 0;
			}
#endif
			//
			// If the channel wants an unblock pulse and the responsible
			// process _isn't_ dying, we can't unblock this thread just
			// yet.
			//
			if(chp
			 && (chp->flags & _NTO_CHF_UNBLOCK)
			 && ((chp->process == NULL)
			     || !(chp->process->flags & (_NTO_PF_TERMING|_NTO_PF_DESTROYALL)))
			 && (thp->state == STATE_REPLY)) {
				int rcvid;
				if (thp->type == TYPE_VTHREAD) {
					rcvid = (((VTHREAD *)thp)->un.net.vtid << 16) | cop->scoid;
				} else if (cop->flags & COF_NETCON) {
					rcvid = (thp->args.ms.coid << 16) | 0xffff;
				} else {
					rcvid = (thp->tid << 16) | cop->scoid;
				}
				thp->internal_flags |= _NTO_ITF_UNBLOCK_QUEUED;
				_TRACE_COMM_EMIT_SPULSE_UN(cop, cop->scoid | _NTO_SIDE_CHANNEL, thp->priority);
				if(pulse_deliver(chp, thp->priority, _PULSE_CODE_UNBLOCK,
								 rcvid, cop->scoid | _NTO_SIDE_CHANNEL, 0) != EOK) {
					/*
					 * If there was a server fault and the pulse could not
					 * be delivered, let the client unblock as if the server
					 * handled the pulse.
					 */
					err |= _FORCE_SET_ERROR;
				}
				if(err & _FORCE_KILL_SELF) {
					thp->flags |= _NTO_TF_KILLSELF | _NTO_TF_ONLYME;
				}
				if((err & _FORCE_SET_ERROR) == 0) {
					thp->flags |= _NTO_TF_UNBLOCK_REQ;
					return 0;
				}
				thp->flags &= ~_NTO_TF_UNBLOCK_REQ;
			}
			if(thp->args.ms.server != NULL) {
				if(thp->args.ms.server->client == NULL) {
					crash();
				}
				thp->args.ms.server->client = NULL;
				thp->args.ms.server = NULL;
			}
			if(thp->restart) {
				if((thp->restart->flags & (_NTO_TF_RCVINFO | _NTO_TF_SHORT_MSG)) != 0) {
					KERCALL_RESTART(thp->restart);
					thp->restart->flags &= ~(_NTO_TF_RCVINFO | _NTO_TF_SHORT_MSG);
					thp->internal_flags &= ~_NTO_ITF_SPECRET_PENDING;
					thp->restart = NULL;
				}
			}
		}
			/* Fall Through */

		case STATE_NET_SEND:
		case STATE_SEND: {
			CONNECT *cop = thp->blocked_on;

#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
				force_ready_msgxfer(thp, err);
#ifndef NDEBUG
				kprintf("try to ready STATE_SEND.\n");
#endif
				return 0;
			}
#endif
			if(cop->links == 0) {
				crash();
			} else if(--cop->links == 0) {
				connect_detach(cop, thp->priority);
			}
		}
			/* Fall Through */

		case STATE_RECEIVE:
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
				force_ready_msgxfer(thp, err);
#ifndef NDEBUG
				kprintf("try to ready STATE_RECEIVE.\n");
#endif
				return 0;
			}
#endif
			thp->internal_flags &= ~_NTO_ITF_RCVPULSE;
			thp->restart = NULL;	// Abort any restart on a message pass
			LINK2_REM(thp);
			break;

		case STATE_MUTEX:
		{
			THREAD		*thp2;
			PROCESS		*prp;
			SYNC		*syp;
			int			 id;
			int			 owner;

#ifndef NDEBUG
if(thp->type != TYPE_THREAD) crash();
#endif
  			syp = thp->blocked_on;
  			if(syp->waiting == thp) {
 				//Remove the thread from the mutex list
  				if((owner = thp->args.mu.owner)) {
  					MUTEXHOLDLIST_REM(syp);
  				}
  				LINK2_REM(thp);
 
 				//Identify the owner of the mutex, could be null if orphaned
 				thp2 = NULL;
 				id = SYNC_PINDEX(owner);
 				if(id  &&  id < process_vector.nentries  &&  VECP(prp, &process_vector, id)) {
 					thp2 = vector_lookup(&prp->threads, SYNC_TID(owner));
 				}
 
  				if(syp->waiting && syp->waiting->type == TYPE_THREAD) {
  					if(thp2) {
  						MUTEXHOLDLIST_PRI(thp2->mutex_holdlist, syp);
  					} else {
  						owner = 0;
  					}
  					syp->waiting->args.mu.owner = owner;
 				} 
 
 				//Re-adjust the boosted mutex owner if required
 				if(thp2 && (thp != thp2) && (thp->priority == thp2->priority) && (thp2->priority > thp2->real_priority)) {
 					priority_chain_drop(thp2, STATE_MUTEX);
 				}
 
  				break;
  			}
  		}
			/* Fall Through */
		case STATE_CONDVAR:
		case STATE_SEM:
			LINK2_REM(thp);
			break;

		case STATE_WAITTHREAD:
			((THREAD *)thp->blocked_on)->flags |= (_NTO_TF_KILLSELF | _NTO_TF_ONLYME);
			/* Fall Through */

		case STATE_JOIN:
			((THREAD *)thp->blocked_on)->join = NULL;
			break;

		case STATE_SIGSUSPEND:
		case STATE_SIGWAITINFO:
		case STATE_NANOSLEEP:
		case STATE_INTR:
			break;

		default:
			crash();
		}

		if(thp->state != STATE_READY  &&  thp->state != STATE_RUNNING) {
			kererr(thp, err & ~(_FORCE_SET_ERROR | _FORCE_KILL_SELF));
			if(thp->type == TYPE_VTHREAD) {
#ifndef NDEBUG
if(thp->state != STATE_SEND && thp->state != STATE_REPLY) crash();
#endif
				thp->state = STATE_STOPPED;
				snap_time(&thp->timestamp_last_block,0); 
				_TRACE_TH_EMIT_STATE(thp, STOPPED);
				if(thp->flags & _NTO_TF_KILLSELF) {
#ifndef NDEBUG
if(vector_lookup(&vthread_vector, thp->un.net.vtid)) crash();
#endif
					object_free(NULL, &vthread_souls, thp);
				}
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
				if(!(thp->internal_flags & _NTO_ITF_NET_UNBLOCK) && net.chp) {
					/* send a pulse to qnet to let the client thread go */
					_TRACE_COMM_EMIT_SPULSE_QUN((-1), thp->priority);
					pulse_deliver(net.chp, thp->priority, _PULSE_CODE_NET_UNBLOCK, thp->un.net.vtid, -1, 0);
				}
#endif
			} else {
				ready(thp);
			}
		}
		break;

	default:
		crash();
	}
	return 1;
}


/*
 *	Block the active thread and scan down the
 *	ready queue looking for thread of next highest priority.
 *	Assume that active must have been the thread of highest
 *	priority or it would not have been running. At least one
 *	thread is always ready (the idle thread).
 *
 *  Note that this function is called quite a bit, but we 
 *  don't create a special version for the non-standard scheduler.
 *  Instead, the FIND_HIGHEST() macro is used to indirect to the
 *  alternate scheduler functions if needed.
 */

void rdecl
block(void) {
	THREAD		*act = actives[KERNCPU], *thp = act;

	chk_lock();
	chk_dispatch(0);
	
	snap_time(&(act->timestamp_last_block), 0);
	
	// Check for timeout timer
	if(act->timeout_flags & _NTO_TIMEOUT_MASK) {
		timeout_start(act);
	}

	// Get highest priority thread which can run on this processor.
	FIND_HIGHEST(act);

	if (IS_SCHED_RR(thp)) {
		if ( (thp==act) || (thp->priority < act->priority)) {
			/* preempt */
			RR_ADD_PREEMPT_TICK(thp);
		} 
	}
	
	mark_running(act);
	
	// Mark execution and schedule a replenishment if needed
	if(thp != act) {
		SS_STOP_RUNNING(thp, (thp->priority < act->priority));
	}
	
	chk_dispatch(1);
}



//
//	Make a thread ready to run. If the priority of this thread
//	is higher than active, then make this one active. Ready asumes
//	that if this is true, then it must now be the only thread
//	of that priority which is ready to run, so no searching
//	through the dispatch is necessary.
//

static void rdecl
ready_default(THREAD *thp) {
	THREAD		*act;
	DISPATCH	*dpp;
	uint8_t		prio = thp->priority;

	chk_lock();
	chk_dispatch(1);
	chk_thread(thp);

	if(thp->timeout_flags) timeout_stop(thp);

	// Make sure the thread does not have a pending stop
	if((thp->flags & _NTO_TF_TO_BE_STOPPED) && !(thp->flags & _NTO_TF_KILLSELF)) {
		thp->state = STATE_STOPPED;
		snap_time(&thp->timestamp_last_block, 0); 
		_TRACE_TH_EMIT_STATE(thp, STOPPED);
		chk_dispatch(1);
		return;
	}

	thp->next = NULL;
	thp->prev = NULL;

	//Before we do anything with the actives, check to see if this is an
	//expired SS thread and if it is, then give a chance to schedule a 
    //replenishment and drop the priority.  This code should only "do something"
    //when ready() is called after servicing requests generated by ISR's
#if !defined(VARIANT_smp)
	//This is only enabled for non-smp systems since it may potentially
    //cause an undesirable feedback loop.  This is an issue to be resolved.
	SS_CHECK_EXPIRY(actives[KERNCPU]);
#endif

	// If new thread is higher priority we make it active
	act = actives[KERNCPU];
	dpp = act->dpp;

#if defined(VARIANT_smp)
	{
		int			cpu;
	
		// Set the activation time, clear the consumed
		SS_MARK_ACTIVATION(thp);

		if(STATE_LAZY_RESCHED(thp) && (prio <= act->priority) && (thp->runmask & (1 << KERNCPU)) == 0) {
			thp->state = STATE_READY;
			_TRACE_TH_EMIT_STATE(thp, READY);
			// We should really do a LINK3_BEG, but we can't be sure we won't starve
			RR_RESET_TICK(thp);
			LINK3_END(DISPATCH_LST(dpp, prio), thp, THREAD);
			DISPATCH_SET(dpp, thp);
			return;
		}
		if((cpu = select_cpu(thp)) >= 0) {
			// Do we need to assign another cpu this thread.
			if(cpu != KERNCPU) {
				thp->state = STATE_READY;
				_TRACE_TH_EMIT_STATE(thp, READY);
				RR_RESET_TICK(thp);
				LINK3_END(DISPATCH_LST(dpp, prio), thp, THREAD);
				DISPATCH_SET(dpp, thp);
	
				// Disable need_to_run for now. 
#if 0
				if (need_to_run == NULL && (thp->flags & _NTO_TF_SPECRET_MASK) == 0) {
					need_to_run_cpu = cpu;
					need_to_run = thp;
				}
#endif

				SENDIPI(cpu, IPI_RESCHED);
				return;
			}
	
			// The thread can replace the thread on this machine.
			mark_running(thp);

			thp = act;
			RR_ADD_PREEMPT_TICK(act);
			act->state = STATE_READY;
			LINK3_BEG(DISPATCH_LST(dpp, act->priority), act, THREAD);

			SS_STOP_RUNNING(act, 1);
		} else {
			thp->state = STATE_READY;
			//If new thread is lower or equal prio, it goes at end of it's
			//ready queue
			RR_RESET_TICK(thp);	
			LINK3_END(DISPATCH_LST(dpp, prio), thp, THREAD);
		}
	}
#else

	// Set the activation time, clear the consumed on ready or running
	SS_MARK_ACTIVATION(thp);

	if(prio > act->priority) {
		mark_running(thp);
		thp = act;
		//If new thread is higher prio, the old active is put at head
		//of it's ready queue (think fifo scheduling).
		RR_ADD_PREEMPT_TICK(act);
		act->state = STATE_READY;
		LINK3_BEG(DISPATCH_LST(dpp, act->priority), act, THREAD);

		SS_STOP_RUNNING(act, 1);		
	} else {
		thp->state = STATE_READY;
		//If new thread is lower or equal prio, it goes at end of it's
		//ready queue
		RR_RESET_TICK(thp);
		LINK3_END(DISPATCH_LST(dpp, prio), thp, THREAD);
	}
#endif
	// Put lower of (act, thp) to the end of the ready queue for its priority.
	_TRACE_TH_EMIT_STATE(thp, READY);
	DISPATCH_SET(dpp, thp);
	chk_dispatch(1);
}



//
//	Block the active thread and ready the specified thread.
//	If the new thread is of higher priority than active, then
//	it can be made active immediately. If not, we must scan
//	the dispatch looking for the highest priority ready
//	thread.
//

static void rdecl
block_and_ready_default(THREAD *thp) {
	THREAD		*act = actives[KERNCPU];
	DISPATCH	*dpp;
	uint8_t		prio;

	chk_lock();
	chk_dispatch(0);
	chk_thread(thp);

	// Check for timeout timers
	if(act->timeout_flags & _NTO_TIMEOUT_MASK) {
		timeout_start(act);
	}

	if(thp->timeout_flags) timeout_stop(thp);

	thp->next = NULL;
	thp->prev = NULL;
	
	thp->restart = NULL;

	// If thp has a pending stop then degrade block_and_ready() to just block()
	if((thp->flags & _NTO_TF_TO_BE_STOPPED) && !(thp->flags & _NTO_TF_KILLSELF)) {
		thp->state = STATE_STOPPED;
		//we dont have to update act->timestamp_last_block here becuse block() below will do it.
		_TRACE_TH_EMIT_STATE(thp, STOPPED);
		block();
		chk_dispatch(1);
		return;
	}
	SNAP_TIME_INLINE(act->timestamp_last_block,0);  
	dpp = thp->dpp;
	prio = thp->priority;

	//We are blocking the active thread (externally) so stop the running
    //Mark the activation time for this new thread 
	SS_STOP_RUNNING(act, ((act->state == STATE_READY) || (act->state == STATE_RUNNING)));
	SS_MARK_ACTIVATION(thp);

#if defined(VARIANT_smp)
	// Should the new thread run on this cpu.
	if((thp->runmask & (1 << KERNCPU)) == 0) {
		if(prio > act->priority  ||  prio > DISPATCH_HIGHEST_PRI(dpp)) {
			mark_running(thp);
			chk_dispatch(1);
			return;
		}
	} else {
		int			cpu;

		if((cpu = select_cpu(thp)) >= 0) {
			// The thread should run on another processor.
			if(cpu == KERNCPU) crash();
			SENDIPI(cpu, IPI_RESCHED);
		}
	}
#else
	// Will thp be the highest priority thread ready to run?
	if(prio > act->priority  ||  prio > DISPATCH_HIGHEST_PRI(dpp)) {
		mark_running(thp);
		chk_dispatch(1);
		return;
	}
#endif

	// Add thp to ready queue.
	LINK3_BEG(DISPATCH_LST(dpp, prio), thp, THREAD);
	DISPATCH_SET(dpp, thp);
	thp->state = STATE_READY;
	_TRACE_TH_EMIT_STATE(thp, READY);
	FIND_HIGHEST(act);

	mark_running(act);

	chk_dispatch(1);
}



//
//	Remove a thread from the ready list. If the thread being make
//  unready is the active thread we call block() otherwise the thread
//	being made unready is not active. As such, active must be higher
//	priority than the thread being made unready. Therefore we do not need
//	to change active.
//
void rdecl
unready(THREAD *thp, int newstate) {
	DISPATCH	*dpp;
	
	if(thp == actives[KERNCPU]) {
		chk_lock();
		chk_cond(thp->state == STATE_RUNNING);
		chk_thread(thp);

		if(thp->state != STATE_RUNNING) crash();
		thp->state = newstate;
		_TRACE_TH_EMIT_ANY_STATE(thp, newstate);
		block();
		return;
	}

	chk_lock();
	chk_cond(thp->state != STATE_RUNNING);
	chk_thread(thp);

	// Only remove from dispatch list if READY
	if(thp->state == STATE_READY) {
		dpp = thp->dpp;
		LINK3_REM(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		if(!DISPATCH_THP(dpp, thp->priority)) {
			DISPATCH_CLR(dpp, thp);
		}
	} else { 
		//timestamp_last_block is valid since were not running and not ready 
		SNAP_TIME_INLINE(thp->timestamp_last_block, 0);  
	}

	
	

	thp->state = newstate;
	_TRACE_TH_EMIT_ANY_STATE(thp, newstate);

	// Check for timeout timer
	if(thp->timeout_flags & _NTO_TIMEOUT_MASK) {
		timeout_start(thp);
	}


	chk_dispatch(1);
}

/*
 * The yield call implements the SchedYield() kernel call. The global
 * rescheduling is handled through the resched() call below.
 * Since this is one of the top benchmarked microkernel calls,
 * we special-case a UP and SMP. AP has its own version.
 */


static void
yield_default(void) {
	THREAD		*act = actives[KERNCPU];
	THREAD		*thp;
	uint8_t		prio = act->priority;
	DISPATCH	*dpp;
#ifdef VARIANT_smp
	uint32_t	runmask = 1 << KERNCPU;
#endif

	// Run down this priority queue looking for a thread which will
	// run on this processor.
	chk_lock();


	/*
	 * For UP, this is really simple. If there is another thread
	 * at the same pririty, just swap it for actives, else return.
	 */

	dpp = act->dpp;

#ifdef VARIANT_smp
	for(thp = DISPATCH_THP(dpp, prio); thp != NULL; thp = thp->next) {
		if((thp->runmask & runmask) == 0) break;
	}
#else
	thp = DISPATCH_THP(dpp, prio);
#endif

	if(thp == NULL) {
		return;
	}

	// We have a new thp, swap it in with act
	LINK3_REM(DISPATCH_LST(dpp, prio), (thp), THREAD);
	LINK3_END(DISPATCH_LST(dpp, prio), act, THREAD);

	RR_RESET_TICK(act);
	act->state = STATE_READY;
	_TRACE_TH_EMIT_STATE(act, READY);

	mark_running(thp);
	SS_STOP_RUNNING(act, 0);
	SS_MARK_ACTIVATION(act);

	chk_dispatch(1);

}

/*
 * This function is called on a clock tick (or from another CPU)
 * when we need to do a global scheduling operation.
 */
static void
resched_default(void) {
	THREAD		*act = actives[KERNCPU];
	THREAD		*thp;
	DISPATCH	*dpp = act->dpp;
	uint8_t		prio = act->priority;

	// Run down this priority queue looking for a thread which will
	// run on this processor.
	chk_lock();

	//If we are coming in from an ISR where the expiry of the execution
	//budget of a SS thread was detected, then look at rescheduling and
	//potentially drop the priority of the active thread.  If this is
	//done then don't bother with the rest of the yield operation
	//since actives has already been re-evaluated
	SS_CHECK_EXPIRY(act);

	// If there is a replenishment pending, then do the adjustment 
	// If actives has changed above, early out.
	if((ss_replenish_list && sched_ss_adjust() != 0) || (act != actives[KERNCPU])) {
		return;
	}

#ifndef NDEBUG
	if(actives[KERNCPU] != act) {
		crash();
	}
#endif

	// check if this process exceeded its max cpu usage
	if (act->process->running_time > act->process->max_cpu_time &&
		!(act->flags & _NTO_TF_KILLSELF)) {

		if (signal_kill_process(act->process, SIGXCPU, 0, 0, act->process->pid, 0) == SIGSTAT_IGNORED) {
			signal_kill_process(act->process, SIGKILL, 0, 0, act->process->pid, 0);
		}
		return;
	}

	// Find highest priority thread which can run on this processor.
	// Unlink from ready queue if its priority is >= than actives.
	prio = (act->runmask & (1 << KERNCPU)) ? 0 : act->priority;
	thp = select_thread(act, KERNCPU, prio);

	/*
	 * If thp is non-NULL, then select_thread has found someone equal or
	 * better to run, although the thread's priority may be lower than
	 * what we're currently running.
	 */
	if(thp) {

		RR_ADD_PREEMPT_TICK(act);

		// Add active to ready queue. If tick count >= RR_MAXTICKS, the
		// assumption is we've consummed our sched quantum. Otherwise, we've
		// been preempted and go back to the head of the queue.
		if (IS_SCHED_RR(act) && RR_GET_TICKS(act) >= RR_MAXTICKS) {
			RR_RESET_TICK(act);
			LINK3_END(DISPATCH_LST(dpp, act->priority), act, THREAD);
		} else {
			// otherwise keep active at the head of the queue
			LINK3_BEG(DISPATCH_LST(dpp, act->priority), act, THREAD);
		}
		DISPATCH_SET(dpp, act);

		act->state = STATE_READY;
		_TRACE_TH_EMIT_STATE(act, READY);

		mark_running(thp);
	} else {
#ifdef VARIANT_smp
		_TRACE_TH_EMIT_STATE(act, RUNNING);	 
		VALIDATE_NEEDTORUN(0)
#endif
	}
	chk_dispatch(1);
}

static void rdecl
adjust_priority_default(THREAD *thp, int prio, DISPATCH *newdpp) {
	THREAD		*act = actives[KERNCPU];
	DISPATCH	*dpp;

	chk_lock();
	chk_dispatch(1);
	chk_thread(thp);

	if(thp->priority == prio) {
		if(thp == act) {
			yield();
		}
		return;
	}

	dpp = thp->dpp;
	switch(thp->state) {
	case STATE_READY:
		chk_cond(thp != act);
		chk_thread(thp);

		// Unlink from the ready queue
		LINK3_REM(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		if(!DISPATCH_THP(dpp, thp->priority)) {
			DISPATCH_CLR(dpp, thp);
		}

		thp->priority = prio;
#if defined(VARIANT_smp)
		ready(thp);
#else
		if(thp->priority > act->priority) {
			act->state = STATE_READY;
			_TRACE_TH_EMIT_STATE(act, READY);
			RR_ADD_PREEMPT_TICK(act);
			mark_running(thp);
			thp = act;
			//This guy should go to the head of the priority list, not the tail
			LINK3_BEG(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
			
			SS_STOP_RUNNING(act, 1);
		} else {
			// Put lower of (act, thp) to the end of the ready queue for its priority
			RR_RESET_TICK(thp);
			LINK3_END(DISPATCH_LST(dpp, thp->priority), thp, THREAD);
		}
		
		DISPATCH_SET(dpp, thp);
#endif
		chk_dispatch(1);
		break;

	case STATE_RUNNING:
		chk_thread(thp);
#if defined(VARIANT_smp)
		// Thread is running on another processor so kick it to adjust.
		if(act != thp) {
			thp->priority = prio;
			SENDIPI(thp->runcpu, IPI_RESCHED);
			break;
		}

		// Thread is running on this processor.
		if(prio < act->priority) {
			// Did we lower our prio below another thread which can run?
			thp = select_thread(act, KERNCPU, prio);
			if(thp  &&  thp->priority >= prio) {
				// Another thread is now higher/equal priority.
				// Put active into ready queue
				act->state = STATE_READY;
				act->priority = prio;
				RR_RESET_TICK(act);
				LINK3_END(DISPATCH_LST(dpp, act->priority), act, THREAD);
				DISPATCH_SET(dpp, act);

				_TRACE_TH_EMIT_STATE(act, READY);

				mark_running(thp);

				SS_STOP_RUNNING(act, 1);
			} else {
				act->priority = prio;
				_TRACE_TH_EMIT_ANY_STATE(act, act->state);
			}
		} else {
			act->priority = prio;
			_TRACE_TH_EMIT_ANY_STATE(act, act->state);
		}
#else
		chk_cond(thp == act);
		if(prio < act->priority && prio <= DISPATCH_HIGHEST_PRI(dpp)) {
			THREAD *oldact = act;

			// Put active into ready queue
			act->priority = prio;
			act->state = STATE_READY;
			_TRACE_TH_EMIT_STATE(act, READY);
			RR_RESET_TICK(act);
			LINK3_END(DISPATCH_LST(dpp, act->priority), act, THREAD);

			DISPATCH_SET(dpp, act);

			// Get highest priority thread.
			act = DISPATCH_THP(dpp, DISPATCH_HIGHEST_PRI(dpp));
			LINK3_REM(DISPATCH_LST(dpp, act->priority), act, THREAD);
			/* If that list is now empty, clear the readybit.	*/
			if (DISPATCH_THP(dpp, act->priority) == NULL) {
				DISPATCH_CLR(dpp, act);
			}

			mark_running(act);
			
			SS_STOP_RUNNING(oldact, 1);
		} else {
			act->priority = prio;
			_TRACE_TH_EMIT_ANY_STATE(act, act->state);
		}
#endif
		chk_dispatch(1);
		break;

	default:
		thp->priority = prio;
	}

	chk_dispatch(1);
}

int rdecl
stop_one_thread(PROCESS *prp, int tid, unsigned flags, unsigned iflags) {
	THREAD *thp, *thp2;

	chk_lock();
	if((tid < prp->threads.nentries) && (thp = VECP2(thp2, &prp->threads, tid))) {
		if((thp == thp2) || (thp->flags & _NTO_TF_WAAA)) {
			thp->flags |= _NTO_TF_TO_BE_STOPPED | flags;
			thp->internal_flags |= iflags;
#if defined(VARIANT_smp)
			if(thp->state == STATE_RUNNING  &&  thp != actives[KERNCPU]) {
				SENDIPI(thp->runcpu, IPI_RESCHED);
			}
#endif
			return 1;
		}
	}
	return 0;
}

void rdecl
stop_threads(PROCESS *prp, unsigned flags, unsigned iflags) {
	int tid;

	for(tid = 0; tid < prp->threads.nentries; ++tid) {
		stop_one_thread(prp, tid, flags, iflags);
	}
}


int rdecl
cont_one_thread(PROCESS *prp, int tid, unsigned flags) {
	THREAD *thp, *thp2;

	chk_lock();
	if((tid < prp->threads.nentries) && (thp = VECP2(thp2, &prp->threads, tid))) {
		if((thp == thp2) || (thp->flags & _NTO_TF_WAAA)) {
			if((prp->flags & (_NTO_PF_STOPPED | _NTO_PF_DEBUG_STOPPED | _NTO_PF_COREDUMP)) == 0) {
				thp->flags &= ~flags;
				if((thp->flags & (_NTO_TF_FROZEN|_NTO_TF_THREADS_HOLD)) == 0) {
					thp->flags &= ~_NTO_TF_TO_BE_STOPPED;
					if(thp->state == STATE_STOPPED) {
						ready(thp);
					}
				}
			}
			return 1;
		}
	}
	return 0;
}


void rdecl
cont_threads(PROCESS *prp, unsigned flags) {
	int tid;

	for(tid = 0 ; tid < prp->threads.nentries ; ++tid) {
		cont_one_thread(prp, tid, flags);
	}
}


//
// Set the runmask for the calling thread.
//
int rdecl
set_runmask(THREAD *thp, unsigned runmask) {
	THREAD	*act = actives[KERNCPU];

	// Must specify at least one available processor.
	if((runmask & LEGAL_CPU_BITMASK) == 0) {
		kererr(act, EINVAL);
		return -1;
	}

	// We keep an inverted copy of the mask internally.
	thp->runmask = ~runmask;

#if defined(VARIANT_smp)
	// If we are on a CPU which we have masked then force us off!
	if(thp->runmask & (1 << thp->runcpu)) {
		if(thp == act) {
			act->state = STATE_STOPPED;
			_TRACE_TH_EMIT_STATE(thp, STOPPED);
			//No need to update thp->timestamp_last_block since block() below will do it.
			block();
			ready(thp);
		}
		else if(thp->state == STATE_RUNNING) {
			SENDIPI(thp->runcpu, IPI_RESCHED);
		}
	}
#endif

	return 0;
}

/* in priority-based sheduling, a thread may always run, if it's got the priority */ 
static int rdecl
may_thread_run_default(THREAD *thp) { 
		return 1;
}

/*provide a target for the sched_trace_initial_parms() function pointer. In our case, there are no global 
 * parms that need tracing. 
 */
void 
sched_trace_initial_parms_default() {}


static void rdecl
debug_moduleinfo_default(PROCESS *prp, THREAD *thp, void *dbg) {
    if(thp == NULL) {		// PROCESS-level information
		memset(dbg, 0, sizeof(((debug_process_t *)NULL)->extsched));
    } else {				// THREAD-level information
		memset(dbg, 0, sizeof(((debug_thread_t *)NULL)->extsched));
    }
}

/*
 * Initialize the scheduler callbacks
 */

DISPATCH *
init_scheduler_default(void) {
	DISPATCH	*dpp;


	/* Init function pointers */
	ready = ready_default;
	block_and_ready = block_and_ready_default;
	select_thread = select_thread_default;
	mark_running = mark_running_default;
	adjust_priority = adjust_priority_default;
	resched = resched_default;
	yield = yield_default;
	may_thread_run = may_thread_run_default; 
	sched_trace_initial_parms = sched_trace_initial_parms_default;
	debug_moduleinfo = debug_moduleinfo_default;
     	
	dpp = _scalloc(sizeof (*dpp));
	scheduler_type = SCHEDULER_TYPE_DEFAULT;
	return dpp;
}


__SRCVERSION("nano_sched.c $Rev: 157350 $");
