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

#undef IGNORE_OLD_SYNC_ATTR
#undef IGNORE_OLD_SYNC_CREATE

int kdecl
ker_sync_create(THREAD *act, struct kerargs_sync_create *kap) {
	struct _sync_attr	*attr;
#ifndef IGNORE_OLD_SYNC_ATTR
	struct _sync_attr	buff;
#endif
	sync_t				*sync;
	unsigned			type;
	int					count;
	SYNC				*syp;
	struct syncevent_entry *syncep = NULL;

#ifndef IGNORE_OLD_SYNC_CREATE
	if(kap->type < _NTO_SYNC_DEAD && kap->type != _NTO_SYNC_MUTEX_FREE) {
		// Older libc didn't have a type. The first parameter would
		// be a pointer instead of a type. This is detected and the
		// parameters are swaped to support the old SyncCreate() call.
		type = _NTO_SYNC_MUTEX_FREE;
		sync = (sync_t *)kap->type;
		attr = (struct _sync_attr *)kap->sync;
	} else {
#endif
		type = kap->type;
		sync = kap->sync;
		attr = kap->attr;
#ifndef IGNORE_OLD_SYNC_CREATE
	}
#endif

	WR_VERIFY_PTR(act, sync, sizeof(*sync));
	WR_PROBE_INT(act, sync, sizeof(*sync) / sizeof(int));

	if(attr) {
#ifndef IGNORE_OLD_SYNC_ATTR
		RD_VERIFY_PTR(act, attr, 3*sizeof(int));
		if(attr->__flags & _NTO_ATTR_EXTRA_FLAG) {
#endif
			RD_VERIFY_PTR(act, attr, sizeof(*attr));
#ifndef IGNORE_OLD_SYNC_ATTR
		} else {
			memcpy(&buff, attr, 3 * sizeof(int));
			memset((int *)&buff + 3, 0x00, sizeof(buff) - 3 * sizeof(int));
			attr = &buff;
		}
#endif
	}

	switch(type) {
	case _NTO_SYNC_MUTEX_FREE:
#ifndef IGNORE_OLD_SYNC_CREATE
		if(sync == kap->sync) {		// The old SyncCreate set the count in the lib
#endif
			count = _NTO_SYNC_NONRECURSIVE;
			if(attr != NULL) {
			 	if((attr->__flags & PTHREAD_RECURSIVE_MASK) != PTHREAD_RECURSIVE_DISABLE) {
					count &= ~_NTO_SYNC_NONRECURSIVE;
				}
				if(attr->__flags & PTHREAD_ERRORCHECK_DISABLE) {
					count |= _NTO_SYNC_NOERRORCHECK;
				}
			}
#ifndef IGNORE_OLD_SYNC_CREATE
		} else {
			count = sync->__count;
		}
#endif
		if (attr) {
			switch(attr->__protocol) {
			case PTHREAD_PRIO_INHERIT:
				break;

			case PTHREAD_PRIO_PROTECT:
			{
				unsigned pri = attr->__prioceiling;

				if((pri < 1) || (pri >= NUM_PRI) || (act->process->cred->info.euid != 0 && pri >= priv_prio)) {
					return EINVAL;
				}
				lock_kernel();
				if((syncep = object_alloc(NULL, &thread_souls)) == NULL) {
					return EAGAIN;
				}
				syncep->type = TYPE_SYNCEVENT;
				syncep->subtype = SYNCEVENT_SUBTYPE_PRIORITYCEILING;
				syncep->priority = pri;
				((THREAD*)syncep)->args.mu.owner = 0;
				((THREAD*)syncep)->args.mu.next = 0;
				((THREAD*)syncep)->args.mu.prev = 0;
				count |= _NTO_SYNC_PRIOCEILING;
				break;
			}

			case PTHREAD_PRIO_NONE:
				count |= _NTO_SYNC_PRIONONE;
				break;

			default:
				return EINVAL;
			}
		}
		// attr->__flags & PTHREAD_PROCESSSHARED_MASK
		break;

	case _NTO_SYNC_COND:
		count = CLOCK_REALTIME;
#ifndef IGNORE_OLD_SYNC_ATTR
		if(attr && (attr->__flags & _NTO_ATTR_EXTRA_FLAG)) {
#else
		if(attr) {
#endif
			count = attr->__clockid;
		}
		switch(count) {
		case CLOCK_MONOTONIC:
		case CLOCK_REALTIME:
		case CLOCK_SOFTTIME:
			break;
		default:
			return EINVAL;
		}

		// attr->__flags & PTHREAD_PROCESSSHARED_MASK
		// attr->__clockid
		break;

	case _NTO_SYNC_SEM:
		if(attr) {
			if((unsigned)attr->__protocol > SEM_VALUE_MAX) {
				return EINVAL;
			}
			count = (unsigned)attr->__protocol;
		} else {
			count = 0;
		}
		// attr->__flags & PTHREAD_PROCESSSHARED_MASK
		break;

	default:
		return EINVAL;
	}

	lock_kernel();

	if((syp = sync_create(sync, attr ? attr->__flags : PTHREAD_PROCESS_PRIVATE)) == NULL) {
		if(syncep) {
			object_free(NULL, &syncevent_souls, syncep);
		}
		return ENOERROR; //status already set by sync_create()
	}

	if(syncep) {
		LINK2_PRI(syp->waiting, (THREAD*)syncep, THREAD);
	}

	// We don't know what kind of sync it is, so initialize it
	// to a free mutex. The lib will change it to what it should be.
	sync->__count = count;
	sync->__owner = type;

	return EOK;
}


int kdecl
ker_sync_destroy(THREAD *act, struct kerargs_sync_destroy *kap) {

	sync_t			*sync = kap->sync;

	WR_VERIFY_PTR(act, sync, sizeof(*sync));

#if defined(VARIANT_smp)
	// Reference sync so it will fault if not-present or invalid.
	// We do this with atomic_set rather than the usual
	// "WR_PROBE_INT(act, &sync->__owner, 1)" because another CPU in an
	// SMP system might be playing around with the memory at the same time
	// and the WR_PROBE_INT accessing is *not* SMP safe. Don't do this to
	// us again!
	atomic_set(&sync->__owner, 0);
#else
	WR_PROBE_INT(act, &sync->__owner, 1);
#endif

	// Check for a statically created mutex.
	if(sync->__owner == _NTO_SYNC_INITIALIZER) {
		// The lock is needed so if we are not ripped out
		// the kernel after setting it to _NTO_SYNC_DESTROYED (-2).
		lock_kernel();
		sync->__owner = _NTO_SYNC_DESTROYED;
		return EOK;
	}

	return sync_destroy(NULL, sync);
}

void
sync_mutex_lock(THREAD *act, sync_t *sync, int flags) {
	THREAD		*thp;
	PROCESS		*prp;
	SYNC		*syp;
	int			 id;
	unsigned	 owner;
	unsigned	*owp;
	unsigned	ceiling;
	struct syncevent_entry *syncep;

	// Verify the sync exists.
	if((syp = sync_lookup(sync, _NTO_SYNC_MUTEX_FREE)) == NULL) {
		return;
	}

	// This is needed in an SMP machine to make a thread which is
	// running on another machine and about to release the mutex
	// (while we are in this code) go into the kernel.
	owp = &sync->__owner;
	ceiling = 0;
	syncep = 0;
	if((sync->__count & _NTO_SYNC_PRIOCEILING) && !(flags & _MUTEXLOCK_FLAGS_NOCEILING)) {
		for(syncep = (struct syncevent_entry*) syp->waiting; syncep; syncep = syncep->next) {
			if(syncep->type == TYPE_SYNCEVENT && syncep->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING) {
				ceiling = syncep->priority;
				break;
			}
		}
		if(ceiling < act->priority) {
			lock_kernel();
			kererr(act, EINVAL);
			return;
		}
	}

retry:
	if(*owp == _NTO_SYNC_DESTROYED) {
		kererr(act, EINVAL);
		return;
	}
	atomic_set(owp, _NTO_SYNC_WAITING);
	lock_kernel();

	// Try and acquire the mutex. There are three return values.
	// _NTO_SYNC_WAITING   - we got the mutex and it is ours.
	// 0                   - we did not get it but should have
	// xxx                 - another thread has it
	owner = *owp;

	// Check if there are any waiting threads
	thp = syp->waiting;
	while (thp && thp->type != TYPE_THREAD) {
		thp = thp->next;
	}
	if(thp == NULL || thp->priority < act->priority || (act->flags & _NTO_TF_ACQUIRE_MUTEX)) {
		if((owner = _smp_cmpxchg(owp, 0, _NTO_SYNC_WAITING)) == _NTO_SYNC_WAITING) {
			act->timeout_flags = 0;
			*owp = SYNC_OWNER(act);
			if(ceiling) {
				// Mutex hold list is linked through the prioceiling syncevent
				thp = (THREAD *)syncep;
			}
			if(thp) {
				if(thp->args.mu.owner) {
					MUTEXHOLDLIST_REM(syp);
				}
				thp->args.mu.owner = *owp;
				*owp |= _NTO_SYNC_WAITING;
				MUTEXHOLDLIST_PRI(act->mutex_holdlist, syp);
			}
			if(ceiling) {
				adjust_priority(act, ceiling, act->dpp);
			}
			SETKSTATUS(act, EOK);
			return;
		}
			
    } else if(owner == _NTO_SYNC_MUTEX_FREE) {
		//Another CPU unlocked the thing on us
		goto retry;
	} else if(owner == _NTO_SYNC_WAITING) {
		if((thp = syp->waiting) && (thp->type == TYPE_THREAD || (thp->type == TYPE_SYNCEVENT && ((struct syncevent_entry *)thp)->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING))) {
			if(thp->args.mu.owner) {
				MUTEXHOLDLIST_REM(syp);
				thp->args.mu.owner = 0;
			}
			sync_wakeup(syp, 0);
			if((thp = syp->waiting) && (thp->type == TYPE_THREAD || (thp->type == TYPE_SYNCEVENT && ((struct syncevent_entry *)thp)->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING))) {
				thp->args.mu.owner = 0;
			}
		}
	}

	if(IMTO(act, STATE_MUTEX)) {
		kererr(act, ETIMEDOUT);
		return;
	}

	// Block waiting for the mutex to be unlocked.
	act->args.mu.mutex = sync;
	act->args.mu.saved_timeout_flags = 0;
	// Call unready() instead of block() the thread may not me active.
	unready(act, STATE_MUTEX);
	act->blocked_on = (void *) syp;
	if((thp = syp->waiting) && thp->type == TYPE_THREAD && act->priority > thp->priority) {
		if(thp->args.mu.owner) {
			MUTEXHOLDLIST_REM(syp);
			thp->args.mu.owner = 0;
		}
	}
	LINK2_PRI(syp->waiting, act, THREAD);

	// Check for priority inversion and avoid via priortity inheritence.
	if(sync->__count & _NTO_SYNC_PRIONONE) {
		act->args.mu.owner = 0;
	} else if(syp->waiting == act) {
		THREAD *thp2;

		act->args.mu.owner = owner;
		thp2 = act;
again:
		// Try and determine who is using it.
		thp = NULL;
		id = SYNC_PINDEX(owner);
		if(id  &&  id < process_vector.nentries  &&  VECP(prp, &process_vector, id)) {
			thp = vector_lookup(&prp->threads, SYNC_TID(owner));
		}

		if(thp) { 
			// adjust mutex hold list
			MUTEXHOLDLIST_PRI(thp->mutex_holdlist, syp);
		  	if(act->priority > thp->priority) {
				adjust_priority(thp, act->priority, thp->dpp);
				/* recursive checking */
chkstate:
				switch(thp->state) {
					case STATE_SEND:
					{
						CHANNEL *chp;
						int i;
	
						/* not recursively raise priority in send block case */
						chp = thp->blocked_on;
						if((chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
							for(i = 0 ; i < chp->process->threads.nentries ; ++i) {
								if(VECP(thp, &chp->process->threads, i)  &&  thp->priority < act->priority
								&&  thp->last_chid == chp->chid) {
									adjust_priority(thp, act->priority, thp->dpp);
								}
							}
						}
					}
					break;
	
					case STATE_REPLY:
					{
						CHANNEL *chp;

						/* recursively raise the priority of the server thread */
						chp = thp->blocked_on;
						if((chp->flags & _NTO_CHF_FIXED_PRIORITY) != 0) {
							break;
						}
						thp = thp->args.ms.server;
					  	if(thp && act->priority > thp->priority) {
							adjust_priority(thp, act->priority, thp->dpp);
							goto chkstate;
						}
					}
					break;
	
					case STATE_MUTEX:
						syp = thp->blocked_on;
						if(syp->waiting == thp || act->priority > syp->waiting->priority) {
							owner = syp->waiting->args.mu.owner;
							if(owner) {
								MUTEXHOLDLIST_REM(syp);
							}
							// adjust syp2 waiting queue for thp changed its priority
							if(syp->waiting != thp) {
								syp->waiting->args.mu.owner = 0;
								thp->args.mu.owner = owner;
								LINK2_REM(thp);
								LINK2_PRI(syp->waiting, thp, THREAD);
							}
							thp2 = thp;
							goto again;
						} else {
							LINK2_REM(thp);
							LINK2_PRI(syp->waiting, thp, THREAD);
						}
					break;
		
					default:
					break;
				}
			}
		} else {
			// no owner, no holding list
			thp2->args.mu.owner = 0;
		}
	} 

	SETKSTATUS(act, EOK);
}

int kdecl
ker_sync_mutex_lock(THREAD *act, struct kerargs_sync_mutex_lock *kap) {
	sync_t	*sync = kap->sync;

	sync_mutex_lock(act, sync, 0);
	act->args.mu.incr = 0; // C library takes care of manipulating the count
	return ENOERROR;
}

int kdecl
ker_sync_mutex_revive(THREAD*act, struct kerargs_sync_mutex_revive *kap) {
	sync_t	*sync = kap->sync;
	
	WR_VERIFY_PTR(act, sync, sizeof(sync_t));
	if(sync->__owner == _NTO_SYNC_DEAD) {
		WR_PROBE_INT(act, sync, sizeof(sync_t));
		lock_kernel();
		sync->__owner = _NTO_SYNC_WAITING;
		sync->__count = (sync->__count & ~_NTO_SYNC_COUNTMASK) | 1;
	   	act->flags |= _NTO_TF_ACQUIRE_MUTEX;
		sync_mutex_lock(act, sync, 0);
	   	act->flags &= ~_NTO_TF_ACQUIRE_MUTEX;
	} else {
		kererr(act, EINVAL);
	}
	return ENOERROR;
}


//
// This call is really more of a mutex check waiting call than an unlock call.
// It will of course also unlock a mutex, however, since the unlock is
// typically done via a cmpxchg in the user code the mutex is typically
// unlocked when you enter. In which case we do the main job of seeing
// who to wakeup. Those woken up will try and reacquire the mutex.
//
void
sync_mutex_unlock(THREAD *act, sync_t *sync) {
	SYNC		*syp;
	THREAD		*thp;
	unsigned	*owp = &sync->__owner;

	// Verify the sync exists.
	if((syp = sync_lookup(sync, _NTO_SYNC_MUTEX_FREE)) == NULL) {
		return;
	}

	// If I own it, then release it.
	if((*owp & ~_NTO_SYNC_WAITING) == SYNC_OWNER(act)) {
		// @@@ Should verify a write fault on mutex
		lock_kernel();
		if((thp = syp->waiting) &&
		  (thp->type == TYPE_THREAD || ((thp = thp->next) && (thp->type == TYPE_THREAD)))) {
			*owp = _NTO_SYNC_WAITING;
		} else {
			*owp = 0;
		}
	}

	lock_kernel();

	// Get the first waiting thread, make it ready and set a flag to force it
	// to re-acquire the mutex.
	if((thp = syp->waiting) && (thp->type == TYPE_THREAD || (thp->type == TYPE_SYNCEVENT && ((struct syncevent_entry *)thp)->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING))) {
		if(thp->args.mu.owner) {
			MUTEXHOLDLIST_REM(syp);
		}
		sync_wakeup(syp, 0);
		// Set waiting flag if anyone else is waiting.
		if((thp = syp->waiting)) {
			if(thp->type == TYPE_THREAD || (thp->next && thp->next->type == TYPE_THREAD)) {
				atomic_set(owp, _NTO_SYNC_WAITING);
				thp->args.mu.owner = 0;
			} else if(thp->type == TYPE_SYNCEVENT && ((struct syncevent_entry*)thp)->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING) {
				thp->args.mu.owner = 0;
			}
		}
	}

	// Check for inheritence and adjust priority back
	if(act->priority > act->real_priority) {
		int priority;
		THREAD * thp2;
		
		priority = act->real_priority;
		if((syp = act->mutex_holdlist) && (thp2 = syp->waiting)) {
			if(priority < thp2->priority) {
				priority = thp2->priority;
			}
		}
		if(act->priority > priority) {
			adjust_priority(act, priority, act->dpp);
		}
	}

	// Allow other thread at same priority equal access to mutex.
//**** removed because ker_sync_condvar_wait calls this
//	if(act == actives[KERNCPU])
//		yield();

	SETKSTATUS(act, EOK);
}

int kdecl
ker_sync_mutex_unlock(THREAD *act, struct kerargs_sync_mutex_unlock *kap) {
	sync_t	*sync = kap->sync;

	sync_mutex_unlock(act, sync);
	return ENOERROR;
}

int
mutex_set_prioceiling(SYNC *syp, unsigned ceiling, unsigned *ceiling_old) {
	struct syncevent_entry *syncep;

	for(syncep = (struct syncevent_entry*) syp->waiting; syncep; syncep = syncep->next) {			
		if(syncep->type == TYPE_SYNCEVENT && syncep->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING) {
			*ceiling_old = syncep->priority;
			syncep->priority = ceiling;
			return ENOERROR;
		}
	}
	return EINVAL;
}
		
int kdecl
ker_sync_ctl(THREAD *act, struct kerargs_sync_ctl *kap) {
	SYNC		*syp;
	PROCESS		*prp = act->process;
	struct syncevent_entry *syncep;
	struct sigevent *event;

	// Verify the sync exists.
	if((syp = sync_lookup(kap->sync, _NTO_SYNC_MUTEX_FREE)) == NULL) {
		return ENOERROR;
	}

	switch(kap->cmd) {
	case _NTO_SCTL_SETPRIOCEILING:
	{
		unsigned ceiling, ceiling_old;
		int ret;

		RD_VERIFY_PTR(act, kap->data, sizeof(int *));
		ceiling = *(int*)kap->data;
		if((ceiling < 1) || (ceiling >= NUM_PRI) || (prp->cred->info.euid != 0 && ceiling >= priv_prio)) {
			return EINVAL;
		}
		if(!(kap->sync->__count & _NTO_SYNC_PRIOCEILING)) {
			return EINVAL;
		}

		/* check if the calling thread has locked the mutex */
		if((kap->sync->__owner & ~_NTO_SYNC_WAITING) == SYNC_OWNER(act)) {
			/* change the ceiling and return */
			lock_kernel();
			ret = mutex_set_prioceiling(syp, ceiling, &ceiling_old);
			SETKSTATUS(act, ceiling_old);
			return ret;
		}

		/* try to lock the mutex */
		sync_mutex_lock(act, kap->sync, _MUTEXLOCK_FLAGS_NOCEILING);

		/* kernel is locked at this point by sync_mutex_lock */
		/* check if the mutex is locked successfully */
		if((kap->sync->__owner & ~_NTO_SYNC_WAITING) == SYNC_OWNER(act)) {
			/* change the ceiling, unlock the mutex, and return */
			ret = mutex_set_prioceiling(syp, ceiling, &ceiling_old);
			sync_mutex_unlock(act, kap->sync);
			SETKSTATUS(act, ceiling_old);
			return ret;
		}
		
		/* set the specret flag, let the thread change the ceiling when specret */
		act->flags |= _NTO_TF_MUTEX_CEILING;
		act->args.mu.ceiling = ceiling;

		return ENOERROR;
	}

	case _NTO_SCTL_GETPRIOCEILING:
		WR_VERIFY_PTR(act, kap->data, sizeof(int *));
		WR_PROBE_OPT(act, kap->data, sizeof(int *));
		if(!(kap->sync->__count & _NTO_SYNC_PRIOCEILING)) {
			return EINVAL;
		}
		for(syncep = (struct syncevent_entry*) syp->waiting; syncep; syncep = syncep->next) {
			if(syncep->type == TYPE_SYNCEVENT && syncep->subtype == SYNCEVENT_SUBTYPE_PRIORITYCEILING) {
				*(int*)kap->data = syncep->priority;
				return EOK;  	
			}
		}
		return EINVAL;

	case _NTO_SCTL_SETEVENT:
	{
		int dead = (kap->sync->__owner == _NTO_SYNC_DEAD);
		
		event = (struct sigevent*) kap->data;
		if(event != NULL) {
			RD_VERIFY_PTR(act, event, sizeof(struct sigevent));
			RD_PROBE_INT(act, event, sizeof(struct sigevent)/sizeof(int));
		}

		// search the queue for an exsisting event
		for(syncep = (struct syncevent_entry*) syp->waiting; syncep; syncep = syncep->next) {
			if( syncep->un.ev.process == prp && syncep->type == TYPE_SYNCEVENT 
				&& syncep->subtype == SYNCEVENT_SUBTYPE_EVENT)
			/*PR46285  allow any thread in same process to change/delete the sigevent */{
				if(event == NULL || SIGEV_GET_TYPE(event) == SIGEV_NONE) {
					lock_kernel();
					// release
					LINK2_REM(syncep);
					object_free(prp, &syncevent_souls, syncep);
				} else {
					struct sigevent tmp;
					
					// replace 
					memcpy(&tmp, event, sizeof(struct sigevent)); //to prevent a patial copy when error
					lock_kernel();
					memcpy(&syncep->un.ev.event, &tmp, sizeof(struct sigevent));
					if(dead) {
						sigevent_exe(&syncep->un.ev.event, act, 1);
					}
				}
				return EOK;
			}
		}
		
		if(event == NULL || SIGEV_GET_TYPE(event) == SIGEV_NONE) {
			return EOK;
		}

		lock_kernel();
		if((syncep = object_alloc(prp, &syncevent_souls)) == NULL) {
			return EAGAIN;
		}

		syncep->type = TYPE_SYNCEVENT;
		syncep->subtype = SYNCEVENT_SUBTYPE_EVENT;
		syncep->un.ev.process = prp;
		syncep->un.ev.tid = act->tid;
		memcpy(&syncep->un.ev.event, event, sizeof(struct sigevent));
		LINK2_PRI(syp->waiting, (THREAD*)syncep, THREAD);

		if(dead) {
			sigevent_exe(&syncep->un.ev.event, act, 1);
		}

		return EOK;
	}

	default:
		return ENOSYS;
	}
}


int kdecl
ker_sync_condvar_wait(THREAD *act, struct kerargs_sync_condvar_wait *kap) {
	SYNC		*syp;
	sync_t		*sync;

	// Verify the sync exists.
	if((syp = sync_lookup(sync = kap->sync, _NTO_SYNC_COND)) == NULL) {
		return ENOERROR;
	}

	// For compatibility with older libraries.
	if(sync->__owner == 0) {
		sync->__owner = _NTO_SYNC_COND;
	}

	// Verify it is a condvar.
	if(sync->__owner != _NTO_SYNC_COND) {
		return EINVAL;
	}

	// Make sure I own the attached mutex!
	if((kap->mutex->__owner & ~_NTO_SYNC_WAITING) != SYNC_OWNER(act)) {
		return EINVAL;
	}
	

	if(PENDCAN(act->un.lcl.tls->__flags)) {
		lock_kernel();
		SETKIP_FUNC(act, act->process->canstub);
		return ENOERROR;
	}

	// Release the attached mutex. sync_mutex_unlock will call lock_kernel();
	sync_mutex_unlock(act, kap->mutex);
	if(act->flags & _NTO_TF_KERERR_SET) {
		// If the sync_mutex_unlock signalled an error on the thread,
		// we need to abort the rest of the operation.
		return ENOERROR;
	}

	act->flags |= _NTO_TF_ACQUIRE_MUTEX;
	act->args.mu.mutex = kap->mutex;
	act->args.mu.saved_timeout_flags = 0;
	act->args.mu.owner = 0;
	if((kap->mutex->__count & _NTO_SYNC_COUNTMASK) > 0) {
		// The kernel is responsible for manipulating the mutex count field 
		// while handling a condvar wait.
		act->args.mu.incr = 1;
		kap->mutex->__count -= 1;
	} else {
		// If the count is zero, silly user used the inline _mutex_lock()
		// to acquire the mutex and we don't want to fiddle the count
		// when the condvar wakes up.
		act->args.mu.incr = 0;
	}

	//
	// POSIX says that we should release and re-acquire the mutex in the
	// timeout case.
	//
	if(IMTO(act, STATE_CONDVAR)) {
		if(act == actives[KERNCPU]) {
			yield();
		}
		return ETIMEDOUT;
	}

	// Block waiting for the condvar to be signaled.
	// Call unready() instead of block() the thread may not me active.
	unready(act, STATE_CONDVAR);

	act->blocked_on = (void *) syp;
	LINK2_PRI(syp->waiting, act, THREAD);
	return EOK;
}


int kdecl
ker_sync_condvar_signal(THREAD *act, struct kerargs_sync_condvar_signal *kap) {
	SYNC		*syp;
	sync_t		*sync;
	int			 all = (volatile int) kap->all;

	// Verify the sync exists.
	if((syp = sync_lookup(sync = kap->sync, _NTO_SYNC_COND)) == NULL) {
		return ENOERROR;
	}

	// For compatibility with older libraries
	if(sync->__owner == 0) {
		sync->__owner = _NTO_SYNC_COND;
	}

	// Verify it is a condvar.
	if(sync->__owner != _NTO_SYNC_COND) {
		return EINVAL;
	}

	lock_kernel();

	// Get the waiting thread/threads and and make it/them ready.
	sync_wakeup(syp, all);

	return EOK;
}


int kdecl
ker_sync_sem_wait(THREAD *act, struct kerargs_sync_sem_wait *kap) {
	SYNC		*syp;
	sync_t		*sync;
	unsigned	tls_flags;

	// Verify the sync exists (pass _NTO_SYNC_INITIALIZER to stop auto-create)
	if((syp = sync_lookup(sync = kap->sync, _NTO_SYNC_INITIALIZER)) == NULL) {
		return ENOERROR;
	}

	// Verify it is a semaphore (Old libraries set this to zero);
	if(sync->__owner != _NTO_SYNC_SEM && sync->__owner != 0) {
		return EINVAL;
	}

	// Is the semaphore available.
	if(sync->__count > 0) {
		// Yes. Decrement and return.
		WR_PROBE_INT(act, &sync->__count, 1);
		lock_kernel();

		--sync->__count;
		act->timeout_flags = 0;
		return EOK;
	}

	// If a trywait was executed we don't block.
	if(kap->try) {
		return EAGAIN;
	}
	tls_flags = act->un.lcl.tls->__flags;
	lock_kernel();

	if(IMTO(act, STATE_SEM)) {
		return ETIMEDOUT;
	}

	if(PENDCAN(tls_flags)) {
		SETKIP_FUNC(act, act->process->canstub);
		return ENOERROR;
	}

	// Block waiting for the semaphore to be posted.
	act->state = STATE_SEM;
	_TRACE_TH_EMIT_STATE(act, SEM)
	block();
	act->blocked_on = (void *)syp;
	act->args.mu.owner = 0;
	LINK2_PRI(syp->waiting, act, THREAD);
	return EOK;
}


int kdecl
ker_sync_sem_post(THREAD *act, struct kerargs_sync_sem_post *kap) {
	SYNC		*syp;
	sync_t		*sync;

	// Verify the sync exists (pass _NTO_SYNC_INITIALIZER to stop auto-create)
	if((syp = sync_lookup(sync = kap->sync, _NTO_SYNC_INITIALIZER)) == NULL) {
		return ENOERROR;
	}

	// Verify it is a semaphore (Old libraries set this to zero);
	if(sync->__owner != _NTO_SYNC_SEM && sync->__owner != 0) {
		return EINVAL;
	}

	WR_PROBE_INT(act, &sync->__count, 1);	// Touch it before locking the kernel.
	lock_kernel();

	// Get a waiting thread and and make it ready.
	if(syp->waiting) {
		sync_wakeup(syp, 0);
	} else {
		// @@@ Should verify a write fault
		++sync->__count;	// Nobody woke up so increment count
	}

	return EOK;
}

__SRCVERSION("ker_sync.c $Rev: 156323 $");
