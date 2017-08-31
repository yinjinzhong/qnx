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


int kdecl
ker_channel_create(THREAD *act, struct kerargs_channel_create *kap) {
	PROCESS	*prp;
	CHANNEL	*chp;
	int		 chid;
	SOUL	*souls;
	VECTOR	*chvec;

	prp = act->process;
	if((kap->flags & (_NTO_CHF_THREAD_DEATH|_NTO_CHF_COID_DISCONNECT))  &&  prp->death_chp) {
		return EBUSY;
	}

	souls = &channel_souls;
	chvec = &prp->chancons;
	if(kap->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL)) {
		souls = &chasync_souls;
		if(kap->event != NULL) {
			RD_PROBE_INT(act, kap->event, sizeof(*kap->event)/sizeof(int));
		}
		if(kap->flags & _NTO_CHF_GLOBAL) {
			prp = NULL;
			souls = &chgbl_souls;
			chvec = &chgbl_vector;
			if(kap->cred != NULL) {
				RD_PROBE_INT(act, kap->cred, sizeof(*kap->cred)/sizeof(int));
			}
		}
	}

	lock_kernel();
	// Allocate a channel entry.
	if((chp = object_alloc(prp, souls)) == NULL) {
		return EAGAIN;
	}

	chp->type = TYPE_CHANNEL;
	chp->process = prp;
	chp->flags = kap->flags;

if(kap->flags & _NTO_CHF_GLOBAL) {
	if(chp->reply_queue) crash();
}	
	// Check if this is the network managers msg channel.
	// Don't do this until we know the channel's fully created, so
	// we don't have to worry about net.* fields not getting cleared on
	// an error.
	if(kap->flags & _NTO_CHF_NET_MSG) {
		if(!kerisroot(act)) {
			object_free(prp, &channel_souls, chp);
			return EPERM;
		}
		if(net.prp  ||  net.chp) {
			object_free(prp, &channel_souls, chp);
			return EAGAIN;
		}
		net.prp = prp;
		net.chp = chp;
	}

	// Add the channel to the channels vector.
	if((chid = vector_add(chvec, chp, 1)) == -1) {
		object_free(prp, &channel_souls, chp);
		return EAGAIN;
	}

	if(kap->flags & (_NTO_CHF_THREAD_DEATH | _NTO_CHF_COID_DISCONNECT)) {
		if(prp) { /* not global channel */
			prp->death_chp = chp;
		}
	}

	if(kap->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL)) {
		if(kap->flags & _NTO_CHF_GLOBAL) {
			CHANNELGBL *chgbl = (CHANNELGBL*)chp;

			chid |= _NTO_GLOBAL_CHANNEL;
			chgbl->event.sigev_notify = SIGEV_NONE;
			chgbl->max_num_buffer = kap->maxbuf;
			chgbl->buffer_size = kap->bufsize;
			if(kap->event != NULL) {
				memcpy(&chgbl->event, kap->event, sizeof(*kap->event));
				chgbl->ev_prp = act->process;
			}
			if(kap->cred) {
				memcpy(&chgbl->cred, kap->cred, sizeof(chgbl->cred));
			}
		} else {
			((CHANNELASYNC*)chp)->event.sigev_notify = SIGEV_NONE;
			if(kap->event != NULL) {
				memcpy(&((CHANNELASYNC*)chp)->event, kap->event, sizeof(*kap->event));
				((CHANNELASYNC*)chp)->ev_prp = act->process;
			}
		}
	}
	chp->chid = chid;

	SETKSTATUS(act, chid);
	return ENOERROR;
}

#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
static void clear_msgxfer(THREAD *thp) {

	switch(thp->state) {
		case STATE_REPLY: 
			thp->flags &= ~_NTO_TF_UNBLOCK_REQ;
			/* Fall Through */

		case STATE_SEND:
		{
			CONNECT *cop = thp->blocked_on;
			if(--cop->links == 0) {
				connect_detach(cop, thp->priority);
			}
		}

		/* fall through */
		case STATE_RECEIVE:
			thp->internal_flags &= ~_NTO_ITF_RCVPULSE;
			thp->restart = NULL;
			break;
		default:
#ifndef NDEBUG
			kprintf("\nclear_msgxfer: should not reach here. thread state %x\n",thp->state);
#endif
			return;
	}
	thp->state = STATE_READY;
	LINK2_REM(thp);
}
#endif

