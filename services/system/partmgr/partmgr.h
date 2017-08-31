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

#ifndef _include_PARTMGR_H_
#define _include_PARTMGR_H_

#include <fcntl.h>
#include <sys/dcmd_all.h>
#include <dirent.h>
#include "externs.h"

#include "mempartmgr.h"
#include "schedpartmgr.h"



/*==============================================================================
 * 
 * 				internal partition manager module types
 * 
*/

/*
 * These structure are no loner required as this resource manager
 * has been modified to eliminate the need for an iofunc_attr_t
 * structure on the /proc/<pid>. The only saved information was the
 * 'pid_t' (from /proc/<pid>) and that is no stored in the ocb.
 * I leave the definitions here for reference ... just in case
typedef struct
{
	iofunc_attr_t	attr;
	pid_t			pid;
} pid_attr_t;

typedef struct pid_attr_entry_s
{
	mpart_qnode_t		hdr;
	pid_attr_t			pid_attr;
} pid_attr_entry_t;
*/

typedef enum
{
partmgr_type_first = 10,

	partmgr_type_NONE,		// <pid>/
	partmgr_type_PART,		// <pid>/partition/
	partmgr_type_MEM,		// <pid>/partition/mem/..
	partmgr_type_SCHED,		// <pid>/partition/sched/..

partmgr_type_space,
partmgr_type_last = partmgr_type_space-1,
} partmgr_type_t;

typedef struct
{
	iofunc_ocb_t	ocb;
	partmgr_type_t	partmgr_type;
	pid_t			pid;
	union {
		mempartmgr_attr_t		*mem;
		void/*FIX ME schedpartmgr_attr_t*/		*sched;
	} attr;
} partmgr_ocb_t;

extern int partmgr_open(resmgr_context_t *ctp, io_open_t *msg, void *extra, void *reserved);
extern int partmgr_close(resmgr_context_t *ctp, void *reserved, void *_ocb);
extern int partmgr_unlink(resmgr_context_t *ctp, io_unlink_t *msg, void *handle, void *reserved);
extern int partmgr_stat(resmgr_context_t *ctp, io_stat_t *msg, void *ocb);
extern int partmgr_devctl(resmgr_context_t *ctp, io_devctl_t *msg, void *ocb);
extern int partmgr_read(resmgr_context_t *ctp, io_read_t *msg, void *_ocb);
extern int partmgr_chown(resmgr_context_t *ctp, io_chown_t *msg, void *_ocb);
extern int partmgr_chmod(resmgr_context_t *ctp, io_chmod_t *msg, void *_ocb);
extern int partmgr_lseek(resmgr_context_t *ctp, io_lseek_t *msg, void *_ocb);
extern int partmgr_lock(resmgr_context_t *ctp, io_lock_t *msg, void *_ocb);
extern int partmgr_unblock(resmgr_context_t *ctp, io_pulse_t *msg, void *_ocb);
extern int partmgr_openfd(resmgr_context_t *ctp, io_openfd_t *msg, void *_ocb);
extern int partmgr_fdinfo(resmgr_context_t *ctp, io_fdinfo_t *msg, void *_ocb);

extern bool isProcessPath(char *path, pid_t *pid_p);
//extern pid_attr_entry_t *find_pidentry(pid_t pid);
extern bool nametoolong(const char *name, unsigned chktype, void *partmgrtype);

extern dev_t  partmgr_devno;
extern const resmgr_io_funcs_t partmgr_io_funcs;
extern mpart_qnodehdr_t		pid_entry_list;
extern iofunc_attr_t 	dummy_partmgr_attr;

/*==============================================================================
 * 
 * 			interfaces to the memory partition resource manager module
 * 
*/
// FIX ME - function tables ?
extern void mempartmgr_init(char *);
extern int mpmgr_read(PROCESS *prp, mempartmgr_attr_t *attr, off_t *offset,
							void *buf, size_t *size);
extern mempartmgr_attr_t *mpmgr_getattr(char *path);
extern int mpmgr_get_st_size(mempartmgr_attr_t *attr, struct stat *st);

/*******************************************************************************
 * MEMPARTMGR_INIT
 * 
*/
#define MEMPARTMGR_INIT(path) \
		mempartmgr_init((path))

/*******************************************************************************
 * MEMPARTMGR_GETATTR
 * 
 * Given <path> (which must be below the "/partition/mem" namespace) return
 * the corresponding 'mempart_attr_t' structure for the last entry or NULL
 * if the path does not exist
*/
#define MEMPARTMGR_GETATTR(path)	mpmgr_getattr((path))
		
/*******************************************************************************
 * MEMPARTMGR_READ
 * 
 * Call the memory partitioning module to perform a read operation at offset <o>
 * for the entry identified by attribute structure <a>. The read should be
 * filtered based on PROCESS <p> and the contents read placed into buffer <d>.
 * The size of buffer <d> is pointed to by <n>.
 * 
 * Returns: EOK or an errno. If EOK is returned, <n> will point to the number of
 * 			bytes placed into <d> (which will never be greater than the original
 * 			value pointed to by <n>)
*/
#define MEMPARTMGR_READ(p, a, o, d, n)	mpmgr_read((p), (a), (o), (d), (n))
		

/*==============================================================================
 * 
 * 			interfaces to the scheduling partition resource manager module
 * 
*/
// FIX ME - function tables ?
extern void schedpartmgr_init(char *);

/*******************************************************************************
 * SCHEDPARTMGR_INIT
 * 
*/
#define SCHEDPARTMGR_INIT(path) \
		schedpartmgr_init((path))


/*******************************************************************************
 * SCHEDPARTMGR_GETATTR
 * 
 * Given <path> (which must be below the "/partition/sched" namespace) return
 * the corresponding 'schedpart_attr_t' structure for the last entry
*/
#define SCHEDPARTMGR_GETATTR(path)	NULL	// FIX ME
		
/*******************************************************************************
 * SCHEDPARTMGR_READ
 * 
 * Call the scheduling partitioning module to perform a read operation at offset
 * <o> for the entry identified by attribute structure <a>. The read should be
 * filtered based on PROCESS <p> and the contents read placed into buffer <d>.
 * The size of buffer <d> is pointed to by <n>.
 * 
 * Returns: EOK or an errno. If EOK is returned, <n> will point to the number of
 * 			bytes placed into <d> (which will never be greater than the original
 * 			value pointed to by <n>)
*/
#define SCHEDPARTMGR_READ(p, a, o, d, n)	EOK//_schedpartmgr_read((p), (a), (o), (d), (n))
		


#endif	/* _include_PARTMGR_H_ */

__SRCVERSION("$IQ: partmgr.h,v 1.23 $");

