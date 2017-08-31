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

/*==============================================================================
 * 
 * mempartmgr_event
 * 
 * When installed, this routine is called into from the memory partition
 * module to deliver events to any registered process.
 * This routine will be called with the partition that the event is associated
 * with <mp>, the event type (evtype> and (depending on the event type) optional
 * event specific data.
 * 
 * Event processing is kept out of the memory partitioning module and is
 * implemented entirely within the resource manager.
 * 
 * Returns: nothing
*/

#include "mempartmgr.h"

//#define DISPLAY_EVENT_MSG

#if !defined(NDEBUG) || defined(__WATCOMC__)
#define INLINE
#else	/* NDEBUG */
#undef DISPLAY_EVENT_MSG
#define INLINE	__inline__
#endif	/* NDEBUG */

#if !defined(NDEBUG) && defined(DISPLAY_EVENT_MSG)
static char *mclass_evt_string(memclass_evttype_t evt);
#endif	/* NDEBUG */

/*
 * =============================================================================
 * 
 * 						S U P P O R T   R O U T I N E S
 * 
 * 				These routines are first to allow them to be inlined
 * 
 * =============================================================================
*/
#define SEND_MC_DELTA_INCR_EVENTS(el, c_sz, p_sz) send_mc_delta_incr_events((el), (c_sz), (p_sz))
#define SEND_MC_DELTA_DECR_EVENTS(el, c_sz, p_sz) send_mc_delta_decr_events((el), (c_sz), (p_sz))
#define SEND_MP_DELTA_INCR_EVENTS(el, c_sz, p_sz) send_mp_delta_incr_events((el), (c_sz), (p_sz))
#define SEND_MP_DELTA_DECR_EVENTS(el, c_sz, p_sz) send_mp_delta_decr_events((el), (c_sz), (p_sz))

/*
 * deliver_event
 * 
 * Actually deliver the event.
 * Make a syscall if from procnto, else make the sigevent_exe() call directly
 * 
 * IMPORTANT:
 * There is an issue with lookup_rcvid() whereby occasionally it will return
 * NULL on the 'rcvid'. The explanantion for this is still not understood but
 * an example of the behaviour is as follows.
 * If 2 separate processes registered for DECR events on a partition and then
 * that partition was filled and subsequently flushed (processes killed), every
 * 3rd event to the 'rcvid' for each listening process would result in a failed
 * lookup_rcvid(). The very next event deliver would succeed, as would the next
 * followed by another failure. This pattern repeats for as many events as would
 * be delivered.
 * The solution I crafted for this (and which seems to work but is as yet not
 * vetted by the rest of the kernel team) is to save the PROCESS * of the
 * registering process along with the 'rcvid' (in a new structure). When the
 * event is delivered (and we are in the kernel), the lookup_rcvid() is attempted
 * using the rcvid of the registering process (as before) and if that fails then
 * the PROCESS * is dereferenced to get the 'valid_thp' field and that is used
 * for the sigevent_exe() call.
 * 
*/
#ifdef NDEBUG
static INLINE void deliver_event(struct evtdest_s *evtdest, struct sigevent *se)
{
	if (!KerextAmInKernel())
		MsgDeliverEvent(evtdest->rcvid, se);
	else
	{
		THREAD  *thp;
		
		if (lookup_rcvid(NULL, evtdest->rcvid, &thp) == NULL)
			if ((thp = evtdest->prp->valid_thp) != NULL)
				sigevent_exe(se, thp, 1);
	}
}
#else	/* NDEBUG */
static unsigned evt_delivery_fail_cnt = 0;
static INLINE void deliver_event(struct evtdest_s *evtdest, struct sigevent *se)
{
	if (!KerextAmInKernel())
	{
		int r = MsgDeliverEvent(evtdest->rcvid, se);
		if (r == -1)
			if ((++evt_delivery_fail_cnt % 100) == 0)
				kprintf("%u event delivery failures @ %d, err = %d\n",
						evt_delivery_fail_cnt, __LINE__, errno);
	}
	else
	{
		int r = ESRCH;
		THREAD  *thp;
		if (lookup_rcvid(NULL, evtdest->rcvid, &thp) == NULL)
			thp = evtdest->prp->valid_thp;

		if ((thp == NULL) || ((r = sigevent_exe(se, thp, 1)) != EOK))
			if ((++evt_delivery_fail_cnt % 100) == 0)
				kprintf("%u event delivery failures @ %d, err = %d\n",
						evt_delivery_fail_cnt, __LINE__, r);	
	}
}
#endif	/* NDEBUG */

#ifndef NDEBUG
#define MATCH_FUNC_ASSERT(v1, v2) \
		do { \
			if (v1 == NULL) crash(); \
			if (v2 == NULL) crash(); \
		} while(0)
#else	/* NDEBUG */
#define MATCH_FUNC_ASSERT(v1, v2)
#endif	/* NDEBUG */

#define MATCH_FUNC(val1, val2, type)	(*((type *)val1) == *((type *)val2))

/*
 * pid_match
 * 
 * compare 2 values as process identifier (pid_t) types and return whether
 * they are equivalent
*/
static INLINE bool pid_match(void **match, void **val)
{
	pid_t null_pid = 0;
	MATCH_FUNC_ASSERT(match, val);
	/* if the caller provided a pid=0, this means match any process id */
	return (MATCH_FUNC(match, &null_pid, pid_t) || MATCH_FUNC(match, val, pid_t));
}

/*
 * mpid_match
 * 
 * compare 2 values as memory partition identifier (mempart_id_t) types and
 * return whether they are equivalent
*/
static INLINE bool mpid_match(void **match, void **val)
{
	mempart_id_t null_mpid = 0;
	MATCH_FUNC_ASSERT(match, val);
	/* if the caller provided an mpid=0, this means match any partition id */
	return (MATCH_FUNC(match, &null_mpid, mempart_id_t) || MATCH_FUNC(match, val, mempart_id_t));
}

/*
 * oid_match
 * 
 * compare 2 values as object identifier (void *) types and return whether they
 * are equivalent
*/
static INLINE bool oid_match(void **match, void **val)
{
	void *null_oid = 0;
	MATCH_FUNC_ASSERT(match, val);
	/* if the caller provided an oid=0, this menas match any object id */
	return (MATCH_FUNC(match, &null_oid, void *) || MATCH_FUNC(match, val, void *));
}

