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
#include <unistd.h>

struct kerargs_trace_args {
	int 	ev_class;
	int 	ev_code;
	int 	iov_cnt;
	IOV 	*iovs;
};

#if defined(VARIANT_instr)

/**
 * This function belongs in nano_trace as part of a generic filter function.  
 * However, since there are no generic filter functions in place yet, and all
 * of the filtering is actually done in a hard coded fashion, we need to have
 * a migration path to this generic filter function.
 *
 * This function serves as the initial generic filter point for the process
 * manager events.  It will reject any events that it isn't configured for
 * initially, though eventually it should be configured to allow other 
 * events to be emitted through this interface.
 *
 * Oh the woes of legacy code
 * Return 0 if the event can not be emitted
 */
//Stolen from nano_trace
#define _TRACE_CKH_EXE_EHB(e1, e2, a1, a2, a3)    \
    ((!(e1) || exe_event_h_buff(1, e1, a1, a2, a3)) && \
     (!(e2) || exe_event_h_buff(1, e2, a1, a2, a3)))

static int can_emit_event(int header, IOV *iov, int *iovadjustedcount) {
	int event_class = _NTO_TRACE_GETEVENT_C(header);
	int event_code = _NTO_TRACE_GETEVENT(header);

	switch(event_class) {
	case _TRACE_SYSTEM_C:
		switch(event_code) {
		case _NTO_TRACE_SYS_PATHMGR:
		case _NTO_TRACE_SYS_MMAP:
		case _NTO_TRACE_SYS_MUNMAP:
		case _NTO_TRACE_SYS_MAPNAME:
			if(!(trace_masks.system_mask[event_code] & _TRACE_ENTER_SYSTEM)) {
				return 0;
			}
			//Non-wide mode may require a reduction in data emitted
			if(!(trace_masks.system_mask[event_code] & _TRACE_SYSTEM_ARG_WIDE)) {
				switch(event_code) {
				case _NTO_TRACE_SYS_MMAP:			//Depend
					if((*iovadjustedcount) > 3) {
						*iovadjustedcount = 3;
					}
					break;
				default: break;
				}
			}

			//We only check the first bit of the data
			return _TRACE_CKH_EXE_EHB(trace_masks.class_system_ehd_p,
									  trace_masks.system_ehd_p[event_code],
									  header,
									  GETIOVBASE(iov),
									  GETIOVLEN(iov));

		default:
			break;
		}
		break;

	default:
		break;
	}

	return 0;		//The default is not to emit any event
}


void kerext_trace(void *data) {
	struct kerargs_trace_args *kap = data;

	if(kap->iov_cnt > 0) {
		int header, iovcnt;

		lock_kernel();

		iovcnt = kap->iov_cnt;
 		header = _TRACE_MAKE_CODE(RUNCPU, _TRACE_STRUCT_S, kap->ev_class, kap->ev_code);

		if(can_emit_event(header, kap->iovs, &iovcnt)) {
			add_trace_iovs(header, kap->iovs, iovcnt);
		}
	}

	SETKSTATUS(actives[KERNCPU], 0);
}

int KerextAddTraceEventIOV(int ev_class, int ev_code, IOV *iovs, int iovcnt) {
	struct kerargs_trace_args args;

	args.ev_class = ev_class;
	args.ev_code = ev_code;
	args.iov_cnt = iovcnt;
	args.iovs = iovs;

	return __Ring0(kerext_trace, (void *)&args);
}

int KerextAddTraceEvent(int ev_class, int ev_code, void *data, int datalen) {
	IOV 	iov[1];

	SETIOV(iov, data, datalen);

	return KerextAddTraceEventIOV(ev_class, ev_code, iov, 1);
}

#else

int KerextAddTraceEventIOV(int ev_class, int ev_code, IOV *iovs, int iovlen) {
	return 0;
}

int KerextAddTraceEvent(int ev_class, int ev_code, void *data, int datalen) {
	return 0;
}

#endif


__SRCVERSION("kerext_trace.c $Rev: 153052 $");
