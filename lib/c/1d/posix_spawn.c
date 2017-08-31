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

#include <kernel/apm_spawn.h>	// FIX ME
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/procmsg.h>
#include <sys/netmgr.h>
#include <fcntl.h>
#include <devctl.h>

/*
 * FIX ME - this temporary define replicated in services/system/procmgr/procmgr_spawn.c
 *
 * Message of _PROC_SPAWN/_POSIX_SPAWN_START
 */
typedef struct
{
	_Uint16t						type;
	_Uint16t						subtype;
	posix_spawnattr_t				attr;
	proc_spawn_t					spawn;

} proc_posixspawn_t;

int apm_posix_spawn(pid_t *pid, const char *path,
					const struct posix_spawn_file_actions_s *file_actions,
					const posix_spawnattr_t *attrp,
        			char * const argv[], char * const envp[])
{
	proc_posixspawn_t				msg;
	iov_t							iov[6];
	char							*const *arg;
	char							*src;
	char							*search;
	char							*dst, *data;
	pid_t							_pid;
	int								coid;
	spawn_inheritance_type			inherit;

	/* all fd's will be inherited as if fd_count was passed as '0' to spawn() */	
	const int fd_map[1];
	const int fd_count = 0;

	if (pid == NULL)
		pid = &_pid;

	msg.type = _PROC_SPAWN;
	msg.subtype = _POSIX_SPAWN_START;

	/* preserve the original message processing */
	msg.spawn.i.type = _PROC_SPAWN;
	msg.spawn.i.subtype = _PROC_SPAWN_START;

	memset(&inherit, 0, sizeof(inherit));
	if (attrp)
	{
		inherit.flags = attrp->flags | attrp->u.__ext.flags2;
		inherit.pgroup = (inherit.flags & SPAWN_SETGROUP) ? attrp->pgrp : 0;
		if (inherit.flags & SPAWN_SETSIGMASK) inherit.sigmask = attrp->ss;
		if (inherit.flags & SPAWN_SETSIGDEF) inherit.sigdefault = attrp->sd;
		inherit.flags &= ~(SPAWN_SETSIGIGN | SPAWN_SETSTACKMAX);	// not supported yet
		inherit.policy = (inherit.flags & SPAWN_EXPLICIT_SCHED) ? attrp->sched_policy : 0;
		inherit.flags &= ~(SPAWN_SETND | SPAWN_EXPLICIT_CPU);	// not supported yet
		if (inherit.flags & SPAWN_EXPLICIT_SCHED) inherit.param = attrp->sp;
	}
	
	search = 0;
	if(inherit.flags != 0) {
		msg.spawn.i.parms = inherit;
		if(inherit.flags & SPAWN_SEARCH_PATH) {
			search = getenv("PATH");
		}
		if((inherit.flags & SPAWN_SETND) && ND_NODE_CMP(inherit.nd, ND_LOCAL_NODE) != 0) {
			/* remote spawn */
			msg.spawn.i.subtype = _PROC_SPAWN_REMOTE;
		}
	} else {
		msg.spawn.i.parms.flags = 0;
	}

	msg.spawn.i.nbytes = msg.spawn.i.nargv = msg.spawn.i.narge = 0;
	if(argv) {
		for(arg = argv; *arg; msg.spawn.i.nbytes += strlen(*arg++) + 1, msg.spawn.i.nargv++) {
			/* nothing to do */
		}
	}
	if(!envp) envp = environ;
	if(envp) {
		for(arg = envp; *arg; msg.spawn.i.nbytes += strlen(*arg++) + 1, msg.spawn.i.narge++) {
			/* nothing to do */
		}
	}

	msg.spawn.i.nfds = fd_count;

	coid = PROCMGR_COID;
/* preventing a spawn remote so this code is effectively dead
	if(msg.spawn.i.subtype == _PROC_SPAWN_REMOTE) {
		spawn_remote_t *premote;
		unsigned len = (msg.spawn.i.nbytes + sizeof(int))&~(sizeof(int) - 1);

		if(!(dst = data = malloc(len + SPAWN_REMOTE_MSGBUF_SIZE))) {
			return = ENOMEM;
		}
		premote = (spawn_remote_t *)(data + len);
		SETIOV(iov + 0, &msg.spawn.i, sizeof msg.spawn.i);
		SETIOV(iov + 1, fd_map, fd_count * sizeof fd_map[0]);
		SETIOV(iov + 2, &msg.spawn.i, sizeof msg.spawn.i);
		SETIOV(iov + 3, premote, SPAWN_REMOTE_MSGBUF_SIZE);
		if(MsgSendvnc(coid, iov + 0, 2, iov + 2, 2) == -1) {
			free(data);
			return errno;
		}
		if((coid = ConnectAttach(premote->nd, premote->pid, premote->chid, _NTO_SIDE_CHANNEL, 0)) == -1) {
			free(data);
			return errno;
		}
		SETIOV(iov + 5, (char*)premote + sizeof(*premote), premote->size);
		msg.spawn.i.subtype = _PROC_SPAWN_START;
	} else */{
		if(!(dst = data = malloc(msg.spawn.i.nbytes))) {
			return ENOMEM;
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

	/* make a local copy of the attributes object */
	if (attrp)
	{
		msg.attr = *attrp;
		if ((msg.attr.flags & POSIX_SPAWN_SETMPART) && (msg.attr.u.__ext.mp_list.num_entries > 0))
		{
			unsigned i;
			/* convert partition names to mempart_id_t's */
			for (i=0; i<msg.attr.u.__ext.mp_list.num_entries; i++)
			{
				int  fd, status, r = EOK;
				
				/* make sure it is a name */
				if ((msg.attr.u.__ext.mp_list.i[i].flags & mempart_flags_USE_ID) == 0)
				{
					if (((fd = open((char *)msg.attr.u.__ext.mp_list.i[i].t.name, O_RDONLY)) == -1) ||
						((r = devctl(fd, MEMPART_GET_ID, &msg.attr.u.__ext.mp_list.i[i].t.id, sizeof(msg.attr.u.__ext.mp_list.i[i].t.id), &status)) != EOK))
					{
						if (fd != -1) close(fd);
						return (r == EOK) ? errno : r;
					}
					if (fd != -1) close(fd);
				}
			}
		}
	}
	else
		memset(&msg.attr, 0, sizeof(msg.attr));

	SETIOV(iov + 0, &msg, sizeof msg);
	SETIOV(iov + 1, fd_map, fd_count * sizeof fd_map[0]);
	SETIOV(iov + 2, search, msg.spawn.i.searchlen = (search ? strlen(search) + 1 : 0));
	SETIOV(iov + 3, path, msg.spawn.i.pathlen = strlen(path) + 1);
	SETIOV(iov + 4, data, msg.spawn.i.nbytes);

	*pid = MsgSendvnc(coid, iov + 0, 6, 0, 0);
/*
if (*pid == -1)
	printf("MsgSendvnc() failed with errno = %d\n", errno);
else
	printf("pid = 0x%x\n", *pid);
*/

	if(coid != PROCMGR_COID) {
		ConnectDetach(coid);
	}

	free(data);
	return (*pid == -1) ? errno : EOK;
}

#include "posix_spawn.h"

#ifdef _POSIX_SPAWN
int posix_spawn(pid_t *pid, const char *path, 
		const posix_spawn_file_actions_t *file_actions,
		const posix_spawnattr_t *attrp,
		char * const argv[],
		char * const envp[]) {
	return ENOSYS;
}
#endif

__SRCVERSION("posix_spawn.c $Rev: 156496 $");