/*
 * send_threshold_X_over_events
 * 
 * Send an event to all listeners in list <evlist> for which
 * <ev>->_type.threshold_cross is > the registered 'over' value
 * 
 * Note that in order to meet the conditions for a threshold crossing, the
 * registered event crossing 'over' value must be greater than <prev_size> and
 * less than <ev>->_type.threshold_cross (ie. it must be between the
 * previous and the current values). If the previous current value was already
 * over the registered 'over' value, the threshold is not crossed and no event
 * will be sent.
 * 
 * Once the event threshold crossing condition is met and the event notification
 * sent, the event is removed from the event list. This ensures that a receiver
 * is not flooded with events for rapidly changing partition current size value
 * changes.
*/
static INLINE void
send_mp_threshold_X_over_events(mempart_evtlist_t *evlist, memsize_t cur_size,
							 memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if ((reg_event->evt_dest.rcvid > 0) &&
			(reg_event->evt_reg.mp.info.val.threshold_cross > prev_size) &&
			(reg_event->evt_reg.mp.info.val.threshold_cross <= cur_size))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mp.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mp.sig);
			if (!(reg_event->evt_reg.mp.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}
static INLINE void
send_mc_threshold_X_over_events(mempart_evtlist_t *evlist, memsize_t cur_size,
							 memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if ((reg_event->evt_dest.rcvid > 0) &&
			(reg_event->evt_reg.mc.info.val.threshold_cross > prev_size) &&
			(reg_event->evt_reg.mc.info.val.threshold_cross <= cur_size))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mc.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mc.sig);
			if (!(reg_event->evt_reg.mc.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mp_threshold_X_under_events
 * 
 * Send an event to all listeners in list <evlist> for which
 * <ev>->_type.threshold_cross is < the registered 'under' value.
 *
 * Note that in order to meet the conditions for a threshold crossing, the
 * registered event crossing 'under' value must be less than <prev_size> and
 * greater than or equal to <ev>->_type.threshold_cross (ie it must be
 * between the previous and the current values). If the previous current value
 * was already under the registered 'under' value, the threshold is not crossed
 * and no event will be sent.
 * 
 * Once the event threshold crossing condition is met and the event notification
 * sent, the event is removed from the event list. This ensures that a receiver
 * is not flooded with events for rapidly changing partition cuurent size value
 * changes. 
*/
static INLINE void
send_mp_threshold_X_under_events(mempart_evtlist_t *evlist, memsize_t cur_size,
							  memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if ((reg_event->evt_dest.rcvid > 0) &&
			(reg_event->evt_reg.mp.info.val.threshold_cross < prev_size) &&
			(reg_event->evt_reg.mp.info.val.threshold_cross >= cur_size))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mp.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mp.sig);
			if (!(reg_event->evt_reg.mp.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

static INLINE void
send_mc_threshold_X_under_events(mempart_evtlist_t *evlist, memsize_t cur_size,
							  memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if ((reg_event->evt_dest.rcvid > 0) &&
			(reg_event->evt_reg.mc.info.val.threshold_cross < prev_size) &&
			(reg_event->evt_reg.mc.info.val.threshold_cross >= cur_size))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mc.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mc.sig);
			if (!(reg_event->evt_reg.mc.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mc_delta_incr_events
 * 
 * Send an event to all listeners in list <evlist> for which the delta between
 * the current size value and the size at the time the last event was sent
 * is >= the registered 'delta' value on increment
*/
static INLINE void
send_mc_delta_incr_events(mempart_evtlist_t *evlist, memsize_t cur_size, memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		/*
		 * memsize_t_INFINITY is a one time only initialization for event
		 * registration. If however 'prev_size' is less than the currently
		 * recorded size since the last event, we know we have decremented and
		 * we must reset the start point
		*/
		if ((reg_event->evt_data.size == memsize_t_INFINITY) ||
			(prev_size < reg_event->evt_data.size))
			reg_event->evt_data.size = prev_size;

		if ((reg_event->evt_dest.rcvid > 0) &&
			(cur_size > reg_event->evt_data.size) &&
			((cur_size - reg_event->evt_data.size) >= reg_event->evt_reg.mc.info.val.delta))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mc.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mc.sig);
			if (!(reg_event->evt_reg.mc.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
			else
				reg_event->evt_data.size = cur_size;
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mc_delta_decr_events
 * 
 * Send an event to all listeners in list <evlist> for which the delta between
 * the current size value and the size at the time the last event was sent
 * is >= the registered 'delta' value on decrement
*/
static INLINE void
send_mc_delta_decr_events(mempart_evtlist_t *evlist, memsize_t cur_size, memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		/*
		 * memsize_t_INFINITY is a one time only initialization for event
		 * registration. If however 'prev_size' is greater than the currently
		 * recorded size since the last event, we know we have incremented and
		 * we must reset the start point
		*/
		if ((reg_event->evt_data.size == memsize_t_INFINITY) ||
			(prev_size > reg_event->evt_data.size))
			reg_event->evt_data.size = prev_size;

		if ((reg_event->evt_dest.rcvid > 0) &&
			(cur_size < reg_event->evt_data.size) &&
			((reg_event->evt_data.size - cur_size) >= reg_event->evt_reg.mc.info.val.delta))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mc.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mc.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mc.sig);
			if (!(reg_event->evt_reg.mc.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
			else
				reg_event->evt_data.size = cur_size;
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mp_delta_incr_events
 * 
 * Send an event to all listeners in list <evlist> for which the delta between
 * the current partition size and the size at the time the last event was sent
 * is >= the registered 'delta' value on increment
*/
static INLINE void
send_mp_delta_incr_events(mempart_evtlist_t *evlist, memsize_t cur_size, memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		/*
		 * memsize_t_INFINITY is a one time only initialization for event
		 * registration. If however 'prev_size' is less than the currently
		 * recorded size since the last event, we know we have decremented and
		 * we must reset the start point
		*/
		if ((reg_event->evt_data.size == memsize_t_INFINITY) ||
			(prev_size < reg_event->evt_data.size))
			reg_event->evt_data.size = prev_size;

		if ((reg_event->evt_dest.rcvid > 0) &&
			(cur_size > reg_event->evt_data.size) &&
			((cur_size - reg_event->evt_data.size) >= reg_event->evt_reg.mp.info.val.delta))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mp.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				if (cur_size > UINT_MAX) crash();	// FIX ME - need to handle 64bit size value somehow
				reg_event->evt_reg.mp.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mp.sig);
			if (!(reg_event->evt_reg.mp.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
			else
				reg_event->evt_data.size = cur_size;
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mp_delta_decr_events
 * 
 * Send an event to all listeners in list <evlist> for which the delta between
 * the current partition size and the size at the time the last event was sent
 * is >= the registered 'delta' value on decrement
*/
static INLINE void
send_mp_delta_decr_events(mempart_evtlist_t *evlist, memsize_t cur_size, memsize_t prev_size)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		/*
		 * memsize_t_INFINITY is a one time only initialization for event
		 * registration. If however 'prev_size' is greater than the currently
		 * recorded size since the last event, we know we have incremented and
		 * we must reset the start point
		*/
		if ((reg_event->evt_data.size == memsize_t_INFINITY) ||
			(prev_size > reg_event->evt_data.size))
			reg_event->evt_data.size = prev_size;

		if ((reg_event->evt_dest.rcvid > 0) &&
			(cur_size < reg_event->evt_data.size) &&
			((reg_event->evt_data.size - cur_size) >= reg_event->evt_reg.mp.info.val.delta))
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mp.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				/*
				 * if the 'cur_size' is too large to fit in the sival_int field
				 * then send back UINT_MAX. This otherwise illegal size value
				 * (because UINT_MAX & (__PAGESIZE - 1) != 0) will be an indication
				 * to the event receiver that o information was provided and
				 * that they need to request the current size if they need it
				*/
				if (cur_size > UINT_MAX)
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = UINT_MAX;
				else
					reg_event->evt_reg.mp.sig.sigev_value.sival_int = (_Uint32t)cur_size;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mp.sig);
//kprintf("sent event on id %x @ cur = %P\n", reg_event->rcvid, cur_size);
			if (!(reg_event->evt_reg.mp.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
			else
				reg_event->evt_data.size = cur_size;
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mp_event
 * 
 * Unconditionally send event data <ev> to all listeners in list <evlist>.
 * The events are not removed when the event is sent
*/
static INLINE void send_mp_event(mempart_evtlist_t *evlist, void **val, bool (*match_func)(void **a, void **b))
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if (reg_event->evt_dest.rcvid > 0)
		{
			/*
			 * if the caller did not provide a match function, or a match
			 * function was provided and it returns TRUE, deliver the event
			*/
			if ((match_func == NULL) ||
				(match_func((void *)&reg_event->evt_reg.mp.info.val, val) == bool_t_TRUE))
			{
				/*
				 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
				 * return some useful information
				*/
				if (reg_event->evt_reg.mp.sig.sigev_notify == SIGEV_SIGNAL_CODE)
				{
					reg_event->evt_reg.mp.sig.sigev_value.sival_ptr = *val;
				}
				deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mp.sig);
				if (!(reg_event->evt_reg.mp.flags & mempart_evtflags_REARM))
				{
					/* remove the event from the list */
					LIST_DEL(*evlist, reg_event);
				}
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * send_mc_event
 * 
 * Unconditionally send event data <ev> to all listeners in list <evlist>.
 * The events are not removed when the event is sent
*/
static INLINE void send_mc_event(mempart_evtlist_t *evlist, void **val)
{
	mempart_evt_t  *reg_event = (mempart_evt_t *)LIST_FIRST(*evlist);

	while (reg_event != NULL)
	{
		if (reg_event->evt_dest.rcvid > 0)
		{
			/*
			 * if the caller has registered for a SIGEV_SIGNAL_CODE, we can
			 * return some useful information
			*/
			if (reg_event->evt_reg.mc.sig.sigev_notify == SIGEV_SIGNAL_CODE)
			{
				reg_event->evt_reg.mc.sig.sigev_value.sival_ptr = *val;
			}
			deliver_event(&reg_event->evt_dest, &reg_event->evt_reg.mc.sig);
			if (!(reg_event->evt_reg.mc.flags & mempart_evtflags_REARM))
			{
				/* remove the event from the list */
				LIST_DEL(*evlist, reg_event);
			}
		}
		reg_event = (mempart_evt_t *)LIST_NEXT(reg_event);
	}
}

/*
 * _mempartmgr_event_trigger
 * 
 * This is the routine which is made available externally to the memory
 * partitioning module and performs initial event processing
*/
static void _mempartmgr_event_trigger(mempartmgr_attr_t *mpart, memclass_evttype_t evtype,
										memsize_t cur_size, memsize_t prev_size)
{
	mempart_evtlist_t  *evt_list;

	/*
	 * it is possible that there is no mempartmgr_attr_t yet even though there
	 * is an mempart_t (ie. mempart_t.resmgr_attr_p == NULL)
	*/
	if (mpart == NULL)
		return;

	/* check to see if there are any events of this type registered */
	evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
	if (LIST_COUNT(*evt_list) == 0)
		return;

	switch (evtype)
	{
		case mempart_evttype_t_THRESHOLD_CROSS_OVER:
		case mempart_evttype_t_THRESHOLD_CROSS_UNDER:
		{
#ifdef DISPLAY_EVENT_MSG
			kprintf("THRESHOLD_CROSS %s: mp %p (%s) from %P to %P\n",
					(cur_size > prev_size) ? "over" : ((cur_size < prev_size) ? "under" : "?"),
					mpart, mpart->name, (paddr_t)prev_size, (paddr_t)cur_size);
#endif	/* DISPLAY_EVENT_MSG */
			if (cur_size > prev_size)
			{
				send_mp_threshold_X_over_events(evt_list, cur_size, prev_size);
			}
			else if (cur_size < prev_size)
			{
				send_mp_threshold_X_under_events(evt_list, cur_size, prev_size);
			}
#ifndef NDEBUG
			else crash();
#endif	/* NDEBUG */
			break;
		}

		case mempart_evttype_t_DELTA_INCR:
		case mempart_evttype_t_DELTA_DECR:
		{
#ifdef DISPLAY_EVENT_MSG
			kprintf("DELTA %s: mp %p (%s) from %P to %P\n",
					(cur_size > prev_size) ? "incr" : ((cur_size < prev_size) ? "decr" : "?"),
					mpart, mpart->name, (paddr_t)prev_size, (paddr_t)cur_size);
#endif	/* DISPLAY_EVENT_MSG */
			if (cur_size > prev_size)
			{
				SEND_MP_DELTA_INCR_EVENTS(evt_list, cur_size, prev_size);
			}
			else if (cur_size < prev_size)
			{
				SEND_MP_DELTA_DECR_EVENTS(evt_list, cur_size, prev_size);
			}
#ifndef NDEBUG
			else crash();
#endif	/* NDEBUG */
			break;
		}

		default:
#ifdef DISPLAY_EVENT_MSG
			kprintf("Unknown event: %d\n", evtype);
#endif	/* DISPLAY_EVENT_MSG */
			break;
	}
}

/*
 * __mempartmgr_event_trigger2
 * 
 * This routine will handle all of the possible memory class DELTA and
 * THRESHOLD_CROSS events. It is only called from _mempartmgr_event_trigger2()
*/
static INLINE void __mempartmgr_event_trigger2(mempartmgr_attr_t *mpart, memclass_evttype_t evtype,
										memsize_t cur_size, memsize_t prev_size)
{
	mempart_evtlist_t  *evt_list;

	/* check to see if there are any events of this type registered */
	evt_list = &mpart->event_list[MEMCLASS_EVTYPE_IDX(evtype)];
	if (LIST_COUNT(*evt_list) == 0)
		return;

	switch (evtype)
	{
		/* handle all the deltas */
		case memclass_evttype_t_DELTA_TF_INCR:
		case memclass_evttype_t_DELTA_TF_DECR:
		case memclass_evttype_t_DELTA_RF_INCR:
		case memclass_evttype_t_DELTA_RF_DECR:
		case memclass_evttype_t_DELTA_UF_INCR:
		case memclass_evttype_t_DELTA_UF_DECR:
		case memclass_evttype_t_DELTA_TU_INCR:
		case memclass_evttype_t_DELTA_TU_DECR:
		case memclass_evttype_t_DELTA_RU_INCR:
		case memclass_evttype_t_DELTA_RU_DECR:
		case memclass_evttype_t_DELTA_UU_INCR:
		case memclass_evttype_t_DELTA_UU_DECR:
		{

#ifdef DISPLAY_EVENT_MSG
			kprintf("%s: mp %p (%s) from %P to %P\n", mclass_evt_string(evtype),
					mpart, mpart->name, (paddr_t)prev_size, (paddr_t)cur_size);
#endif	/* DISPLAY_EVENT_MSG */

			if (cur_size > prev_size)
			{
				SEND_MC_DELTA_INCR_EVENTS(evt_list, cur_size, prev_size);
			}
			else if (cur_size < prev_size)
			{
				SEND_MC_DELTA_DECR_EVENTS(evt_list, cur_size, prev_size);
			}
#ifndef NDEBUG
			else crash();
#endif	/* NDEBUG */
			break;
		}

		case memclass_evttype_t_THRESHOLD_CROSS_TF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_TU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_RF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_UF_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_RU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER:
		case memclass_evttype_t_THRESHOLD_CROSS_UU_OVER:
		case memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER:
		{

#ifdef DISPLAY_EVENT_MSG
			kprintf("%s: mp %p (%s) from %P to %P\n", mclass_evt_string(evtype),
					mpart, mpart->name, (paddr_t)prev_size, (paddr_t)cur_size);
#endif	/* DISPLAY_EVENT_MSG */

			if (cur_size > prev_size)
			{
				send_mc_threshold_X_over_events(evt_list, cur_size, prev_size);
			}
			else if (cur_size < prev_size)
			{
				send_mc_threshold_X_under_events(evt_list, cur_size, prev_size);
			}
#ifndef NDEBUG
			else crash();
#endif	/* NDEBUG */
			break;
		}

		default:
#ifdef DISPLAY_EVENT_MSG
			kprintf("Unknown event: %d\n", evtype);
#endif	/* DISPLAY_EVENT_MSG */
			break;
	}
}

/*
 * _mempartmgr_event_trigger2
 * 
 * This routine will take the individual memory class DELTA events and generate
 * the applicable THRESHOLD_CROSS events. __mempartmgr_event_trigger2() is
 * called to actually deliver the event.
 * This routne is only called from the main mempartmgr_event_trigger2() function.
 * The only events processed by _mempartmgr_event_trigger2() are DELTA events.
*/
static INLINE void _mempartmgr_event_trigger2(mempartmgr_attr_t *mpart, memclass_evttype_t evtype,
										memsize_t cur_size, memsize_t prev_size)
{
	/* send the delta event */
	__mempartmgr_event_trigger2(mpart, evtype, cur_size, prev_size);

	switch (evtype)
	{
		/* handle all the deltas */
		case memclass_evttype_t_DELTA_TF_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_TF_OVER;
			break;
		case memclass_evttype_t_DELTA_TF_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER;
			break;
		case memclass_evttype_t_DELTA_RF_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_RF_OVER;
			break;
		case memclass_evttype_t_DELTA_RF_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER;
			break;
		case memclass_evttype_t_DELTA_UF_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_UF_OVER;
			break;
		case memclass_evttype_t_DELTA_UF_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER;
			break;
		case memclass_evttype_t_DELTA_TU_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_TU_OVER;
			break;
		case memclass_evttype_t_DELTA_TU_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER;
			break;
		case memclass_evttype_t_DELTA_RU_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_RU_OVER;
			break;
		case memclass_evttype_t_DELTA_RU_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER;
			break;		
		case memclass_evttype_t_DELTA_UU_INCR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_UU_OVER;
			break;
		case memclass_evttype_t_DELTA_UU_DECR:
			evtype = memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER;
			break;
		default:
#ifdef NDEBUG
			crash();
#endif	/* NDEBUG */
			evtype = memclass_evttype_t_INVALID;
			break;
	}
	
	if (evtype != memclass_evttype_t_INVALID)
		__mempartmgr_event_trigger2(mpart, evtype, cur_size, prev_size);
}

/*
 * =============================================================================
 * 
 * 						E V E N T   A P I   R O U T I N E S
 * 
 * =============================================================================
*/

/*******************************************************************************
 * mempartmgr_event_trigger
 * 
 * This is the routine which is made available externally to the memory
 * partitioning module and performs initial event processing
*/
void mempartmgr_event_trigger(mempartmgr_attr_t *mpart, mempart_evttype_t evtype, ...)
{
	mempart_evtlist_t  *evt_list;
	va_list  ap;
	va_start(ap, evtype);

// kprintf("%s: mp = %p, evt = %d\n", __func__, mpart, evtype);

	/*
	 * it is possible that there is no mempartmgr_attr_t yet even though there
	 * is an mempart_t (ie. mempart_t.resmgr_attr_p == NULL)
	*/
	if (mpart == NULL)
		return;

	switch (evtype)
	{
		case mempart_evttype_t_DELTA_INCR:
		case mempart_evttype_t_DELTA_DECR:
		{
			/*
			 * additional arguments:
			 * arg1 - current partition size
			 * arg2 - previous partition size (before the increment/decrement
			*/
			memsize_t  cur_size = va_arg(ap, memsize_t);
			memsize_t  prev_size = va_arg(ap, memsize_t);

			/*
			 * Since multiple events may be triggered from the DELTA_INCR/DECR,
			 * the check for registered events will be done in _mempartmgr_event_trigger()
			*/
			_mempartmgr_event_trigger(mpart, evtype, cur_size, prev_size);
			if (cur_size > prev_size)	
				_mempartmgr_event_trigger(mpart, mempart_evttype_t_THRESHOLD_CROSS_OVER, cur_size, prev_size);
			else if (cur_size < prev_size)
				_mempartmgr_event_trigger(mpart, mempart_evttype_t_THRESHOLD_CROSS_UNDER, cur_size, prev_size);
#ifndef NDEBUG
			else crash();
#endif	/* NDEBUG */
			break;
		}

		case mempart_evttype_t_PROC_ASSOCIATE:
		case mempart_evttype_t_PROC_DISASSOCIATE:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - pid_t of the associated/disassociated process
				*/
				pid_t  pid = va_arg(ap, pid_t);

#ifdef DISPLAY_EVENT_MSG
			kprintf("PROC_%sASSOCIATE: pid %d %s associated with mp %p (%s)\n",
						(evtype == mempart_evttype_t_PROC_DISASSOCIATE) ? "DIS" : "", pid,
						(evtype == mempart_evttype_t_PROC_DISASSOCIATE) ? "no longer" : "now",
						mpart, mpart->name);
#endif	/* DISPLAY_EVENT_MSG */

				/*
				 * for disassociation events, we optionally check whether a
				 * specific process id of interest has been provided
				*/
				if (evtype == mempart_evttype_t_PROC_DISASSOCIATE)
					send_mp_event(evt_list, (void *)&pid, pid_match);
				else
					send_mp_event(evt_list, (void *)&pid, NULL);
			}
			break;
		}

		case mempart_evttype_t_CONFIG_CHG_ATTR_MIN:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - previous configuration (mempart_cfg_t *)
				 * arg2 - current configuration (mempart_cfg_t *)
				 * 
				 * From these args the 'cfg_chg.attr.min' will be built an sent
				 * to any registered processes
				*/
				mempart_cfg_t  *prev = va_arg(ap, mempart_cfg_t *);
				mempart_cfg_t  *cur = va_arg(ap, mempart_cfg_t *);

				if (cur->attr.size.min != prev->attr.size.min)
				{
#ifdef DISPLAY_EVENT_MSG
					kprintf("CONFIG_CHG_ATTR_MIN: mp %p (%s) from %P to %P\n",
							mpart, mpart->name, (paddr_t)prev->attr.size.min,
							(paddr_t)cur->attr.size.min);
#endif	/* DISPLAY_EVENT_MSG */
					send_mp_event(evt_list, (void *)&cur->attr.size.min, NULL);
				}
			}
			break;
		}

		case mempart_evttype_t_CONFIG_CHG_ATTR_MAX:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - previous configuration (mempart_cfg_t *)
				 * arg2 - current configuration (mempart_cfg_t *)
				 * 
				 * From these args the 'cfg_chg.attr.max' will be built an sent
				 * to any registered processes
				*/
				mempart_cfg_t  *prev = va_arg(ap, mempart_cfg_t *);
				mempart_cfg_t  *cur = va_arg(ap, mempart_cfg_t *);

				if (cur->attr.size.max != prev->attr.size.max)
				{
#ifdef DISPLAY_EVENT_MSG
					kprintf("CONFIG_CHG_ATTR_MAX: mp %p (%s) from %P to %P\n",
							mpart, mpart->name, (paddr_t)prev->attr.size.max,
							(paddr_t)cur->attr.size.max);
#endif	/* DISPLAY_EVENT_MSG */
					send_mp_event(evt_list, (void *)&cur->attr.size.max, NULL);
				}
			}
			break;
		}

		case mempart_evttype_t_CONFIG_CHG_POLICY:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - previous configuration (mempart_cfg_t *)
				 * arg2 - current configuration (mempart_cfg_t *)
				 * 
				 * Note that the policies in the previous configuration are munged
				 * and must be converted to boolean values
				 * 
				 * From these args a 'cfg_chg.policy' will be built an sent to any
				 * registered processes
				*/
				union cfg_chg_s  cfgchg;
				mempart_cfg_t  *prev = va_arg(ap, mempart_cfg_t *);
				mempart_cfg_t  *cur = va_arg(ap, mempart_cfg_t *);

				if ((cur->policy.terminal != prev->policy.terminal) ||
					(cur->policy.config_lock != prev->policy.config_lock) ||
					(cur->policy.permanent != prev->policy.permanent) ||
					(cur->policy.alloc != prev->policy.alloc))
				{
					cfgchg.policy.b = (cur->policy.terminal ? cfgchg_t_TERMINAL_POLICY : 0);
					cfgchg.policy.b |= (cur->policy.config_lock ? cfgchg_t_LOCK_POLICY : 0);
					cfgchg.policy.b |= (cur->policy.permanent ? cfgchg_t_PERMANENT_POLICY : 0);
					cfgchg.policy.alloc = cur->policy.alloc;
				
#ifdef DISPLAY_EVENT_MSG
					kprintf("CONFIG_CHG_POLICY: mp %p (%s) alloc/term/cfg_lck/perm = %d/%c/%c/%c\n",
							mpart, mpart->name,
							cfgchg.policy.alloc,
							(cfgchg.policy.bool & cfgchg_t_TERMINAL_POLICY) ? 'T' : 'F',
							(cfgchg.policy.bool & cfgchg_t_LOCK_POLICY) ? 'T' : 'F',
							(cfgchg.policy.bool & cfgchg_t_PERMANENT_POLICY) ? 'T' : 'F');
#endif	/* DISPLAY_EVENT_MSG */

					send_mp_event(evt_list, (void *)&cfgchg, NULL);
				}
			}
			break;
		}

		case mempart_evttype_t_PARTITION_CREATE:
		case mempart_evttype_t_PARTITION_DESTROY:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - mempart_t * to the created partition
				 * Note that the first argument <mpart> is the parent for which
				 * child partition add/del events were registered on
				*/
				mempart_id_t id = MEMPART_T_TO_ID(va_arg(ap, mempart_t *));

#ifdef DISPLAY_EVENT_MSG
				kprintf("PARTITION_%s: %p added to parent %p (%s)\n",
						(evtype == mempart_evttype_t_PARTITION_CREATE) ? "CREATE" : "DESTROY", id,
						mpart, mpart->name);
#endif	/* DISPLAY_EVENT_MSG */

				/*
				 * for destroy events, we optionally check whether a
				 * specific partition id of interest has been provided
				*/
				if (evtype == mempart_evttype_t_PARTITION_DESTROY)
					send_mp_event(evt_list, (void *)&id, mpid_match);
				else
					send_mp_event(evt_list, (void *)&id, NULL);
			}
			break;
		}
		
		case mempart_evttype_t_OBJ_ASSOCIATE:
		case mempart_evttype_t_OBJ_DISASSOCIATE:
		{
			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMPART_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
				/*
				 * addition arguments:
				 * arg1 - OBJECT * of the associated/disassociated object
				*/
				OBJECT  *obj = va_arg(ap, OBJECT *);
#ifdef DISPLAY_EVENT_MSG
				kprintf("OBJ_%sASSOCIATE: obj %p now associated with mp %p (%s)\n",
						(evtype == mempart_evttype_t_OBJ_DISASSOCIATE) ? "DIS" : "", obj,
						(evtype == mempart_evttype_t_OBJ_DISASSOCIATE) ? "no longer" : "now",
						mpart, mpart->name);
#endif	/* DISPLAY_EVENT_MSG */

				/*
				 * for disassociation events, we optionally check whether a
				 * specific process id of interest has been provided
				*/
				if (evtype == mempart_evttype_t_OBJ_DISASSOCIATE)
					send_mp_event(evt_list, (void *)&obj, oid_match);
				else
					send_mp_event(evt_list, (void *)&obj, NULL);
			}
			break;
		}

		default:
#ifdef DISPLAY_EVENT_MSG
			kprintf("Unknown event: %d\n", evtype);
#endif	/* DISPLAY_EVENT_MSG */
			break;
	}
	va_end(ap);
}

/*******************************************************************************
 * mempartmgr_event_trigger2
 * 
 * This is the routine which is made available externally to the memory
 * partitioning module and performs initial event processing for memory classes
 * 
 * this event handler is called from the memory class allocator. The only
 * events sent from the allocator are ...
 * 
 * 	memclass_evttype_t_DELTA_TU_INCR - implying an allocation
 * 	memclass_evttype_t_DELTA_TU_DECR - implying a deallocation
 * 	memclass_evttype_t_DELTA_RF_INCR - implying an increase in the amount of
 * 										reserved memory configured
 * 	memclass_evttype_t_DELTA_RF_DECR - implying a decrease in the amount of
 * 										reserved memory configured
 * 	memclass_evttype_t_DELTA_RU_INCR - implying an exchange of memory as being
 * 										accounted to reserved where it was
 * 										previously accounted as unreserved
 *	memclass_evttype_t_DELTA_RU_DECR - implying an exchange of memory as being
 * 										accounted to unreserved where it was
 * 										previously accounted as reserved
 * 	memclass_evttype_t_PARTITION_CREATE	- a partition of the memory class was
 * 										created
 * 	memclass_evttype_t_PARTITION_DESTROY- a partition of the memory class was
 * 										destroyed
 * 
 * Note that all of the other events can be generated based on the previous
 * and current size information.
 * 
 * This function will generate all of the sub DELTA events from the primary
 * events received from the allocator and call _mempartmgr_event_trigger2() to
 * deliver them. _mempartmgr_event_trigger2() will in turn generate all
 * applicable THRESHOLD_CROSS events. Ultimately, __mempartmgr_event_trigger2()
 * will be called to deliver any registered events.
 *
 * The PARTITION_CREATE/DESTROY events are handled directly by this routine.
 * Also, the PARTITION_CREATE/DESTROY events will send 'size_info' as a pointer
 * to the partition identifier created/destroyed.
 *
 * additional arguments:
 * arg1 - memclass_sizeinfo_t
*/
void mempartmgr_event_trigger2(mempartmgr_attr_t *mpart, memclass_evttype_t evtype,
								memclass_sizeinfo_t *cur_size_info,
								memclass_sizeinfo_t *prev_size_info)
{
	/*
	 * it is possible that there is no mempartmgr_attr_t yet even though there
	 * is an mempart_t (ie. mempart_t.resmgr_attr_p == NULL)
	*/
	if (mpart == NULL)
		return;

	switch (evtype)
	{
		/*
		 * an allocation has occurred for the memory class. This has the
		 * possibility to generate 4 separate events
		 * 
		 * 		total used increment
		 * 		total free decrement
		 * 		reserved used increment
		 * 		unreserved used increment
		 * 		reserved free decrement
		 * 		unreserved free decrement
		 * 
		 * Either 4 of these events will occur or all 6 will occur as the first
		 * 2 are unconditional
		*/
		case memclass_evttype_t_DELTA_TU_INCR:
		{
			if ((cur_size_info->reserved.used + cur_size_info->unreserved.used) >
				(prev_size_info->reserved.used + prev_size_info->unreserved.used))
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_TU_INCR, 
											cur_size_info->reserved.used + cur_size_info->unreserved.used,
											prev_size_info->reserved.used + prev_size_info->unreserved.used);
			}
			if ((cur_size_info->reserved.free + cur_size_info->unreserved.free) <
				(prev_size_info->reserved.free + prev_size_info->unreserved.free))
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_TF_DECR, 
											cur_size_info->reserved.free + cur_size_info->unreserved.free,
											prev_size_info->reserved.free + prev_size_info->unreserved.free);
			}
			if (cur_size_info->reserved.used > prev_size_info->reserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RU_INCR, 
											cur_size_info->reserved.used,
											prev_size_info->reserved.used);
			}
			if (cur_size_info->unreserved.used > prev_size_info->unreserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UU_INCR, 
											cur_size_info->unreserved.used,
											prev_size_info->unreserved.used);
			}
			if (cur_size_info->reserved.free < prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_DECR, 
											cur_size_info->reserved.free,
											prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.free < prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_DECR, 
											cur_size_info->unreserved.free,
											prev_size_info->unreserved.free);
			}
			break;
		}

		/*
		 * a deallocation has occurred for the memory class. This has the
		 * possibility to generate 6 separate events
		 * 
		 * 		total used decrement
		 * 		total free increment
		 * 		reserved used decrement
		 * 		unreserved used decrement
		 * 		reserved free increment
		 * 		unreserved free increment
		 *
		 * Either 4 of these events will occur or all 6 will occur as the first
		 * 2 are unconditional
		*/
		case memclass_evttype_t_DELTA_TU_DECR:
		{
			if ((cur_size_info->reserved.used + cur_size_info->unreserved.used) <
				(prev_size_info->reserved.used + prev_size_info->unreserved.used))
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_TU_DECR, 
											cur_size_info->reserved.used + cur_size_info->unreserved.used,
											prev_size_info->reserved.used + prev_size_info->unreserved.used);
			}
			if ((cur_size_info->reserved.free + cur_size_info->unreserved.free) >
				(prev_size_info->reserved.free + prev_size_info->unreserved.free))
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_TF_INCR, 
											cur_size_info->reserved.free + cur_size_info->unreserved.free,
											prev_size_info->reserved.free + prev_size_info->unreserved.free);
			}
			if (cur_size_info->reserved.used < prev_size_info->reserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RU_DECR, 
											cur_size_info->reserved.used,
											prev_size_info->reserved.used);
			}
			if (cur_size_info->unreserved.used < prev_size_info->unreserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UU_DECR, 
											cur_size_info->unreserved.used,
											prev_size_info->unreserved.used);
			}
			if (cur_size_info->reserved.free > prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_INCR, 
											cur_size_info->reserved.free,
											prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.free > prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_INCR, 
											cur_size_info->unreserved.free,
											prev_size_info->unreserved.free);
			}
			break;
		}
		
		case memclass_evttype_t_DELTA_RF_INCR:
		{
			/*
			 * an increase to reserved memory occurs when a reservation is
			 * established for a partition. In this situation, the only
			 * parameters that change are the reserved.free increases and the
			 * unreserved.free decreases by a corresponding amount. The used
			 * amounts do not change hence there are only 2 possible events to
			 * generate from this event
			 * 
			 *	memclass_evttype_t_DELTA_RF_INCR
			 * 	memclass_evttype_t_DELTA_UF_DECR
			*/
			if (cur_size_info->reserved.free > prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_INCR, 
											cur_size_info->reserved.free, prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.free < prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_DECR, 
											cur_size_info->unreserved.free, prev_size_info->unreserved.free);
			}
			break;
		}

		case memclass_evttype_t_DELTA_RF_DECR:
		{
			/*
			 * a decrease to reserved memory occurs when a reservation is
			 * released for a partition. In this situation, the only
			 * parameters that change are the reserved.free decreases and the
			 * unreserved.free increases by a corresponding amount. The used
			 * amounts do not change hence there are only 2 possible events to
			 * generate from this event
			 * 
			 *	memclass_evttype_t_DELTA_RF_DECR
			 * 	memclass_evttype_t_DELTA_UF_INCR
			*/
			if (cur_size_info->reserved.free < prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_DECR, 
											cur_size_info->reserved.free, prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.free > prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_INCR, 
											cur_size_info->unreserved.free, prev_size_info->unreserved.free);
			}
			break;
		}

		case memclass_evttype_t_DELTA_RU_INCR:
		{
			/*
			 * an increase to reserved used memory occurs when allocated memory
			 * was previously accounted to unreserved and will now be accounted
			 * to reserved. In this situation, the parameters that change are
			 * are the reserved.used increases and the unreserved.used decreases.
			 * Also, in order to keep the totals constant, reserved.free decreases
			 * and the unreserved.free increases by identical amounts. The 4 events
			 * to generate from this event are
			 * 
			 *	memclass_evttype_t_DELTA_RU_INCR
			 * 	memclass_evttype_t_DELTA_RF_DECR
			 * 	memclass_evttype_t_DELTA_UU_DECR
			 * 	memclass_evttype_t_DELTA_UF_INCR
			*/
			if (cur_size_info->reserved.used > prev_size_info->reserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RU_INCR, 
											cur_size_info->reserved.used, prev_size_info->reserved.used);
			}
			if (cur_size_info->reserved.free < prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_DECR,
											cur_size_info->reserved.free, prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.used < prev_size_info->unreserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UU_DECR, 
											cur_size_info->unreserved.used, prev_size_info->unreserved.used);
			}
			if (cur_size_info->unreserved.free > prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_INCR,
											cur_size_info->unreserved.free, prev_size_info->unreserved.free);
			}
			break;
		}

		case memclass_evttype_t_DELTA_RU_DECR:
		{
			/*
			 * a decrease to reserved used memory occurs when allocated memory
			 * was previously accounted to reserved and will now be accounted
			 * to unreserved. In this situation, the parameters that change are
			 * are the reserved.used decreases and the unreserved.used increases.
			 * Also, in order to keep the totals constant, reserved.free increases
			 * and the unreserved.free decreases by identical amounts. The 4 events
			 * to generate from this event are
			 * 
			 *	memclass_evttype_t_DELTA_RU_DECR
			 * 	memclass_evttype_t_DELTA_RF_INCR
			 * 	memclass_evttype_t_DELTA_UU_INCR
			 * 	memclass_evttype_t_DELTA_UF_DECR
			*/
			if (cur_size_info->reserved.used < prev_size_info->reserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RU_DECR, 
											cur_size_info->reserved.used, prev_size_info->reserved.used);
			}
			if (cur_size_info->reserved.free > prev_size_info->reserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_RF_INCR,
											cur_size_info->reserved.free, prev_size_info->reserved.free);
			}
			if (cur_size_info->unreserved.used > prev_size_info->unreserved.used)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UU_INCR, 
											cur_size_info->unreserved.used, prev_size_info->unreserved.used);
			}
			if (cur_size_info->unreserved.free < prev_size_info->unreserved.free)
			{
				_mempartmgr_event_trigger2(mpart, memclass_evttype_t_DELTA_UF_DECR,
											cur_size_info->unreserved.free, prev_size_info->unreserved.free);
			}
			break;
		}

		case memclass_evttype_t_PARTITION_CREATE:
		case memclass_evttype_t_PARTITION_DESTROY:
		{
			mempart_id_t id = *((mempart_id_t *)cur_size_info);
			mempart_evtlist_t  *evt_list;

			/* check to see if there are any events of this type registered */
			evt_list = &mpart->event_list[MEMCLASS_EVTYPE_IDX(evtype)];
			if (LIST_COUNT(*evt_list) > 0)
			{
#ifdef DISPLAY_EVENT_MSG
				kprintf("%s in memory class %p (%s), id = %p\n",
							mclass_evt_string(evtype), mpart, mpart->name, id);
#endif	/* DISPLAY_EVENT_MSG */
				send_mc_event(evt_list, (void *)&id);
			}
			break;
		}

		default:
