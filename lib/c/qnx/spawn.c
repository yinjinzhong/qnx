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




#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/procmsg.h>
#include <sys/netmgr.h>

pid_t spawn(const char *path, int fd_count, const int fd_map[],
		const struct inheritance *inherit, char * const *argv, char * const *envp) {
	proc_spawn_t					msg;
	iov_t							iov[6];
	char							*const *arg;
	char							*src;
	char							*search;
	char							*dst, *data;
	pid_t							pid;
	int								coid;
#if 0
	static pthread_mutex_t			exec_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

	msg.i.type = _PROC_SPAWN;
	msg.i.subtype = _PROC_SPAWN_START;

	search = 0;
	if(inherit) {
		msg.i.parms = *inherit;
		if(inherit->flags & SPAWN_SEARCH_PATH) {
			search = getenv("PATH");
		}
		if((inherit->flags & SPAWN_SETND) && ND_NODE_CMP(inherit->nd, ND_LOCAL_NODE) != 0) {
			/* remote spawn */
			msg.i.subtype = _PROC_SPAWN_REMOTE;
		}
	} else {
		msg.i.parms.flags = 0;
	}

	msg.i.nbytes = msg.i.nargv = msg.i.narge = 0;
	if(argv) {
		for(arg = argv; *arg; msg.i.nbytes += strlen(*arg++) + 1, msg.i.nargv++) {
			/* nothing to do */
		}
	}
	if(!envp) envp = environ;
	if(envp) {
		for(arg = envp; *arg; msg.i.nbytes += strlen(*arg++) + 1, msg.i.narge++) {
			/* nothing to do */
		}
	}

	msg.i.nfds = fd_count;

	coid = PROCMGR_COID;
	if(msg.i.subtype == _PROC_SPAWN_REMOTE) {
		spawn_remote_t *premote;
		unsigned len = (msg.i.nbytes + sizeof(int))&~(sizeof(int) - 1);

		if(!(dst = data = malloc(len + SPAWN_REMOTE_MSGBUF_SIZE))) {
			errno = ENOMEM;
			return -1;
		}
		premote = (spawn_remote_t *)(data + len);
		SETIOV(iov + 0, &msg.i, sizeof msg.i);
		SETIOV(iov + 1, fd_map, fd_count * sizeof fd_map[0]);
		SETIOV(iov + 2, &msg.i, sizeof msg.i);
		SETIOV(iov + 3, premote, SPAWN_REMOTE_MSGBUF_SIZE);
		if(MsgSendvnc(coid, iov + 0, 2, iov + 2, 2) == -1) {
			free(data);
			return -1;
		}
		if((coid = ConnectAttach(premote->nd, premote->pid, premote->chid, _NTO_SIDE_CHANNEL, 0)) == -1) {
			free(data);
			return -1;
		}
		SETIOV(iov + 5, (char*)premote + sizeof(*premote), premote->size);
		msg.i.subtype = _PROC_SPAWN_START;
	} else {
		if(!(dst = data = malloc(msg.i.nbytes))) {
			errno = ENOMEM;
			return -1;
		}
		SETIOV(iov + 5, data, 0);
	}

	if(argv) {
		for(arg = argv; (src = *arg); arg++) {
			while((*dst++ = *src++)) {
				/* nothing to do */
			}
		}
	}

	if(envp) {
		for(arg = envp; (src = *arg); arg++) {
			while((*dst++ = *src++)) {
				/* nothing to do */
			}
		}
	}

	SETIOV(iov + 0, &msg.i, sizeof msg.i);
	SETIOV(iov + 1, fd_map, fd_count * sizeof fd_map[0]);
	SETIOV(iov + 2, search, msg.i.searchlen = (search ? strlen(search) + 1 : 0));
	SETIOV(iov + 3, path, msg.i.pathlen = strlen(path) + 1);
	SETIOV(iov + 4, data, msg.i.nbytes);

#if 0
	// Kind of a kludge: only allow one thread in a process to exec
	// at any one time. This avoids many race conditions. We really should
	// fix procnto to handle it though.
	if(inherit->flags & SPAWN_EXEC) {
		pthread_mutex_lock(&exec_mutex);
	}
#endif

	pid = MsgSendvnc(coid, iov + 0, 6, 0, 0);

	if(coid != PROCMGR_COID) {
		ConnectDetach(coid);
	}
#if 0
	if(inherit->flags & SPAWN_EXEC) {
		pthread_mutex_unlock(&exec_mutex);
	}
#endif

	free(data);
	return pid;
}
	

__SRCVERSION("spawn.c $Rev: 153052 $");
