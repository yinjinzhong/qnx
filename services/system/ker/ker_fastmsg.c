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

#include <unistd.h>
#include "externs.h"

/*
 * New message passing code.
 * Faster version than original -- optimizes for the common resmgr cases:
 * - process to process msgpass
 * - small messages (less than 128-256 bytes) or...
 * - larger messages with multi-IOV
 * - normally, offsets into copy routine are zero
 * - msginfo is requested
 * - we rarely get send-blocked
 * - send and reply length are requested
 * 
 * It improves performance by doing the following
 * For short messaged:
 * - dramatically increases the range of messages handled through the 
 *   short message path (from 32 up to about 256 bytes)
 * - allows the handling of multi-IOV short messages
 * - the 2 changes above typically allow handling of 95%+ of send and 
 *   80%+ of replies in the fast path (it was less than 50% with old code)
 * Large messages are improved as follows:
 * - make a copy of the incoming IOVs before blocking (up to a max)
 * - the fast copy routine now integrates the aspace map and copy in one
 * - we branch off early after checking that we meet conditions above
 * 
 * We also remove the specret processing if possible. This leads to 
 * some simplifications, and also solves some of the issues with the non-atomicity of 
 * the message xfer code in the kernel (a reply to a thread sneaking in between 
 * the receive and specret path, for example)
 * 
 * 
 */

/*
 * memcpy routines inlined
 * NYI -- this should be moved to kercpu.h for all architectures
 */

#ifndef __inline_xfer_memcpy
#define __inline_xfer_memcpy(a,b,c) memcpy(a,b,c)
#endif

/* Fast path rcvinfo stuffing for the local case with no pending unblock */

#if defined(__BIGENDIAN__)
	#define RCVINFO_ENDIAN_FLAG		(_NTO_MI_ENDIAN_BIG)
#elif defined(__LITTLEENDIAN__)
	#define RCVINFO_ENDIAN_FLAG		(0)
#else
#error ENDIAN Not defined for system
#endif

#define STUFF_RCVINFO(thp, cop, info) 							\
	{															\
		struct _msg_info *rep = (info);							\
																\
		if((cop->flags & COF_NETCON && thp->type == TYPE_VTHREAD) || \
		   (act->process == net.prp &&                      	\
		   (cop)->channel->chid == net.chp->chid &&             \
		   ((cop)->flags & COF_NETCON) == 0)) {                 \
			get_rcvinfo(thp, -1, cop, info);					\
		} else {												\
			rep->nd = ND_LOCAL_NODE;							\
			rep->srcnd = (cop)->un.lcl.nd;						\
			rep->pid = (cop)->process->pid;						\
			rep->tid = (thp)->tid + 1;							\
			rep->chid = (cop)->channel->chid;					\
			rep->scoid = (cop)->scoid | _NTO_SIDE_CHANNEL;		\
			rep->coid = (thp)->args.ms.coid;					\
			rep->msglen = (thp)->args.ms.msglen;				\
			rep->srcmsglen = ((thp)->flags & _NTO_TF_BUFF_MSG) ?	\
				(thp)->args.ms.msglen : (thp)->args.ms.srcmsglen;	\
			rep->dstmsglen = (thp)->args.ms.dstmsglen;			\
			rep->priority = (thp)->priority;					\
			rep->flags = RCVINFO_ENDIAN_FLAG;					\
		}														\
	}


#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
#define		START_SMP_XFER(thp1, thp2)							\
	{															\
			*(volatile void**) &((thp1)->blocked_on) = (thp2);							\
			*(volatile int_fl_t*) &((thp1)->internal_flags) |= _NTO_ITF_MSG_DELIVERY;	\
			*(volatile int_fl_t*) &((thp2)->internal_flags) |= _NTO_ITF_MSG_DELIVERY;	\
	}
	
#define		END_SMP_XFER(thp1, thp2)							\
	{															\
			*(volatile int_fl_t*) &((thp2)->internal_flags) &= ~_NTO_ITF_MSG_DELIVERY;	\
			*(volatile int_fl_t*) &((thp1)->internal_flags) &= ~_NTO_ITF_MSG_DELIVERY;	\
			*(volatile void**) &((thp1)->blocked_on) = (0);								\
	}

#else
#define 	START_SMP_XFER(act, thp)
#define 	END_SMP_XFER(act, thp)
#endif

/*
 * Handle CPU memmgr.aspace implementations that need the kernel locked
 */
#if defined(__ARM__) || defined(__MIPS__)
#define	SWITCH_ASPACE(aspace_prp, aspacep, act)		\
	{												\
		lock_kernel();								\
		memmgr.aspace(aspace_prp, aspacep);	\
		unlock_kernel();							\
		KER_PREEMPT(act, ENOERROR);					\
	}
#elif defined(__PPC__) || defined(__X86__) || defined(__SH__)
#define	SWITCH_ASPACE(aspace_prp, aspacep, act)		\
	memmgr.aspace(aspace_prp, aspacep)
#else
#error Is it safe to call memmgr.aspace without kernel lock?
#endif

static 
#if defined(__GNUC__) || defined(__INTEL_COMPILER)
__inline__ 
#endif
CONNECT * rdecl
inline_lookup_connect(PROCESS *prp, int coid) {
	VECTOR	*vec;
	CONNECT	*cop;

	if(coid & _NTO_SIDE_CHANNEL) {
		coid &= ~_NTO_SIDE_CHANNEL;
		vec = &prp->chancons;
	} else {
		vec = &prp->fdcons;
	}

	/* Do unsigned compare with 'coid' to catch negative values as well */
	if(vec  &&  (unsigned)coid < vec->nentries  &&  VECAND(cop = VEC(vec, coid), 1) == 0) {
		return(VECAND(cop, ~3));
	}
	
	return(NULL);
}

