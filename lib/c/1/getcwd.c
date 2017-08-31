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
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <sys/iomsg.h>
#include "connect.h"

#ifndef PATH_MAX
#define PATH_MAX	1024
#endif

/* @@@ this function appears to create a memory leak when users passes in a null buf */


char *getcwd(char *buf, size_t size) {
	struct _connect_ctrl			ctrl;
	struct _io_connect				msg;		
	int								fd;
	char							*save;
	int saved_errno;

	if(size <= 0) {
		errno = EINVAL;
		return 0;
	}

	/* Should use fpathconf(".", _PC_PATH_MAX) */
	if(!(save = buf) && !(buf = alloca(size = PATH_MAX + 1))) {
		errno = ENOMEM;
		return 0;
	}

	memset(&ctrl, 0x00, sizeof ctrl);
	ctrl.base = _NTO_SIDE_CHANNEL;
	ctrl.path = buf;
	ctrl.pathsize = size;
	ctrl.flags = FLAG_NO_SYM | _IO_CONNECT_RET_CHROOT;
	ctrl.send = MsgSendvnc;
	ctrl.msg = &msg;

	memset(&msg, 0x00, sizeof msg);
	msg.ioflag = O_LARGEFILE | O_NOCTTY;
	msg.subtype = _IO_CONNECT_COMBINE_CLOSE;

	saved_errno=errno;
	if((fd = _connect_ctrl(&ctrl, ".", 0, 0)) == -1) {
		return 0;
	}
	errno=saved_errno;
	ConnectDetach(fd);

	if(buf[0] == '\0') {
		errno = ERANGE;
		return 0;
	}

	//return path relative to last chroot
	if (ctrl.reply->link.chroot_len == strlen(buf)) {
		//we are in the chroot-ed directory
		buf = "/";
	} else { 
		buf += ctrl.reply->link.chroot_len; 
	}
	
	if(save) {
		return buf;
	}

	if(!(save = malloc(size = strlen(buf) + 1))) {
		errno = ENOMEM;
		return 0;
	}

	return strncpy(save, buf, size);
}

__SRCVERSION("getcwd.c $Rev: 153052 $");




