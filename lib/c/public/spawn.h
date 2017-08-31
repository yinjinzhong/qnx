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
 *  spawn.h
 *

 */
#ifndef _SPAWN_H_INCLUDED
#define _SPAWN_H_INCLUDED

#if defined(__WATCOMC__) && !defined(_ENABLE_AUTODEPEND)
 #pragma read_only_file;
#endif

#ifndef __TYPES_H_INCLUDED
 #include <sys/types.h>
#endif

#ifndef _SIGNAL_H_INCLUDED
 #include <signal.h>
#endif

#ifndef __PLATFORM_H_INCLUDED
#include <sys/platform.h>
#endif

#include <_pack64.h>

__BEGIN_DECLS

#ifndef _SCHED_H_INCLUDED
 #include <sched.h>
#endif

#if defined(__NYI)		/* Approved 1003.1d D14 */

#define POSIX_SPAWN_SETPGROUP		SPAWN_SETGROUP
#define POSIX_SPAWN_SETSIGMASK		SPAWN_SETSIGMASK
#define POSIX_SPAWN_SETSIGDEF		SPAWN_SETSIGDEF
#define POSIX_SPAWN_SETSCHEDULER	SPAWN_EXPLICIT_SCHED
#define POSIX_SPAWN_SETSCHEDPARAM	SPAWN_EXPLICIT_SCHED
#define POSIX_SPAWN_RESETIDS		0x0000

typedef struct _spawn_file_actions		posix_spawn_file_actions_t;
typedef struct _spawnattr				posix_spawnattr_t;

extern int posix_spawn(pid_t *__pid, const char *__path, 
		const posix_spawn_file_actions_t *__file_actions,
		const posix_spawnattr_t *__attrp,
		char * const __argv[], char * const __envp[]);
extern int posix_spawnp(pid_t *__pid, const char *__file, 
		const posix_spawn_file_actions_t *__file_actions,
		const posix_spawnattr_t *__attrp,
		char * const __argv[], char * const __envp[]);

extern int posix_spawnattr_init(posix_spawnattr_t *__attr);
extern int posix_spawnattr_destroy(posix_spawnattr_t *__attr);
extern int posix_spawnattr_getflags(posix_spawnattr_t *__attr, short *__flags);
extern int posix_spawnattr_setflags(posix_spawnattr_t *__attr, short __flags);
extern int posix_spawnattr_getpgroup(posix_spawnattr_t *__attr, pid_t *__pgroup);
extern int posix_spawnattr_setpgroup(posix_spawnattr_t *__attr, pid_t __pgroup);
extern int posix_spawnattr_getsigmask(posix_spawnattr_t *__attr, sigset_t *__sigmask);
extern int posix_spawnattr_setsigmask(posix_spawnattr_t *__attr, const sigset_t *__sigmask);
extern int posix_spawnattr_getdefault(posix_spawnattr_t *__attr, sigset_t *__sigdefault);
extern int posix_spawnattr_setdefault(posix_spawnattr_t *__attr, const sigset_t *__sigdefault);

extern int posix_spawn_file_actions_init(posix_spawn_file_actions_t *__file_actions);
extern int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *__file_actions);
extern int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *__file_actions, int __fd);
extern int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *__file_actions, int __fd, int __newfd);
extern int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *__file_actions, int __fd, const char *__path, int __oflag, mode_t __mode);
#endif

#if defined(__EXT_QNX)
typedef struct inheritance {
	unsigned long				flags;
	pid_t						pgroup;		/* SPAWN_SETGROUP must be set in flags */
	sigset_t					sigmask;	/* SPAWN_SETSIGMASK must be set in flags */
	sigset_t					sigdefault;	/* SPAWN_SETSIGDEF must be set in flags */
	sigset_t					sigignore;	/* SPAWN_SETSIGIGN must be set in flags */
	unsigned long				stack_max;	/* SPAWN_SETSTACKMAX must be set in flags */
#if __INT_BITS__ != 32
	long						policy;		/* SPAWN_EXPLICIT_SCHED must be set in flags */
#else
	int							policy;		/* SPAWN_EXPLICIT_SCHED must be set in flags */
#endif
	_Uint32t					nd;			/* SPAWN_SETND must be set in flags */
	_Uint32t					runmask;	/* SPAWN_EXPLICIT_CPU must be set in flags */
	struct sched_param			param;		/* SPAWN_EXPLICIT_SCHED must be set in flags */
} spawn_inheritance_type;

#define SPAWN_SETGROUP			0x00000001	/* set process group */
#define SPAWN_SETSIGMASK		0x00000002	/* set mask to sigmask */
#define SPAWN_SETSIGDEF			0x00000004	/* set members of sigdefault to SIG_DFL */
#define SPAWN_SETSIGIGN			0x00000008	/* set members of sigignore to SIG_IGN */
#define SPAWN_TCSETPGROUP		0x00000080	/* Start a new terminal group */
#define SPAWN_SETND				0x00000100	/* spawn to remote node */
#define SPAWN_SETSID			0x00000200	/* Make new process a session leader */
#define SPAWN_EXPLICIT_SCHED	0x00000400	/* Set the scheduling policy */
#define SPAWN_EXPLICIT_CPU		0x00000800	/* Set the CPU affinity/runmask */
#define SPAWN_SETSTACKMAX		0x00001000	/* Set the stack max */
#define SPAWN_NOZOMBIE			0x00002000	/* Process will not zombie on death  */
#define SPAWN_DEBUG				0x00004000	/* Debug process */
#define SPAWN_HOLD				0x00008000	/* Hold a process for Debug */
#define SPAWN_EXEC				0x00010000	/* Cause the spawn to act like exec() */
#define SPAWN_SEARCH_PATH		0x00020000	/* Search envar PATH for executable */
#define SPAWN_CHECK_SCRIPT		0x00040000	/* Allow starting a shell passing file as script */
#define SPAWN_ALIGN_MASK		0x03000000	/* Mask for align fault states below */
#define SPAWN_ALIGN_DEFAULT		0x00000000	/* Use system default settings for alignment */
#define SPAWN_ALIGN_FAULT		0x01000000	/* Try to always fault data misalignment references */
#define SPAWN_ALIGN_NOFAULT		0x02000000	/* Don't fault on misalignment, and attempt to fix it (may be slow) */

#define SPAWN_FDCLOSED			(-1)
#define SPAWN_NEWPGROUP			0

extern pid_t spawn(const char *__path, int __fd_count, const int __fd_map[],
		const struct inheritance *__inherit, char * const __argv[], char * const __envp[]);
extern pid_t spawnp(const char *__file, int __fd_count, const int __fd_map[],
		const struct inheritance *__inherit, char * const __argv[], char * const __envp[]);
#endif

#include <_packpop.h>

__END_DECLS

#endif

/* __SRCVERSION("spawn.h $Rev: 153052 $"); */
