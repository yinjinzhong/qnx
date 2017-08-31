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

#include <sys/procmgr.h>
#include "externs.h"
#include "procmgr_internal.h"

static struct procmgr_event_entry {
	struct procmgr_event_entry			*next;
	struct procmgr_event_entry			**prev;
	int							rcvid;
	unsigned					flags;
	struct sigevent				event;
}							*event_list;

static pthread_mutex_t				event_mutex = PTHREAD_MUTEX_INITIALIZER;

void procmgr_event_destroy(PROCESS *prp) {
	struct procmgr_event_entry			*p;

	if((p = prp->event)) {
		prp->event = 0;
		pthread_mutex_lock(&event_mutex);
		LINK2_REM(p);
		pthread_mutex_unlock(&event_mutex);
		_sfree(p, sizeof *p);
	}
}

void procmgr_trigger(unsigned flags) {
	struct procmgr_event_entry 			*p;
#ifdef VARIANT_gcov
	extern void __bb_exit_func(void);

	if ( flags & PROCMGR_EVENT_SYNC ) {
		__bb_exit_func();
	}
#endif

	pthread_mutex_lock(&event_mutex);
	for(p = event_list; p; p = p->next) {
		if(p->flags & flags) {
			(void)MsgDeliverEvent(p->rcvid, &p->event);
		}
	}
	pthread_mutex_unlock(&event_mutex);
}

int procmgr_event(resmgr_context_t *ctp, proc_event_t *msg) {
	struct procmgr_event_entry 			*p;
	PROCESS								*prp;

	switch(msg->i.subtype) {
	case _PROC_EVENT_NOTIFY:
		if(!(prp = proc_lock_pid(ctp->info.pid))) {
			return ESRCH;
		}

		if(msg->i.flags == 0) {
			procmgr_event_destroy(prp);
		} else {
			if(!(p = prp->event)) {
				if(!(p = _scalloc(sizeof *p))) {
					return proc_error(ENOMEM, prp);
				}
				prp->event = p;
				pthread_mutex_lock(&event_mutex);
				LINK2_BEG(event_list, p, struct procmgr_event_entry);
				pthread_mutex_unlock(&event_mutex);
				p->rcvid = ctp->rcvid;
			}
			p->flags = msg->i.flags;
			p->event = msg->i.event;
		}
		return proc_error(EOK, prp);

	case _PROC_EVENT_TRIGGER:
		if(!proc_isaccess(0, &ctp->info)) {
			if(msg->i.flags & PROCMGR_EVENT_PRIVILEGED) {
				return EPERM;
			}
		}
		procmgr_trigger(msg->i.flags);
		return EOK;

	default:
		break;
	}
	return ENOSYS;
}

__SRCVERSION("procmgr_event.c $Rev: 153052 $");
