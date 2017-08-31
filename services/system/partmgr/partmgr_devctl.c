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
 * partmgr_devctl
 * 
 * Provide resource manager devctl() processing for the partitioning module
 * 
*/

#include "partmgr.h"


int partmgr_devctl(resmgr_context_t *ctp, io_devctl_t *msg, void *_ocb)
{
	partmgr_ocb_t *ocb = (partmgr_ocb_t *)_ocb;
	void	*data = (void *)(msg + 1);
    int		nbytes = 0;

    switch(msg->i.dcmd)
    {
    	case DCMD_ALL_GETFLAGS:
    	{
			int  ioflag = ocb ? ((iofunc_ocb_t *)ocb)->ioflag : 0;
        	*((int *)data) = (ioflag & ~O_ACCMODE) | ((ioflag - 1) & O_ACCMODE);
        	nbytes = sizeof(ioflag);
        	break;
    	}

		case DCMD_ALL_SETFLAGS:
			if(ocb)
				ocb->ocb.ioflag = (ocb->ocb.ioflag & ~O_SETFLAG) | (*((int *)data) & O_SETFLAG);
			break;

    	default:
        	return ENOSYS;
    }

    if(nbytes)
    {
        msg->o.ret_val = 0;
        return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o + nbytes);
    }
    return EOK;
}


__SRCVERSION("$IQ: partmgr_devctl.c,v 1.23 $");

