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
 * partmgr_read
 * 
 * Provide resource manager read() processing for the partitioning module
 * 
*/

#include "partmgr.h"

/*
 * partmgr_read
 * 
 * Only ever called for a /proc/<pid> and only ever returns a single directory
 * entry for "partition"
*/
int partmgr_read(resmgr_context_t *ctp, io_read_t *msg, void *_ocb)
{
	partmgr_ocb_t  *ocb = (partmgr_ocb_t *)_ocb;
	int  status;
	struct dirent *dir = NULL;
	unsigned int  nbytes = 0;
	PROCESS *prp;
	int  ret = EOK;

	if (ocb == NULL)
		return ENOENT;

	if (msg->i.type != _IO_READ)
		return EBADF;
	else if ((status = iofunc_read_verify(ctp, msg, &ocb->ocb, NULL)) != EOK)
		return status;
	else if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE)
		return ENOSYS;
	else if ((ocb->pid <= 0) || (!(prp = proc_lock_pid(ocb->pid))))
		return EBADF;	// FIX ME - if the process /proc/<pid> is gone
						//		is this ret code adequate to cause the
						//		opener to close thus releasing the ocb ?

	switch (ocb->partmgr_type)
	{
		/*
		 * The ocb is for an open on /proc/<pid>/. The only information to be
		 * read is a single ./partition/ directory entry
		*/
		case partmgr_type_NONE:
		{
			unsigned size = msg->i.nbytes;
			
			if (ocb->ocb.offset > 0)
				break;

			dir = (struct dirent *)(void *)msg;
			memset(dir, 0, sizeof(*dir));

			dir->d_ino = 0;	// FIX ME - need unique ino for all of my resmgrs;
			dir->d_offset = ocb->ocb.offset++;
			memcpy(dir->d_name, "partition", sizeof("partition"));
			dir->d_namelen = sizeof("partition") - 1;
			dir->d_reclen = (sizeof(*dir) + sizeof("partition") + 3) & ~3;
			nbytes += dir->d_reclen;

			if(nbytes > size)
				ret = EMSGSIZE;

			break;
		}

		/*
		 * The ocb is for an open on /proc/<pid>/partition/. There is (currently)
		 * only 2 directory entries to return, ./mem/ and ./sched/
		*/
		case partmgr_type_PART:
		{
			unsigned size = msg->i.nbytes;
			
			if (ocb->ocb.offset > 1)
				break;

			dir = (struct dirent *)(void *)msg;
			memset(dir, 0, min(sizeof(*dir), size - nbytes));

			if (ocb->ocb.offset == 0)
			{
				dir->d_ino = 0;	// FIX ME - need unique ino for all of my resmgrs;
				dir->d_offset = ocb->ocb.offset++;
				memcpy(dir->d_name, "mem", sizeof("mem"));
				dir->d_namelen = sizeof("mem") - 1;
				dir->d_reclen = (sizeof(*dir) + sizeof("mem") + 3) & ~3;
				nbytes += dir->d_reclen;

				dir = (struct dirent *)((unsigned int)dir + (unsigned int)dir->d_reclen);
				memset(dir, 0, min(sizeof(*dir), size - nbytes));
			}
			if (ocb->ocb.offset == 1)
			{
				dir->d_ino = 0;	// FIX ME - need unique ino for all of my resmgrs;
				dir->d_offset = ocb->ocb.offset++;
				memcpy(dir->d_name, "sched", sizeof("sched"));
				dir->d_namelen = sizeof("sched") - 1;
				dir->d_reclen = (sizeof(*dir) + sizeof("sched") + 3) & ~3;
				nbytes += dir->d_reclen;
			}
			dir = (struct dirent *)(void *)msg;	// point back to beginning of buffer

			if(nbytes > size)
				ret = EMSGSIZE;

			break;
		}
		/*
		 * The ocb is for an open on '/proc/<pid>/partition/mem/', The specific
		 * partitions that the process is associated with will be obtained from
		 * the memory partitioning resource manager 
		*/
		case partmgr_type_MEM:
		{
			nbytes = msg->i.nbytes;
			dir = (struct dirent *)(void *)msg;
			memset(dir, 0, nbytes);
			ret = MEMPARTMGR_READ(prp, ocb->attr.mem, (off_t *)&ocb->ocb.offset, dir, &nbytes);
			break;
		} 
		/*
		 * The ocb is for an open on '/proc/<pid>/partition/mem/', The specific
		 * partitions that the process is associated with will be obtained from
		 * the scheduling partitioning resource manager 
		*/
		case partmgr_type_SCHED:
		{
			nbytes = msg->i.nbytes;
			dir = (struct dirent *)(void *)msg;
			memset(dir, 0, nbytes);
			ret = SCHEDPARTMGR_READ(prp, ocb->attr.sched, &ocb->ocb.offset, dir, &nbytes);
			break;
		} 
#ifndef NDEBUG		
		default: crash();
#else	/* NDEBUG */
		default:
			ret = EBADF;
			break;
#endif	/* NDEBUG */
	}
	proc_unlock(prp);

	if ((ret == EOK) && (nbytes > 0))
	{
		resmgr_endian_context(ctp, _IO_READ, S_IFDIR, 0);
		_IO_SET_READ_NBYTES(ctp, nbytes);
		return _RESMGR_PTR(ctp, dir, nbytes);
	}
	else
		return ret;
}


__SRCVERSION("$IQ: partmgr_read.c,v 1.23 $");