#ifdef DISPLAY_EVENT_MSG
			kprintf("Unknown event: %d\n", evtype);
#endif	/* DISPLAY_EVENT_MSG */
			break;
	}
}

/*
 * =============================================================================
 * 
 * 					D E B U G   S U P P O R T   R O U T I N E S
 * 
 * =============================================================================
*/
#if !defined(NDEBUG) && defined(DISPLAY_EVENT_MSG)
static char *mclass_evt_strings[] =
{
	//	free and used crossings (total)
	[memclass_evttype_t_THRESHOLD_CROSS_TF_OVER] = "TOTAL FREE OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_TF_UNDER] = "TOTAL FREE UNDER",
	[memclass_evttype_t_THRESHOLD_CROSS_TU_OVER] = "TOTAL USED OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_TU_UNDER] = "TOTAL USED UNDER",
	//	free crossings (reserved and unreserved)
	[memclass_evttype_t_THRESHOLD_CROSS_RF_OVER] = "RESERVED FREE OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_RF_UNDER] = "RESERVED FREE UNDER",
	[memclass_evttype_t_THRESHOLD_CROSS_UF_OVER] = "UNRESERVED FREE OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_UF_UNDER] = "UNRESERVED FREE UNDER",
	//	used crossings	(reserved and unreserved)
	[memclass_evttype_t_THRESHOLD_CROSS_RU_OVER] = "RESERVED USED OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_RU_UNDER] = "RESERVED USED UNDER",
	[memclass_evttype_t_THRESHOLD_CROSS_UU_OVER] = "UNRESERVED USED OVER",
	[memclass_evttype_t_THRESHOLD_CROSS_UU_UNDER] = "UNRESERVED USED UNDER",
	
	//	free and used deltas (total)
	[memclass_evttype_t_DELTA_TF_INCR] = "TOTAL FREE DELTA INCR",
	[memclass_evttype_t_DELTA_TF_DECR] = "TOTAL FREE DELTA DECR",
	[memclass_evttype_t_DELTA_TU_INCR] = "TOTAL USED DELTA INCR",
	[memclass_evttype_t_DELTA_TU_DECR] = "TOTAL USED DELTA DECR",
	//	free deltas (reserved and unreserved)
	[memclass_evttype_t_DELTA_RF_INCR] = "RESERVED FREE DELTA INCR",
	[memclass_evttype_t_DELTA_RF_DECR] = "RESERVED FREE DELTA DECR",
	[memclass_evttype_t_DELTA_UF_INCR] = "UNRESERVED FREE DELTA INCR",
	[memclass_evttype_t_DELTA_UF_DECR] = "UNRESERVED FREE DELTA DECR",
	//	used deltas (reserved and unreserved)
	[memclass_evttype_t_DELTA_RU_INCR] = "RESERVED USED DELTA INCR",
	[memclass_evttype_t_DELTA_RU_DECR] = "RESERVED USED DELTA DECR",
	[memclass_evttype_t_DELTA_UU_INCR] = "UNRESERVED USED DELTA INCR",
	[memclass_evttype_t_DELTA_UU_DECR] = "UNRESERVED USED DELTA DECR",
	
	[memclass_evttype_t_PARTITION_CREATE] = "MEMCLASS PARTITION CREATE",
	[memclass_evttype_t_PARTITION_DESTROY] = "MEMCLASS PARTITION DESTROY",
};
static char *mclass_evt_string(memclass_evttype_t evt) {return mclass_evt_strings[evt];}
#endif	/* NDEBUG */


__SRCVERSION("$IQ: mempartmgr_event.c,v 1.23 $");