int kdecl
ker_channel_destroy(THREAD *act, struct kerargs_channel_destroy *kap) {
	PROCESS	*prp;
	CHANNEL	*chp;
	CONNECT	*cop;
	THREAD	*thp;
	int		i, chid;
	SOUL	*souls;
	VECTOR	*chvec;

	prp = act->process;

	chid = kap->chid;
	chvec = &prp->chancons;
	if(chid & _NTO_GLOBAL_CHANNEL) {
		chid &= ~_NTO_GLOBAL_CHANNEL;
		chvec = &chgbl_vector;
	}
	
	// Make sure the channel is valid.
	if((chp = vector_lookup2(chvec, chid)) == NULL  ||
		chp->type != TYPE_CHANNEL) {
		return EINVAL;
	}

	lock_kernel();

	// Mark channel as unavailable.
	vector_flag(chvec, chid, 1);
	// Check if the network manager is releasing his message channel.
	if(prp == net.prp  &&  chp == net.chp) {
		for(i = 0; i < vthread_vector.nentries; ++i) {
			VTHREAD		*vthp;

			if((vthp = vector_rem(&vthread_vector, i))) {
				if(force_ready((THREAD *)(void *)vthp, EINTR | _FORCE_KILL_SELF)) {
					object_free(NULL, &vthread_souls, vthp);
				} else {
					vector_add(&vthread_vector, vthp, i);
					return EAGAIN;
				}
			}
			KER_PREEMPT(act, ENOERROR);
		}
		net.prp = NULL;
		net.chp = NULL;
	}

	// Check for destruction of the death channel.
	if(chp == prp->death_chp) {
		prp->death_chp = NULL;
	}

if(!(chp->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL))) {
	// Unblock all vthreads send blocked on the channel
	for(thp = chp->send_queue; thp; thp = thp->next) {
		if(thp->type == TYPE_VTHREAD) {
			SETKIP(act, KIP(act) - KER_ENTRY_SIZE);
			act->flags |= _NTO_TF_KERERR_SET | _NTO_TF_KERERR_LOCK;
			(void)net_send2((KERARGS *)(void *)kap, thp->un.net.vtid, thp->blocked_on, thp);
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
				clear_msgxfer(thp);
			}
#endif
			return ENOERROR;
		}
		KER_PREEMPT(act, ENOERROR);
	}

	// Unblock all vthreads reply blocked on the channel
	for(thp = chp->reply_queue; thp; thp = thp->next) {
		if(thp->type == TYPE_VTHREAD) {
			SETKIP(act, KIP(act) - KER_ENTRY_SIZE);
			act->flags |= _NTO_TF_KERERR_SET | _NTO_TF_KERERR_LOCK;
			(void)net_send2((KERARGS *)(void *)kap, thp->un.net.vtid, thp->blocked_on, thp);
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
				clear_msgxfer(thp);
			}
#endif
			return ENOERROR;
		}
		KER_PREEMPT(act, ENOERROR);
	}
}

	// Find all server connections and mark their channels as NULL.
if(!(chp->flags & _NTO_CHF_GLOBAL)) {
	for(i = 0 ; i < prp->chancons.nentries ; ++i) {
		if((cop = VECP2(cop, &prp->chancons, i))) {
			if(cop->type == TYPE_CONNECTION  &&  cop->channel == chp  &&  cop->scoid == i) {

				// Remove the server connection from the servers channels vector.
				vector_rem(&prp->chancons, i);

				if(cop->links == 0) {
					// Remove server connection object.
					object_free(prp, &connect_souls, cop);
				} else {
					do {
						if(cop->process  &&  cop->process->death_chp) {
							connect_coid_disconnect(cop, cop->process->death_chp,
									act->priority);
						}
						// Null channel pointer since it is gone.
						cop->channel = NULL;
                                        } while((cop = cop->next) && (!(chp->flags & _NTO_CHF_ASYNC)));
				}
			}
		}
		KER_PREEMPT(act, ENOERROR);
	}
} else {
		for(cop = ((CHANNELGBL*) chp)->cons;cop;cop=cop->next) {
			cop->channel = NULL;
			cop->next = NULL;
		}
}

	// Unblock all threads receive blocked on the channel
	while((thp=chp->receive_queue)) {
		force_ready(thp, ESRCH);
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
		if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
			clear_msgxfer(thp);
		}
#endif
		KER_PREEMPT(act, ENOERROR);
	}

	// Unblock all threads send blocked on the channel
	while((thp=chp->send_queue)) {
		force_ready(thp, ESRCH);
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
		if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
			clear_msgxfer(thp);
		}
#endif
		KER_PREEMPT(act, ENOERROR);
	}

if(!(chp->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL))) {
	// Unblock all threads reply blocked on the channel
	while((thp = chp->reply_queue)) {
		// Next line prevents an unblock pulse being send and ensures
		// you will be removed from the queue.
		thp->state = STATE_SEND;
		_TRACE_TH_EMIT_STATE(thp, SEND);
		force_ready(thp, ESRCH | _FORCE_SET_ERROR);
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
		if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
			clear_msgxfer(thp);
		}
#endif
		KER_PREEMPT(act, ENOERROR);
	}
} else {
	// release all packages/send requests
	if(chp->flags & _NTO_CHF_GLOBAL) {
		struct gblmsg_entry *gblmsg;
		CHANNELGBL *chgbl = (CHANNELGBL*)chp;

		if(chgbl->free != NULL) {
			_sfree(chgbl->free, chgbl->buffer_size + sizeof(*gblmsg));
		}
		while((gblmsg = (struct gblmsg_entry*) chp->reply_queue)) {
			chp->reply_queue = (void*)gblmsg->next;
			_sfree(gblmsg, chgbl->buffer_size + sizeof(*gblmsg));

		}
	} else {
	}
}

	// Remove the channel from the servers channels vector.
	if((chp = vector_rem(chvec, chid)) == NULL) {
		return EINVAL;
	}

	souls = &channel_souls;
	if(chp->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL)) {
		souls = &chasync_souls;
		if(chp->flags & _NTO_CHF_GLOBAL) {
			souls = &chgbl_souls;
		}
	}

	// Release the channel entry.
	object_free(prp, souls, chp);

	return EOK;
}

__SRCVERSION("ker_channel.c $Rev: 153052 $");
