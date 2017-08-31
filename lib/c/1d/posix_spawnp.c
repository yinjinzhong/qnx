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




#include <string.h>
#include "posix_spawn.h"

#ifdef _POSIX_SPAWN
int posix_spawnp(pid_t *pid, const char *file, 
		const posix_spawn_file_actions_t *file_actions,
		const posix_spawnattr_t *attrp,
		char * const argv[],
		char * const envp[]) {
	posix_spawnattr_t			attr;

	// The library knows that posix_spawnattr_destroy is not needed.
	if(attrp) {
		attr = *attrp;
	} else {
		memset(&attr, 0x00, sizeof attr);
	}

	if(!strchr(file, '/')) {
		attr.flags |= SPAWN_SEARCH_PATH;
	}
	attr.flags |= SPAWN_CHECK_SCRIPT;

	return posix_spawn(pid, file, file_actions, &attr, argv, envp);
}
#endif

__SRCVERSION("posix_spawnp.c $Rev: 153052 $");
