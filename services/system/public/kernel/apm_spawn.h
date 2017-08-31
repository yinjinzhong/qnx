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



/*
 *  apm_spawn.h
 *
 * 						W A R N I N G
 * 
 * Stay away from this file. It is a hack for the sole purpose of enabling
 * memory partition association via the 'apm' utility. It won't be around
 * so don't included it. I fact, do yourself a favour and don't even read it,
 * it's awful 
 *
 */
#ifndef _APM_SPAWN_H_INCLUDED
#define _APM_SPAWN_H_INCLUDED

#include <spawn.h>
//FIX ME #include <sys/mempart.h>
#include "kernel/mempart.h"

typedef struct
{
	short int	flags;
	pid_t		pgrp;
	sigset_t	sd;
	sigset_t	ss;
	struct sched_param	sp;
	int		sched_policy;

	/* the following are QNX extensions to 'posix_spawnattr_t' */
	union {
		struct {
			unsigned long	flags2;
			mempart_list_t	mp_list;
		} __ext;			
		int __pad[16];
	} u;
} posix_spawnattr_t;

/* the following will be removed */
struct posix_spawn_file_actions_s;
extern int apm_posix_spawn(pid_t *__pid, const char *__path, 
		const struct posix_spawn_file_actions_s *__file_actions,
		const posix_spawnattr_t *__attrp,
		char * const __argv[], char * const __envp[]);


#define SPAWN_SETMEMPART		0x00000010	/* associate process with a set of memory partitions */
#define POSIX_SPAWN_SETMPART	SPAWN_SETMEMPART

#endif	/* _APM_SPAWN_H_INCLUDED */

/* __SRCVERSION("apm_spawn.h $Rev: 152112 $"); */
