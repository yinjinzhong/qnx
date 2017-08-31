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


#include <fcntl.h>
#include <sys/dcmd_all.h>
#include "externs.h"
#include "pathmgr/pathmgr_object.h"


dev_t schedpartmgr_devno;


static int 
schedpartmgr_write(resmgr_context_t *ctp, io_write_t *msg, void *ocb)
{
	unsigned	ioflag = (uintptr_t)ocb;

	if(!(ioflag & _IO_FLAG_WR))
		return EBADF;
	else
		return EINVAL;
/*
 * no writes supported
	switch(msg->i.xtype & _IO_XTYPE_MASK) {
	case _IO_XTYPE_NONE:
	case _IO_XTYPE_OFFSET:	
		break;
	default:
		// We don't handle this xtype, return error
		return EINVAL;
	}
	_IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);
	return EOK;
*/
}


static int 
schedpartmgr_read(resmgr_context_t *ctp, io_read_t *msg, void *ocb)
{
	unsigned	ioflag = (uintptr_t)ocb;
	int			nbytes;
	int			size;
	char		*buff;

	if(!(ioflag & _IO_FLAG_RD))
	{
		return EBADF;
	}

	// Find size and start of msg buffer
	size = ctp->msg_max_size - ctp->offset - sizeof msg->i;
	buff = (char *)(&msg->i + 1);

	// Start writing zeros to client
	memset(buff, 0x00, nbytes = min(size, msg->i.nbytes));
	SETIOV(ctp->iov + 0, buff, nbytes);
	if(msg->i.nbytes > size)
	{
		int					offset;

		nbytes = msg->i.nbytes;
		offset = 0;
		while(nbytes > size)
		{
			if((nbytes -= size) < size)
			{
				SETIOV(ctp->iov + 0, buff, nbytes);
			}
			offset += size;

			// use resmgr as it takes care of combined message offsets.
			if(resmgr_msgwritev(ctp, ctp->iov + 0, 1, offset) == -1)
			{
				return errno;
			}
		}
		SETIOV(ctp->iov + 0, buff, size);
	}

	_IO_SET_READ_NBYTES(ctp, msg->i.nbytes);
	return _RESMGR_NPARTS(1);
}


static int 
schedpartmgr_stat(resmgr_context_t *ctp, io_stat_t *msg, void *ocb)
{
	memset(&msg->o, 0x00, sizeof msg->o);
	msg->o.st_ino = 2;
	msg->o.st_size = 0x3333;
	msg->o.st_ctime =
	msg->o.st_mtime =
	msg->o.st_atime = time(0);
	msg->o.st_mode = 0666 | S_IFDIR;
	msg->o.st_nlink = 1;
	msg->o.st_dev = path_devno;
	msg->o.st_rdev = (ctp->info.srcnd << 16) | schedpartmgr_devno;
	return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
}


static int
schedpartmgr_devctl(resmgr_context_t *ctp, io_devctl_t *msg, void *ocb)
{
    union
    {
        int                 oflag;
        int                 mountflag;
    }                   *data = (void *)(msg + 1);
    unsigned            ioflag = (uintptr_t)ocb;
    int                 nbytes = 0;

    switch(msg->i.dcmd)
    {
    	case DCMD_ALL_GETFLAGS:
        	data->oflag = (ioflag & ~O_ACCMODE) | ((ioflag - 1) & O_ACCMODE);
        	nbytes = sizeof data->oflag;
        	break;

    	case DCMD_ALL_SETFLAGS:
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

static const resmgr_io_funcs_t schedpartmgr_io_funcs =
{
	_RESMGR_IO_NFUNCS,
	schedpartmgr_read,
	schedpartmgr_write,
	0,
	schedpartmgr_stat,
	0,
	schedpartmgr_devctl
};


int 
schedpartmgr_open(resmgr_context_t *ctp, io_open_t *msg, void *extra, void *reserved)
{
	if(resmgr_open_bind(ctp, (void *)msg->connect.ioflag, 0) == -1)
	{
		return errno;
	}
	return EOK;
}


static const resmgr_connect_funcs_t schedpartmgr_connect_funcs =
{
	_RESMGR_CONNECT_NFUNCS,
	schedpartmgr_open
};

int 
schedpartmgr_check(const resmgr_io_funcs_t *funcs, mem_map_t *msg, void *handle, OBJECT **pobp)
{
	unsigned	ioflag = (uintptr_t)handle;

	if(funcs != &schedpartmgr_io_funcs)
	{
		return -1;
	}
	*pobp = NULL;
	if(msg != NULL)
	{
		if((ioflag & _IO_FLAG_RD) == 0)
		{
			return EACCES;
		}
		if(((msg->i.flags & MAP_TYPE) == MAP_SHARED) && ((ioflag & _IO_FLAG_WR) == 0))
		{
			if(msg->i.prot & PROT_WRITE)
			{
				//RUSH3: Is this test needed anymore - vmm_mmap() should catch it now
				return EACCES;
			}
			// Indicate that we can't allow PROT_WRITE later...
			msg->i.flags |= IMAP_OCB_RDONLY;
		}
		// Turn it into anonymous memory.
		msg->i.flags |= MAP_ANON;
	}
	return EOK;
}

#define strcat(d, s)	STRLCPY(&d[strlen(d)], s, strlen(s) + 1)

void 
schedpartmgr_init(char *basepath)
{
	char name[PATH_MAX + 1];
	resmgr_attr_t  attr;

	STRLCPY(name, basepath, sizeof(name));
	if ((strlen(name) + strlen("/sched")) >= sizeof(name))
		return;

	strcat(name, "/sched");

    /* initialize resource manager attributes */
    memset(&attr, 0, sizeof(attr));
    attr.nparts_max = 1;
    attr.msg_max_size = 2048;
	attr.flags |= _RESMGR_FLAG_DIR;
	// allocate schedpartmgr directory entry
	resmgr_attach(dpp, NULL, name, _FTYPE_ANY, _RESMGR_FLAG_DIR | _RESMGR_FLAG_BEFORE, &schedpartmgr_connect_funcs, &schedpartmgr_io_funcs, 0);
//	rsrcdbmgr_proc_devno("part", &schedpartmgr_devno, -1, 0);
}

__SRCVERSION("$IQ: schedpartmgr.c,v 1.23 $");

