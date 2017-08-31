/*
 * $QNXtpLicenseC:
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




#include <stdlib.h>
#include <sys/types.h>
#include <sys/storage.h>
#include "context.h"

ulong_t bt_stack_from = 0;

#ifdef GET_REGFP
static int
thr_stksegment(stack_t *stkseg) {
    if (stkseg) {
        ulong_t base = (ulong_t)__tls();
        stkseg->ss_sp = (void *)base;       /* hack! */
        stkseg->ss_size = (ulong_t)((char *)base - (char *)__tls()->__stackaddr);
        return 0;
    }
    return -1;
}


static int
get_traceback(void **buffer, int size, int starting_line) {
    ucontext_t uc;
    void *ip = NULL;
    long *fp = NULL;
    ulong_t stacklo, stackhi;
    int  i, found;

    //if (_argv == NULL) return; /* don't try to traceback inside ldd */
    if (size <= 0) {
        return 0;
    }

    uc.uc_mcontext.cpu.ebp = _ebp();
    //uc.uc_mcontext.cpu.eip = starting_line;

    thr_stksegment((void *)&uc.uc_stack);
    uc.uc_stack.ss_flags = 0;
    stacklo = (ulong_t)uc.uc_stack.ss_sp - uc.uc_stack.ss_size;
    stackhi = (ulong_t)uc.uc_stack.ss_sp;

    /*
     * Find the correct frame on the stack 
     */
    for (found = (starting_line == 0), i = 0, fp = (long *)GET_REGFP(&uc.uc_mcontext.cpu);
            fp != NULL && i < size; fp = GET_FRAME_PREVIOUS(fp)) {
        if ((ulong_t)fp < stacklo || (ulong_t)fp > stackhi) {
            break;
        }
        if (bt_stack_from!=0 && (ulong_t)fp < bt_stack_from) {
            continue;
        }
        ip = (void *)GET_FRAME_RETURN_ADDRESS(fp);
        if (ip == NULL) {
            break;
        }
        if (!found) {
            if (starting_line == (int)ip) {
                found = 1;
            }
        }
        if (found && buffer) {
            buffer[i++] = ip;
        }
    }
    /*
     * if we did not found the starting point do it again
     */
    if (!found) {
        i=0;
        if (buffer) {
            // stack trace should start with given address
            if (starting_line!=0)
                buffer[i++] = starting_line;
        }
        for (fp = (long *)GET_REGFP(&uc.uc_mcontext.cpu);
                fp != NULL && i < size; fp = GET_FRAME_PREVIOUS(fp)) {
            if ((ulong_t)fp < stacklo || (ulong_t)fp > stackhi) {
                break;
            }
            if (bt_stack_from!=0 && (ulong_t)fp < bt_stack_from) {
                continue;
            }
            ip = (void *)GET_FRAME_RETURN_ADDRESS(fp);
            if (ip == NULL) {
                break;
            }
            if (buffer) {
                buffer[i++] = ip;
            }
        }
    }
    return i;
}

#else

static int
get_traceback(void **buffer, int size, int starting_line) {
    return 0;
}
#endif


/*
 * Function int backtrace(void **buffer, int size)
 * Place the information in buffer.
 * Return value is the number of entries in buffer at most "size"
 */
int
backtrace(void **buffer, int size, int starting_line) {
    return get_traceback(buffer, size, starting_line);
}