int kdecl
ker_msg_sendv(THREAD *act, struct kerargs_msg_sendv *kap) {
	CONNECT		*cop;
	CHANNEL		*chp;
	int			 type = KTYPE(act);
	THREAD		*thp, *act2;
	PROCESS		*actprp = act->process;
	unsigned	th_flags = 0;
	uint32_t	fast_path, net_srcmsglen = -1U;

	/*
	 * These are the usual incoming checks
	 *  - validate connection
	 *  - get channel pointer
	 *  - check for cancellation
	 */
	 
	// Lookup src connect.
	if((cop = inline_lookup_connect(actprp, kap->coid)) == NULL || cop->type != TYPE_CONNECTION) {
		return EBADF;
	}

	// Get dst channel.
	if((chp = cop->channel) == NULL) {
		return EBADF;
	}

	 _TRACE_COMM_EMIT_SMSG(act, cop, (act->tid << 16) | cop->scoid);

	if(PENDCAN(act->un.lcl.tls->__flags) &&  (type != __KER_MSG_SENDVNC)) {
		lock_kernel();
		SETKIP_FUNC(act, act->process->canstub);
		return ENOERROR;
	}

	/*
	 * The base conditions are now met. If this is a netcon or async channel,
	 * we handle separately
	 */
	 if(chp->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL)) {
		if(chp->flags & _NTO_CHF_GLOBAL) {
			return msgsend_gbl(act, cop, kap->smsg, -kap->sparts, (unsigned)-kap->rparts, kap->coid);
		} else {
			return msgsend_async(act, cop);
		}
	 }
	 
	 act2 = act;

	// Store incoming args
	if(cop->flags & COF_NETCON) {
		RD_PROBE_INT(act, kap->rmsg, sizeof(struct _vtid_info) / sizeof(int));
		if(!(act = (THREAD *)(void *)net_send1(kap->rparts, (struct _vtid_info *)(void *)kap->rmsg))) {
			return EINVAL;
		}
		if(act->state != STATE_STOPPED) crash();
		act->args.ms.rmsg = kap->rmsg;
		act->args.ms.rparts = kap->rparts;
		act2->args.ms.smsg = kap->smsg;
		act2->args.ms.sparts = kap->sparts;
		// Do this up-front while we have addressabilty
		net_srcmsglen = ((struct _vtid_info *)(void *)kap->rmsg)->srcmsglen;
	} else {
		act->args.ms.coid = kap->coid;
		act->args.ms.rmsg = kap->rmsg;
		act->args.ms.rparts = kap->rparts;
	}

	act->flags &= ~_NTO_TF_BUFF_MSG;
	// Make sure the SPECRET_PENDING bit isn't set when we don't need it.
	act->internal_flags &= ~_NTO_ITF_SPECRET_PENDING;
	
	// Validate incoming IOVs - override for QNET case - rparts/rmsg have special meaning
	if(cop->flags & COF_NETCON) {
		act->args.ms.dstmsglen = ((struct _vtid_info *)(void *)kap->rmsg)->dstmsglen;
	} else if(kap->rparts >= 0) {
		int len = 0;
		IOV *iov = kap->rmsg;
		int rparts = kap->rparts;
		int niov = 0;
		
		// Incoming reply IOV -- make copy of reply IOVs
		// Calculate reply length -- even if not requested, it is almost free
		// Also do boundary check
		while(rparts) {
			uintptr_t base, last;
			
			len += GETIOVLEN(iov);
			base = (uintptr_t)GETIOVBASE(iov);
			last = base + GETIOVLEN(iov) - 1;
			if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (GETIOVLEN(iov) != 0)) {
				return EFAULT;
			}
			// Keep copy of IOV
			if(niov < _NUM_CACHED_REPLY_IOV) {
			//	act->args.ms.riov[niov] = *iov;
			}
			++iov;
			++niov;
			--rparts;
		}
		act->args.ms.dstmsglen = len;
	} else {
		// Single part -- validate and store reply address
		uintptr_t base, last;
		base = (uintptr_t) kap->rmsg;
		last = base + (-kap->rparts) - 1;
		if((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) {
			// We know length is non-zero from test above
			return EFAULT;
		}
		act->args.ms.dstmsglen = -kap->rparts;
	}

	
	/* Send IOVs */
	if(kap->sparts < 0) {
		// Single part -- do the boundary check and copy if short message
		uintptr_t base, last;
		int	len;
		
		base = (uintptr_t) kap->smsg;
		len = -kap->sparts;
		last = base + len - 1;
		if((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) {
			// We know length is non-zero from test above
			return EFAULT;
		}
		act->args.ms.srcmsglen = len;

		if(len <= sizeof(act->args.msbuff.buff)) {
			(void)__inline_xfer_memcpy(act->args.msbuff.buff, (char *)base, act->args.msbuff.msglen = len);
			th_flags = _NTO_TF_BUFF_MSG;
		}
	} else if(kap->sparts == 1) {
		// Single IOV -- do the boundary check and copy if short message
		uintptr_t base, last, len;
		
		base = (uintptr_t)GETIOVBASE(kap->smsg);
		len = GETIOVLEN(kap->smsg);
		last = base + len - 1;
		if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (len != 0)) {
			return EFAULT;
		}
		act->args.ms.srcmsglen = len;
		if(len <= sizeof(act->args.msbuff.buff)) {
			(void)__inline_xfer_memcpy(act->args.msbuff.buff, (char *)base, act->args.ms.msglen = len);
			th_flags = _NTO_TF_BUFF_MSG;
		}
	} else {
		// Multi IOV case
		int len = 0;
		IOV *iov = kap->smsg;
		int sparts = kap->sparts;
		
		// Calculate send length -- even if not requested, it is almost free
		// Also do boundary check
		while(sparts) {
			uintptr_t base, last;
			
			len += GETIOVLEN(iov);
			base = (uintptr_t)GETIOVBASE(iov);
			last = base + GETIOVLEN(iov) - 1;
			if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (GETIOVLEN(iov) != 0)) {
				return EFAULT;
			}
			++iov;
			--sparts;
			// Keep copy of IOV -- NYI, only really need if no receiver
			//if(niov < _NUM_CACHED_SEND_IOV) {
			//	act->args.ms.siov[niov] = *iov;
			//}
		}
		act->args.ms.srcmsglen = len;
		if(len <= sizeof(act->args.msbuff.buff)) {
			int pos = 0;
			iov = kap->smsg;
			sparts = kap->sparts;
			// Multi-IOV incoming message that is short
			// FIXME -- need memcpy_siov for efficiency
			while(sparts) {
				int ilen = GETIOVLEN(iov);
				__inline_xfer_memcpy(&act->args.msbuff.buff[pos], GETIOVBASE(iov), ilen);
				
				pos += ilen;
				iov++;
				sparts--;
			}
			act->args.ms.msglen = len;
			th_flags = _NTO_TF_BUFF_MSG;
		}
	}

	// Now that the up-front business is done, we do the actual copy. If
	// this was identified as a short message, we have copied the message into the msgbuff area.

	// Was there was a waiting thread on the channel?
	
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)  
	for(thp = chp->receive_queue; thp && (thp->internal_flags & _NTO_ITF_MSG_DELIVERY); thp = thp->next) {
	// nothing to do
	}
	if(thp && !(thp->internal_flags & _NTO_ITF_RCVPULSE) ) {
		
#else		
	if((thp = chp->receive_queue) && !(thp->internal_flags & _NTO_ITF_RCVPULSE)) {
#endif	
		int xferstat;
		// If an immediate timeout was specified we return immediately.
		if(IMTO(act2, STATE_REPLY)) {
			act->flags &= ~_NTO_TF_BUFF_MSG;
			return ETIMEDOUT;
		}

		// Is this a long message?
		if(th_flags == 0) {
			act->args.ms.smsg = kap->smsg;
			act->args.ms.sparts = kap->sparts;
			START_SMP_XFER(act2, thp);
			// Yes. Transfer the data.
			xferstat = xfermsg(thp, act2, 0, 0);
			act->args.ms.msglen = act2->args.ms.msglen;

			lock_kernel();
			END_SMP_XFER(act2, thp);
			
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_FORCE_RDY) {
				force_ready(thp,KSTATUS(thp));
				thp->internal_flags &= ~_NTO_ITF_MSG_FORCE_RDY;
				KERCALL_RESTART(act2);
				act2->restart = 0;
				return ENOERROR;
			}
			if(act2->flags & (_NTO_TF_SIG_ACTIVE | _NTO_TF_CANCELSELF)) {
				/* send is a cancelation point */
				KERCALL_RESTART(act2);
				act2->restart = 0;
				return ENOERROR;
			}
#endif

			if(xferstat) {
				lock_kernel();
				// If sender faulted let him know and abort the operation
				// without waking up the receiver.
				if(xferstat & XFER_SRC_FAULT) {
					goto send_fault;
				}
				// If receiver faulted, wake him up with an error and fail the
				// send.
				goto rcv_fault;
			}
		} else {

			// Short message. We do the following:
			// - switch aspace to receiver
			if(thp->aspace_prp && thp->aspace_prp != aspaces_prp[KERNCPU]) {
				/*
				 * Lock/unlock kernel if necessary before calling memmgr.aspace
				 */
				SWITCH_ASPACE(thp->aspace_prp, &aspaces_prp[KERNCPU], act);
			}
			// - copy message and handle errors
			if((xferstat = xfer_cpy_diov(thp, thp->args.ri.rmsg, act->args.msbuff.buff, thp->args.ri.rparts, act->args.msbuff.msglen))) {
				lock_kernel();
				// Has to be a receiver fault;
				goto rcv_fault;
			}
			act->flags |= _NTO_TF_BUFF_MSG;
			// Note: below this point, we should NOT reference kap anywhere
			// as kap points to the original aspace
		}
		

		// If the receive specified an info buffer stuff it as well.
		// However, we are not in the address space of the destination
		// thread, we switch now
		thp->restart = NULL;
		
		if(thp->args.ri.info)  {
			struct _msg_info *repp = thp->args.ri.info;
			// Fill in rcvinfo
			// Switch to aspace of receiver. It's already adjusted if short msg.
			if(th_flags == 0) {
				if(thp->aspace_prp && thp->aspace_prp != aspaces_prp[KERNCPU]) {
					/*
					 * Kernel is already locked so we don't need SWITCH_ASPACE
					 */
					memmgr.aspace(thp->aspace_prp,&aspaces_prp[KERNCPU]);
				}
				if(cop->flags & COF_NETCON) {
					// Note: have to adjust srcmsglen before stuffing rcvinfo!
					act->args.ms.srcmsglen = net_srcmsglen;
				}
			}
			// We can use a fast inline version as we know the thread does not 
			// have an unblock pending
			STUFF_RCVINFO(act, cop, thp->args.ri.info);

			// RUSH: Adjust msglen in better fashion...
			if(thp->args.ms.srcmsglen < repp->msglen) {
				repp->msglen = thp->args.ms.srcmsglen;
			}
		}
		
		lock_kernel();
		SETKSTATUS(thp, (act->tid << 16) | cop->scoid);
				
		// Unlink receive thread from the receive queue.
		LINK2_REM(thp);

		act->args.ms.server = thp;
		thp->client = act;

		// Check fast path conditions - no timeouts, no QNET, no sporadic.
		fast_path = act->timeout_flags | 
			thp->timeout_flags | 
			(cop->flags & COF_NETCON) |
			(chp->flags & _NTO_CHF_FIXED_PRIORITY) |
			(IS_SCHED_SS(act) ? 1 : 0);

		// We can inline the block_and_ready()
		if(fast_path == 0) {

			// By default the receiver runs with message driven priority.
			thp->real_priority = thp->priority = act->priority;
			thp->dpp = act->dpp;
			AP_INHERIT_CRIT(thp, act);
	
			act->state = STATE_REPLY;	// Must be set before calling block_and_ready()
			_TRACE_TH_EMIT_STATE(act, REPLY);
#if !defined(VARIANT_smp)
			// This is an inline version of block an ready
			// We can use this for non-SMP (no runmask).
			// This also works for AP as we inherit the partition
			thp->next = NULL;
			thp->prev = NULL;
	
			//thp->restart = NULL;
			actives[KERNCPU] = thp;
			thp->state = STATE_RUNNING;
			//thp->runcpu = KERNCPU;
			_TRACE_TH_EMIT_STATE(thp, RUNNING);
#else
			block_and_ready(thp);
#endif
		} else {
			if((chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
				// By default the receiver runs with message driven priority.
				thp->real_priority = thp->priority = act->priority;
				thp->dpp = act->dpp;
				AP_INHERIT_CRIT(thp, act);
			}
			act->state = STATE_REPLY;	// Must be set before calling block_and_ready()
			_TRACE_TH_EMIT_STATE(act, REPLY);

			if(cop->flags & COF_NETCON) {
				SETKSTATUS(act2, 1);
				if((act->flags & _NTO_TF_BUFF_MSG) == 0) {
					// #### Note: use net_srcmsglen saved above before we switch aspace
					act->args.ms.srcmsglen = net_srcmsglen;
				}

				SETKSTATUS(thp, (act->args.ms.rparts << 16) | cop->scoid);
				ready(thp);
			} else {
				block_and_ready(thp);
			}
			
			if(thp->timeout_flags & _NTO_TIMEOUT_REPLY) {
				// arm the timeout for reply block
				timeout_start(thp);
			}
		}
		
		// Block the active thread and ready the receiver thread
		act->blocked_on = cop;

		// Link the now reply blocked sending thread in the reply queue
		LINK2_BEG(chp->reply_queue, act, THREAD);
		++cop->links;

		return ENOERROR;
	}

	// No-one waiting for a msg
	// If a normal thread
	//     Block the active thread
	//     Link the now send blocked thread into the reply queue
	// If a network thread send
	//     Link the passed vthread into the reply queue
	// Boost the servers priority to the clients if needed.
	if(th_flags == 0) {
		act->args.ms.smsg = kap->smsg;
		act->args.ms.sparts = kap->sparts;
			// FUTURE: Make copy of send IOVs
	} else {
		act->flags |= _NTO_TF_BUFF_MSG;
	}
	
	
	if(IMTO(act, STATE_SEND)) {
		act->flags &= ~_NTO_TF_BUFF_MSG;
		return ETIMEDOUT;
	}

	lock_kernel();

	// Incoming network Send.
	// We use vtid passed in kap->rparts and _vtid_info passed in kap->rmsg
	if(cop->flags & COF_NETCON) {
		if(act->flags & _NTO_TF_BUFF_MSG) {
			SETKSTATUS(act2, 1);
		} else {
			// Return zero telling the network manager we still need the send data.
			// A _PULSE_CODE_NET_ACK will be sent later when the receive completes.
			act->args.ms.srcmsglen = net_srcmsglen;
			SETKSTATUS(act2, 0);
		}
		act->state = STATE_SEND;
		_TRACE_TH_EMIT_STATE(act, SEND);
	} else {
		//
		// Don't allow any MsgSend's to processes that are dying.
		// Only have to check here because of code in nano_signal.c
		// - check the comment where we turn on the _NTO_PF_COREDUMP
		// flag.
		//
		if(chp->process->flags & (_NTO_PF_TERMING | _NTO_PF_ZOMBIE | _NTO_PF_COREDUMP)) {
			return ENXIO;
		}
		// Can't use block(), because 'act' might not actually be the
		// actives[KERNCPU] anymore...
		unready(act, STATE_SEND);
	}

	act->blocked_on = cop;
	LINK2_PRI(chp->send_queue, act, THREAD);
	++cop->links;

	// To prevent priority inversion, boost all threads in the server
	//
	// for non-APS scheduling: raise prio of thread who last used this channel to at least that of the sender
	// 
	// for APS scheduling: also cause the out-of-budget threads to inherit the budget of the sender,
	// but do not inherit the critical state.
	if((chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
		int i;

		for(i = 0 ; i < chp->process->threads.nentries ; ++i) {
			if(VECP(thp, &chp->process->threads, i) &&  thp->last_chid == chp->chid) {
				short may_run = may_thread_run(thp); 
				if ( thp->priority < act->priority ) { 		
					adjust_priority(thp, act->priority, may_run ? thp->dpp : act->dpp );
					thp->real_priority = thp->priority;
				} else { 
					if (!may_run) { 
						// server threads are higher prio, but have no budget. So inherit budget only
						adjust_priority(thp, thp->priority, act->dpp); 
					}
				}
			}
		}
	}

	return ENOERROR;
	
send_fault:
	act->flags &= ~_NTO_TF_BUFF_MSG;
	
	return EFAULT;

rcv_fault:
	act->flags &= ~_NTO_TF_BUFF_MSG;
	kererr(thp, EFAULT);
	LINK2_REM(thp);
	ready(thp);

	/* Restart the kernel call - same behavior as receive path */
	// @@@ Should this be act2?
	KERCALL_RESTART(act);
	
	return ENOERROR;
}

int kdecl
ker_msg_sendpulse(THREAD *act, struct kerargs_msg_sendpulse *kap) {
	CONNECT		*cop;
	CHANNEL		*chp;
	int			 prio, status;
	CREDENTIAL	*src, *dst;
	THREAD		*thp;

	if((cop = lookup_connect(kap->coid)) == NULL  ||  cop->type != TYPE_CONNECTION  ||  (chp = cop->channel) == NULL) {
		return EBADF;
	}

	prio = (kap->priority == -1) ? act->priority : kap->priority & _PULSE_PRIO_PUBLIC_MASK;

	// pulse over QNET
	if(chp == net.chp) {
		_TRACE_COMM_EMIT_SPULSE(act, cop, cop->scoid, prio);
		return net_send_pulse(act, cop, kap->coid, prio, kap->code, kap->value);
	}

	// Revert back to the pre-beta3 practise of allowing pulses across
	// processes based upon permissions
	dst = chp->process->cred;
	if(cop->flags & COF_NETCON) {
		src = cop->un.net.cred;
	} else {
		src = act->process->cred;
	}

	if(!(src->info.euid == 0  ||
	   src->info.ruid == dst->info.ruid  ||
	   src->info.ruid == dst->info.suid  ||
	   src->info.euid == dst->info.ruid  ||
	   src->info.euid == dst->info.suid)) {
		return EPERM;
		}

	/* 
	 * If we have a receive thread on the channel, and there is no other pending
	 * pulse (think pulse prio inversion), we can take the shortcut of 
	 * delivering directly.
	 */
	_TRACE_COMM_EMIT_SPULSE(act, cop, cop->scoid|_NTO_SIDE_CHANNEL, prio);
	
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
	for(thp = chp->receive_queue; thp && (thp->internal_flags & _NTO_ITF_MSG_DELIVERY); thp = thp->next) {
	// nothing to do
	}
	if((thp != NULL) && (thp->aspace_prp == aspaces_prp[KERNCPU]) && (chp->send_queue == NULL)) {
#else
	if(((thp = chp->receive_queue) != NULL) && (thp->aspace_prp == aspaces_prp[KERNCPU]) && (chp->send_queue == NULL)) {
#endif
		/* Receive thread waiting with right aspace -- short-circuit pulse delivery */

		thp->internal_flags &= ~_NTO_ITF_RCVPULSE;
		thp->restart = NULL;

		// Yes. Transfer the data.
		status = xferpulse(thp, thp->args.ri.rmsg, thp->args.ri.rparts, kap->code, kap->value, cop->scoid | _NTO_SIDE_CHANNEL);
		
		/* 
		 * If the receiver asked for a MsgInfo, then stuff it right now 
		 * since we're in the right aspace from the test above
		 */
		if(thp->args.ri.info)  {
			// We can use a fast inline version as we know the thread does not 
			// have an unblock pending
		//	STUFF_RCVINFO(act, cop, thp->args.ri.info);
			get_rcvinfo(act, -1, cop, thp->args.ri.info);
		}

		lock_kernel();

		SETKSTATUS(thp, 0);

		if(status) {
			kererr(thp, EFAULT);
		}

		// Unlink receive thread from the receive queue and ready it.
		LINK2_REM(thp);

		// By default the receiver runs with message driven priority.
		if(thp->priority != prio  &&  (chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
			adjust_priority(thp, prio, thp->process->default_dpp);
			thp->real_priority = thp->priority;
		} else if(thp->dpp != thp->process->default_dpp) {
			adjust_priority(thp, thp->priority, thp->process->default_dpp);
		}

		/* 
		 * Note - since this is a user-delivered pulse, it cannot have 
		 * the VTID flag on 
		 * @@Add assert
		 */
		ready(thp);

		return(status ? ESRVRFAULT : EOK);
		
	} else {
		if((status = pulse_deliver(chp, prio, kap->code, kap->value, cop->scoid | _NTO_SIDE_CHANNEL, 0))) {
			return status;
		}
	}

	return EOK;
}

int kdecl
ker_msg_receivev(THREAD *act, struct kerargs_msg_receivev *kap) {
	CHANNEL		*chp;
	CONNECT		*cop;
	THREAD		*thp;
	int			 tid, chid;
	unsigned	 tls_flags;
	VECTOR		*chvec;

	chid = act->last_chid = kap->chid;		// Used for priority boost
	chvec = &act->process->chancons;
	
	if(chid & _NTO_GLOBAL_CHANNEL) {
		chid &= ~_NTO_GLOBAL_CHANNEL;
		chvec = &chgbl_vector;
	}	
	if((chp = vector_lookup(chvec, chid)) == NULL  ||
	   chp->type != TYPE_CHANNEL) {
	   	lock_kernel();
		return ESRCH;
	}

	if(kap->info) {
		WR_VERIFY_PTR(act, kap->info, sizeof(*kap->info));
		// NOTE:
		// Make sure the receive info pointer is valid. Note that we need some
		// extra checks in the mainline when filling in the rcvinfo (this is no
		// longer done in specret).
		//
		// Note: we don't probe the whole buffer, rather touch start and end, 
		// which is faster and sufficient
		//
		WR_PROBE_INT(act, kap->info, 1);
		WR_PROBE_INT(act, &kap->info->reserved, 1);
	}

	if(chp->flags & (_NTO_CHF_ASYNC | _NTO_CHF_GLOBAL)) {
		if(chp->flags & _NTO_CHF_GLOBAL) {
			cop = NULL;
			if(kap->coid) {
				if((cop = lookup_connect(kap->coid)) == NULL  ||  cop->type != TYPE_CONNECTION) {
					return EBADF;
				}
			}
		
			return msgreceive_gbl(act, (CHANNELGBL*) chp, kap->rmsg, -kap->rparts, kap->info, cop, kap->coid);
		} else {
			return msgreceive_async(act, (CHANNELASYNC*) chp, kap->rmsg, kap->rparts);
		}
	}
	
	/*
	 * Validate incoming IOVs and calculate receive length
	 */
 	if(kap->rparts >= 0) {
		int len = 0;
		IOV *iov = kap->rmsg;
		int rparts = kap->rparts;
		
		// Calculate receive length -- even if not requested, we use it for msginfo
		// Do boundary check
		while(rparts) {
			uintptr_t base, last;
			
			len += GETIOVLEN(iov);
			base = (uintptr_t)GETIOVBASE(iov);
			last = base + GETIOVLEN(iov) - 1;
			if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (GETIOVLEN(iov) != 0)) {
				return EFAULT;
			}
			++iov;
			--rparts;
		}
		act->args.ms.srcmsglen = len;
	} else {
		// Single part -- validate receive address
		uintptr_t base, last;
		base = (uintptr_t) kap->rmsg;
		last = base + (-kap->rparts) - 1;
		if((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) {
			// We know length is non-zero from test above
			return EFAULT;
		}
		act->args.ms.srcmsglen = -kap->rparts;
	}
		

restart:
	// Was there was a waiting thread or pulse on the channel?
	thp = chp->send_queue;
restart2:
	if(thp) {
		int xferstat;

		// Yes. There is a waiting message.
		if(thp->type == TYPE_PULSE  ||  thp->type == TYPE_VPULSE) {
			PULSE *pup = (PULSE *)(void *)thp;

			act->restart = NULL;
			xferstat = xferpulse(act, kap->rmsg, kap->rparts, pup->code, pup->value, pup->id);

			if(thp->type == TYPE_VPULSE) {
				thp = (THREAD *)pup->id;
				get_rcvinfo(thp, -1, thp->blocked_on, kap->info);
			}

			lock_kernel();
			act->timeout_flags = 0;

			// By default the receiver runs with message driven priority.
			// RUSH: Fix for partition inheritance
			if(act->priority != pup->priority  &&  (chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
				adjust_priority(act, pup->priority, act->process->default_dpp);
				act->real_priority = act->priority;
			} else if(act->dpp != act->process->default_dpp) {
				adjust_priority(act, act->priority, act->process->default_dpp);
			}

			if(pup->count == _PULSE_COUNT_HEAD || --pup->count == 0) {
				LINK2_REM(pup);
				object_free(chp->process, &pulse_souls, pup);
			}

			if((thp = act->client) != 0) {
				/* need to clear client's server field */
				act->client = 0;
				thp->args.ms.server = 0;
			}
	
			if(xferstat) {
				return EFAULT;
			}
			_TRACE_COMM_IPC_RET(act);

			return EOK;
		}

		// If the receive request was for a pulse only, keep checking the list..
		if(KTYPE(act) == __KER_MSG_RECEIVEPULSEV) {
			thp = thp->next;
			goto restart2;
		}

#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
		// If thp is in the xfer status in another CPU, try next one
		if(thp->internal_flags & _NTO_ITF_MSG_DELIVERY) {
			thp = thp->next;
			goto restart2;
		}
#endif

		// If an immediate timeout was specified we unblock the sender.
		if(IMTO(thp, STATE_REPLY)) {
			lock_kernel();
			force_ready(thp, ETIMEDOUT);
			unlock_kernel();
			KER_PREEMPT(act, ENOERROR);
			goto restart;
		}

		if(thp->flags & _NTO_TF_BUFF_MSG) {
			xferstat = xfer_cpy_diov(act, kap->rmsg, thp->args.msbuff.buff, kap->rparts, thp->args.msbuff.msglen);
		} else {
			act->args.ri.rmsg = kap->rmsg;
			act->args.ri.rparts = kap->rparts;

			START_SMP_XFER(act, thp);
			
			xferstat = xfermsg(act, thp, 0, 0);

			lock_kernel();
			END_SMP_XFER(act, thp);

#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_FORCE_RDY) {
				force_ready(thp,KSTATUS(thp));
				thp->internal_flags &= ~_NTO_ITF_MSG_FORCE_RDY;
				KERCALL_RESTART(act);
				act->restart = 0;
				return ENOERROR;
			}
			if(act->flags & (_NTO_TF_SIG_ACTIVE | _NTO_TF_CANCELSELF)) {
				KERCALL_RESTART(act);
				act->restart = 0;
				return ENOERROR;
			}
#endif
		}

		if(xferstat) {
			lock_kernel();

			// Only a send fault will unblock the sender.
			if(xferstat & XFER_SRC_FAULT) {
				// Let sender know it faulted and restart receive.
				force_ready(thp, EFAULT);
				unlock_kernel();
				KER_PREEMPT(act, ENOERROR);
				goto restart;
			}

			if((thp = act->client) != 0) {
				/* need to clear client's server field */
				act->client = 0;
				thp->args.ms.server = 0;
			}

			// Let receiver and sender know reason for fault.
			act->timeout_flags = 0;
			return EFAULT;
		}

		if(thp->type == TYPE_VTHREAD) {
			tid = thp->args.ri.rparts;
		} else {
			tid = thp->tid;
		}
		cop = thp->blocked_on;
		//lint --e(650) Constant out of range
		if(thp->args.ms.srcmsglen == -1) {
			// This should never occur with the new code
			crash();
			thp->args.ms.srcmsglen = thp->args.ms.msglen;
		}

		// If the receive specified an info buffer stuff it as well.
		// thp->args.ms.msglen was set by xfermsg
		if(kap->info) {
		//	get_rcvinfo(thp, -1, cop, kap->info);
			STUFF_RCVINFO(thp, cop, kap->info);
			if(thp->flags & _NTO_TF_BUFF_MSG) {
				if(kap->info->msglen > act->args.ms.srcmsglen) kap->info->msglen = act->args.ms.srcmsglen;
			}
		}

		lock_kernel();
		_TRACE_COMM_IPC_RET(act);
		act->timeout_flags = 0;
		act->restart = NULL;

		// Because _NTO_TF_RCVINFO and _NTO_TF_SHORT_MSG will not be set, set this to NULL
		thp->restart = NULL;

		if(act->client != 0) {
			/* need to clear client's server field */
			act->client->args.ms.server = 0;
		}
		thp->args.ms.server = act;
		act->client = thp;

		LINK2_REM(thp);
		if(thp->state == STATE_SEND) {
			thp->state = STATE_REPLY;
			_TRACE_TH_EMIT_STATE(thp, REPLY);
			SETKSTATUS(act, (tid << 16) | cop->scoid);
		} else {
			thp->state = STATE_NET_REPLY;
			_TRACE_TH_EMIT_STATE(thp, NET_REPLY);
			SETKSTATUS(act, -((tid << 16) | cop->scoid));
		}
		LINK2_BEG(chp->reply_queue, thp, THREAD);

		// By default the receiver runs with message driven priority.
		// RUSH: Fix for partition inheritance
		if((act->priority != thp->priority || act->dpp != thp->dpp) &&  (chp->flags & _NTO_CHF_FIXED_PRIORITY) == 0) {
			AP_INHERIT_CRIT(act, thp);
			adjust_priority(act, thp->priority, thp->dpp);
			if(act->real_priority != act->priority) act->real_priority = act->priority;
		} else {
			AP_CLEAR_CRIT(act);
		}

		return ENOERROR;
	}

	// No-one waiting for a msg so block
	tls_flags = act->un.lcl.tls->__flags;
	lock_kernel();
	_TRACE_COMM_IPC_RET(act);

	if((thp = act->client) != 0) {
		/* need to clear client's server field */
		act->client = 0;
		thp->args.ms.server = 0;
	}
	
	if(IMTO(act, STATE_RECEIVE)) {
		return ETIMEDOUT;
	}

	if(PENDCAN(tls_flags)) {
		SETKIP_FUNC(act, act->process->canstub);
		return ENOERROR;
	}

	// Can't call block() here, because act may not be actives[KERNCPU]
	// anymore - if the sender faulted, we call force_ready() above and
	// that might change actives[KERNCPU]
	unready(act, STATE_RECEIVE);

	// End inheritance of partition and critical state. This must be after block() so that we microbill
	// the partition we where running in before we reset to the original partition. PR26990
	act->dpp = act->orig_dpp; 				
	AP_CLEAR_CRIT(act); 

	act->blocked_on = chp;
	act->args.ri.rmsg = kap->rmsg;
	act->args.ri.rparts = kap->rparts;
	act->args.ri.info = kap->info;

	// Add to the receive queue, put pulse only receives at the end of
	// the list so the ker_msg_send() only has to check the head of the list
	if(KTYPE(act) == __KER_MSG_RECEIVEPULSEV) {
		act->internal_flags |= _NTO_ITF_RCVPULSE;
		LINK2_END(chp->receive_queue, act, THREAD);
	} else {
		LINK2_BEG(chp->receive_queue, act, THREAD);
	}
	return ENOERROR;
}


int kdecl
ker_msg_replyv(THREAD *act, struct kerargs_msg_replyv *kap) {
	CONNECT			*cop;
	THREAD			*thp;
	int				 xferstat, status;
	unsigned		 flags = 0;

	// Future: Think about an inline version of this for speed
	if((cop = lookup_rcvid((KERARGS *)(void *)kap, kap->rcvid, &thp)) == NULL) {
		return ENOERROR;
	}

	// Make sure thread is replied blocked on a channel owned by this process.
	if(thp->state != STATE_REPLY && thp->state != STATE_NET_REPLY) {
		return ESRCH;
	}
	
	// Verify that the message has been fully received, and that the receiving
	// thread has completed any specialret() processing that needs to be done.
	if ( thp->internal_flags & _NTO_ITF_SPECRET_PENDING ) {
		return ESRCH;
	}
	
	if(thp->blocked_on != cop) {
		CONNECT *cop1 = cop;
		cop = thp->blocked_on;
		if((cop->flags & COF_VCONNECT) == 0 || cop->un.lcl.cop != cop1) {
			return ESRCH;
		}
	}
	
	if(thp->internal_flags & _NTO_ITF_UNBLOCK_QUEUED) {
		remove_unblock(thp, cop, kap->rcvid);
		// no need to keep kernel locked after the unblock pulse is removed
		if(get_inkernel() & INKERNEL_LOCK) {
			unlock_kernel();
			KER_PREEMPT(act, ENOERROR);
		}
	}

	thp->flags &= ~(_NTO_TF_BUFF_MSG | _NTO_TF_SHORT_MSG);
	
	if(kap->sparts != 0) {
		/* Reply IOVs */
		if(kap->sparts < 0) {
			// Single part -- do the boundary check and copy if short message
			uintptr_t base, last;
			int	len;
			
			base = (uintptr_t) kap->smsg;
			len = -kap->sparts;
			last = base + len - 1;
			if((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) {
				// We know length is non-zero from test above
				xferstat = XFER_SRC_FAULT;
				goto xfer_err;
			}
			if(len <= sizeof(thp->args.msbuff.buff)) {
				 if((xferstat = xfer_memcpy(thp->args.msbuff.buff, (char *)base, thp->args.msbuff.msglen = len))) {
				 	goto xfer_err;
				 }
				flags = _NTO_TF_BUFF_MSG;
			}
		} else if(kap->sparts == 1) {
			// Single IOV -- do the boundary check and copy if short message
			uintptr_t base, last, len;
			
			base = (uintptr_t)GETIOVBASE(kap->smsg);
			len = GETIOVLEN(kap->smsg);
			last = base + len - 1;
			if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (len != 0)) {
				xferstat = XFER_SRC_FAULT;
				goto xfer_err;
			}
			if(len <= sizeof(thp->args.msbuff.buff)) {
				if((xferstat = xfer_memcpy(thp->args.msbuff.buff, (char *)base, thp->args.ms.msglen = len))) {
					goto xfer_err;
				}
				flags = _NTO_TF_BUFF_MSG;
			}
		} else {
			// Multi IOV case
			int len = 0;
			IOV *iov = kap->smsg;
			int sparts = kap->sparts;
			
			// Calculate reply length -- even if not requested, it is almost free
			// Also do boundary check
			while(sparts) {
				uintptr_t base, last;
				
				len += GETIOVLEN(iov);
				base = (uintptr_t)GETIOVBASE(iov);
				last = base + GETIOVLEN(iov) - 1;
				if(((base > last) || !WITHIN_BOUNDRY(base, last, act->process->boundry_addr)) && (GETIOVLEN(iov) != 0)) {
					xferstat = XFER_SRC_FAULT;
					goto xfer_err;
				}
				++iov;
				--sparts;
				// Keep copy of IOV -- NYI, only really need if no receiver
				//if(niov < _NUM_CACHED_SEND_IOV) {
				//	act->args.ms.siov[niov] = *iov;
				//}
			}
			if(len <= sizeof(thp->args.msbuff.buff)) {
				int pos = 0;
				iov = kap->smsg;
				sparts = kap->sparts;
				// Multi-IOV incoming message that is short
				// Optimization -- need memcpy_siov for efficiency
				while(sparts) {
					if((xferstat = xfer_memcpy(&thp->args.msbuff.buff[pos], GETIOVBASE(iov), GETIOVLEN(iov)))) {
						goto xfer_err;
					}
					
					pos += GETIOVLEN(iov);
					iov++;
					sparts--;
				}
				thp->args.ms.msglen = len;
				flags = _NTO_TF_BUFF_MSG;
			}
		}
	
		/*
		 * Up-front work is now done. There are a few things set:
		 *  - incoming reply IOVs are verified for boundary
		 *  - if short message, it has been copied into thp's short buffer
		 *  - flags is either zero (long message) or _NTO_TF_BUFF_MSG if short
		 */
		 
		// This is a long message
		if(flags == 0) {
			act->args.ms.smsg = kap->smsg;
			act->args.ms.sparts = kap->sparts;
			START_SMP_XFER(act, thp);
			// Yes. Transfer the data.
			xferstat = xfermsg(thp, act, 0, 0);
			lock_kernel();
			END_SMP_XFER(act, thp);
			
#if defined(VARIANT_smp) && defined(SMP_MSGOPT)
			if(thp->internal_flags & _NTO_ITF_MSG_FORCE_RDY) {
				_TRACE_COMM_EMIT_REPLY(thp, cop, thp->tid+1);
				/* it could be channel destroy */
				force_ready(thp,KSTATUS(thp));
				thp->internal_flags &= ~_NTO_ITF_MSG_FORCE_RDY;
				KERCALL_RESTART(act);
				act->restart = 0;
				return ENOERROR;
			}
#endif
		} else {
	
			// Short message. We only do something if there is no target aspace --
			// reply to proc case. Else, this is handled in specret.
			if(thp->aspace_prp == NULL) {
				xferstat = xfer_cpy_diov(thp, thp->args.ri.rmsg, thp->args.msbuff.buff, thp->args.ri.rparts, thp->args.msbuff.msglen);
				lock_kernel();
			} else {
				xferstat = 0;
				lock_kernel();
				thp->blocked_on = thp;
				thp->flags |= (_NTO_TF_BUFF_MSG | _NTO_TF_SHORT_MSG);
			}
		}
		_TRACE_COMM_EMIT_REPLY(thp, cop, thp->tid+1);
	} else { // Zero-length reply -- common case
		xferstat = 0;
		lock_kernel();
		_TRACE_COMM_EMIT_REPLY(thp, cop, thp->tid+1);
	}



	thp->flags &= ~_NTO_TF_UNBLOCK_REQ;

	if(thp->args.ms.server != 0) {
		thp->args.ms.server->client = 0;
		thp->args.ms.server = 0;
	}

	if(xferstat) goto xfer_err;

	thp->restart = act->restart = NULL;

	LINK2_REM(thp);
	if(--cop->links == 0) {
		connect_detach(cop, thp->priority);
	}

	ready(thp);
	SETKSTATUS(thp, kap->status);

	return EOK;

xfer_err:

	// Fault -- determine if this is sender or replyer.
	// If replier faulted let him know and abort the operation
	// and wakup the sender with an error.
	status = (xferstat & XFER_SRC_FAULT) ? ESRVRFAULT : EFAULT;
	kererr(thp, status);
	LINK2_REM(thp);
	thp->flags &= ~(_NTO_TF_BUFF_MSG | _NTO_TF_SHORT_MSG);

	if(--cop->links == 0) {
		connect_detach(cop, thp->priority);
	}
	ready(thp);
	
	return status;
}

int kdecl
ker_msg_error(THREAD *act, struct kerargs_msg_error *kap) {
	CONNECT				*cop;
	THREAD				*thp;

	if((cop = lookup_rcvid((KERARGS *)(void *)kap, kap->rcvid, &thp)) == NULL) {
		return ENOERROR;
	}

	// Make sure thread is replied blocked on a channel owned by this process.
	if(thp->state != STATE_REPLY && thp->state != STATE_NET_REPLY) {
			return ESRCH;
	}
	
	// Verify that the message has been fully received, and that the receiving
	// thread has completed any specialret() processing that needs to be done.
	if ( thp->internal_flags & _NTO_ITF_SPECRET_PENDING ) {
		return ESRCH;
	}
	
	if(thp->blocked_on != cop) {
		CONNECT *cop1 = cop;
		cop = thp->blocked_on;
		if((cop->flags & COF_VCONNECT) == 0 || cop->un.lcl.cop != cop1) {
			return ESRCH;
		}
	}
	
	if(thp->internal_flags & _NTO_ITF_UNBLOCK_QUEUED) {
		remove_unblock(thp, cop, kap->rcvid);
	}

	lock_kernel();

	if(thp->args.ms.server != 0) {
		thp->args.ms.server->client = 0;
		thp->args.ms.server = 0;
	}

	thp->flags &= ~(_NTO_TF_BUFF_MSG | _NTO_TF_UNBLOCK_REQ);
	if(kap->err == ERESTART) {
		SETKIP(thp, KIP(thp) - KER_ENTRY_SIZE);
	} else {
		kererr(thp, kap->err);
	}
	_TRACE_COMM_EMIT_ERROR(thp, cop, thp->tid+1);

	LINK2_REM(thp);
	if(--cop->links == 0) {
		connect_detach(cop, thp->priority);
	}
	ready(thp);

	act->restart = NULL;
	return EOK;
}

__SRCVERSION("ker_fastmsg.c $Rev: 157350 $");
