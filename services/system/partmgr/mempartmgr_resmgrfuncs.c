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
 * mempartmgr_resmgrfuncs
 * 
 * Resource manager miscellaneous connect and I/O functions not warranting their
 * own file
 * 
*/

#include "mempartmgr.h"


/*******************************************************************************
 * mempartmgr_chmod
*/
int mempartmgr_chmod(resmgr_context_t *ctp, io_chmod_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	int  r;
	struct _client_info ci;

	if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
		return r;

	/* enforce that only the owner can change the mode */
	if (((r = iofunc_client_info(ctp, ocb->ioflag, &ci)) == EOK) &&
		((r = (ci.cred.euid != mp->attr.uid) ? EACCES : EOK) == EOK))
		r = iofunc_chmod(ctp, msg, ocb, &mp->attr);
	
	MEMPART_ATTR_UNLOCK(mp);
	return r;
}

/*******************************************************************************
 * mempartmgr_chown
*/
int mempartmgr_chown(resmgr_context_t *ctp, io_chown_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	int  r;
	struct _client_info ci;

	if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
		return r;

	if (((r = iofunc_client_info(ctp, ocb->ioflag, &ci)) == EOK) &&
		((r = (ci.cred.euid != mp->attr.uid) ? EACCES : EOK) == EOK))
/*		r = iofunc_chown(ctp, msg, ocb, &mp->attr); */
	{
		if ((msg->i.uid != mp->attr.uid) || (msg->i.gid != mp->attr.gid))
		{
			mp->attr.uid = msg->i.uid;
			mp->attr.gid = msg->i.gid;
			mp->attr.flags |= IOFUNC_ATTR_DIRTY_OWNER;
		}
		// Mark ctime for update
		mp->attr.flags |= (IOFUNC_ATTR_CTIME | IOFUNC_ATTR_DIRTY_TIME);
	}

	MEMPART_ATTR_UNLOCK(mp);
	return r;
}

/*******************************************************************************
 * mempartmgr_lseek
 * 
 * This function provided primarily to support rewinddir()
*/
 #define IS32BIT(_attr, _ioflag) ((!((_ioflag) & O_LARGEFILE)) || ((_attr)->mount != NULL && (_attr)->mount->flags & IOFUNC_MOUNT_32BIT))
int mempartmgr_lseek(resmgr_context_t *ctp, io_lseek_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
	off64_t  offset = msg->i.offset;
	unsigned dirent_max; 

	dirent_max = LIST_COUNT(mp->children);
	if (mp->type == mpart_type_MEMPART_real)
		dirent_max += LIST_COUNT((MEMPART_ID_TO_T(mp->mpid))->prp_list);

	if ((offset < 0) || (offset >= dirent_max))
		return EINVAL;

 	switch (msg->i.whence)
 	{
		case SEEK_SET:
			break;

		case SEEK_CUR:
			offset += ocb->offset;
			break;

		case SEEK_END:
			offset = dirent_max;
			break;

		default:
			return EINVAL;
	}


	if (IS32BIT(&mp->attr, ocb->ioflag) && (offset > LONG_MAX))
		return(EOVERFLOW);

	ocb->offset = offset;

	if (msg->i.combine_len & _IO_COMBINE_FLAG)
		return EOK;

	msg->o = offset;
	return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
}

/*******************************************************************************
 * mempartmgr_unblock
 * 
*/
int mempartmgr_unblock(resmgr_context_t *ctp, io_pulse_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	return iofunc_unblock_default(ctp, msg, ocb);
}

/*******************************************************************************
 * mempartmgr_openfd
 * 
*/
int mempartmgr_openfd(resmgr_context_t *ctp, io_openfd_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	return iofunc_openfd_default(ctp, msg, ocb);
}

/*******************************************************************************
 * mempartmgr_fdinfo
 * 
*/
int mempartmgr_fdinfo(resmgr_context_t *ctp, io_fdinfo_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	return iofunc_fdinfo_default(ctp, msg, ocb);
}



__SRCVERSION("$IQ: mempartmgr_resmgrfuncs.c,v 1.23 $");

