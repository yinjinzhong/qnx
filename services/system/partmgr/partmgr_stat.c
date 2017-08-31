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
 * partmgr_stat
 * 
 * Provide resource manager stat() processing for the partitioning module
 * 
*/

#include "partmgr.h"


int partmgr_stat(resmgr_context_t *ctp, io_stat_t *msg, void *_ocb)
{
	partmgr_ocb_t *ocb = (partmgr_ocb_t *)_ocb;
	PROCESS  *prp;
	int r = EOK;

	if (ocb == NULL)
		return ENOENT;

	if ((ocb->pid <= 0) || (!(prp = proc_lock_pid(ocb->pid))))
		return EBADF;

	switch (ocb->partmgr_type)
	{
		case partmgr_type_NONE:
		case partmgr_type_PART:
			memset(&msg->o, 0x00, sizeof msg->o);
			/*
			 * for partmgr_type_NONE, st_size = 1 (ie. partition/)
			 * for partmgr_type_PART, st_size = 2 (ie. partition/sched/ and partition/mem/)
			*/
			msg->o.st_size = (ocb->partmgr_type == partmgr_type_PART) ? 2 : 1;
			msg->o.st_mode = 0555 | S_IFDIR;
			msg->o.st_ino = ocb->ocb.attr->inode;
			msg->o.st_ctime =
			msg->o.st_mtime =
			msg->o.st_atime = time(0);
			msg->o.st_nlink = ocb->ocb.attr->nlink;
			msg->o.st_dev = partmgr_devno;
			msg->o.st_rdev = (ctp->info.srcnd << 16) | partmgr_devno;
			
			r = EOK;
			break;

		case partmgr_type_MEM:
		{
			int r = iofunc_stat(ctp, &ocb->attr.mem->attr, &msg->o);
			if (r == EOK)
				r = mpmgr_get_st_size(ocb->attr.mem, &msg->o);
			break;
		}

		case partmgr_type_SCHED:
		{
			int r = 0;//iofunc_stat(ctp, &ocb->attr.sched->attr, &msg->o);
			if (r == EOK)
				msg->o.st_size = 0;	// FIX ME
//				r = spmgr_getstat(ocb->attr.sched, &msg->o);
			break;
		}

		default:
			crash();
	}

	proc_unlock(prp);
	return (r != EOK) ? r : _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
}


__SRCVERSION("$IQ: partmgr_stat.c,v 1.23 $");

