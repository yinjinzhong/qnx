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




#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <process.h>
#include <sys/neutrino.h>
#include "xstdio.h"
#include "atexit.h"

//
//This function is used by libgcc.a. See exit.c for explanation.
//

void
_cleanup(void) {
	FILE							*fp;

	while(_atexit_list) {
		if(_atexit_list->func) {
			void (*func) (void) = _atexit_list->func;
			_atexit_list->func = NULL;
			func();
		} else if (_atexit_list->cxa.func) {
			void (*func) (void *arg, int status) = _atexit_list->cxa.func;
			_atexit_list->cxa.func = NULL;
			func(_atexit_list->cxa.arg, 0);
		}
		_atexit_list = _atexit_list->next;
	}

	for(fp = _Files[0]; fp; fp = fp->_NextFile) {
		if(fp->_Mode & _MWRITE) {
			int								n;
			unsigned char					*p;

			for(p = fp->_Buf; p < fp->_Next; p += n) {
				if((n = write(fileno(fp), p, fp->_Next - p)) <= 0) {
					break;
				}
			}
			fp->_Next = fp->_Buf;
		}
	}
}

__SRCVERSION("_cleanup.c $Rev: 153052 $");
