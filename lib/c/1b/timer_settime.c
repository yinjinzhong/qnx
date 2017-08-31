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




#include <time.h>
#include <errno.h>
#include <sys/neutrino.h>

#define ONE_THOUSAND_MILLION 1000000000L


extern void _timer_round_interval( _Uint64t* intervalp );


int timer_settime(timer_t timerid, int flags, const struct itimerspec *value, struct itimerspec *ovalue) {
	struct _itimer		n, o;
	int					ret;

	if(value) {
		//POSIX 14.2.4.4 error checking
		if(value->it_value.tv_nsec < 0 || value->it_value.tv_nsec >= ONE_THOUSAND_MILLION ||
		   value->it_interval.tv_nsec < 0 || value->it_interval.tv_nsec >= ONE_THOUSAND_MILLION) {
			errno = EINVAL;
			return -1;
		}
		n.nsec = timespec2nsec(&value->it_value);
		n.interval_nsec = timespec2nsec(&value->it_interval);
	}
	if((ret = TimerSettime(timerid, flags, value ? &n : 0, ovalue ? &o : 0)) != -1) {
		if(ovalue) {
			_timer_round_interval(&o.interval_nsec);
			_timer_round_interval(&o.nsec);
			nsec2timespec(&ovalue->it_value, o.nsec);
			nsec2timespec(&ovalue->it_interval, o.interval_nsec);
		}
	}
	return ret;
}

__SRCVERSION("timer_settime.c $Rev: 153052 $");
