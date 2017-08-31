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

#ifndef _include_MEMPARTMGR_H_
#define _include_MEMPARTMGR_H_

#include "externs.h"

#include "apm.h"
#include "../memmgr/apm/apm_internal.h"

#include <fcntl.h>
#include <sys/dcmd_all.h>
#include <dirent.h>
#include <sys/iofunc.h>



extern mempart_rsrcmgr_fnctbl_t  *mempart_rsrcmgr_fnctbl;

/*==============================================================================
 * 
 * 				interfaces to the memory partition module
 * 
*/

/*******************************************************************************
 * MEMPART_CREATE
 * 
*/
#define MEMPART_CREATE(p, c, mc) \
		((mempart_rsrcmgr_fnctbl == NULL) ? mempart_id_t_INVALID : mempart_rsrcmgr_fnctbl->create((p), (c), (mc)))

/*******************************************************************************
 * MEMPART_DESTROY
 * 
*/
#define MEMPART_DESTROY(p) \
		((mempart_rsrcmgr_fnctbl == NULL) ? ENOSYS : mempart_rsrcmgr_fnctbl->destroy((p)))

/*******************************************************************************
 * MEMPART_CHANGE
 * 
*/
#define MEMPART_CHANGE(p, c) \
		((mempart_rsrcmgr_fnctbl == NULL) ? ENOSYS : mempart_rsrcmgr_fnctbl->config((p), (c)))

/*******************************************************************************
 * MEMPART_GETINFO
 * 
*/
#define MEMPART_GETINFO(p, i) \
		((mempart_rsrcmgr_fnctbl == NULL) ? NULL : mempart_rsrcmgr_fnctbl->getinfo((p), (i)))

/*******************************************************************************
 * MEMPART_FINDPID
 * 
*/
#define MEMPART_FINDPID(p, mp) \
		((mempart_rsrcmgr_fnctbl == NULL) ? NULL : mempart_rsrcmgr_fnctbl->find_pid((p), (mp)))

/*******************************************************************************
 * VALIDATE_CFG_CREATION
 * VALIDATE_CFG_MODIFICATION
 * 
 * These 2 routines are used to validate configurations for partition creation
 * or modification based on the policies in use. They provide common routines
 * for validation so that these types of checks are not littered throughout the
 * code.
 *
 * VALIDATE_CFG_CREATION() requires a mempart_t * for the parent partition
 * under which the new partition, described by <cfg> is proposed to be created.
 * The parent partition <pmp> may be NULL if this is a root partition.
 * The configuration <cfg> can also be NULL however nothing useful will be done
 * unless a parent partition pointer is provided
 * 
 * VALIDATE_CFG_MODIFICATION requires a mempart_t * for the partition to which
 * the modifications specified by <cfg> apply. The partition <mp> can not be NULL
 * 
 * Both routines will return EOK if <cfg> is valid for the requested operation
 * and policies in place, otherwise and errno is returned. 
*/
#define VALIDATE_CFG_CREATION(pmp, cfg, mc) \
		((mempart_rsrcmgr_fnctbl == NULL) ? ENOSYS : mempart_rsrcmgr_fnctbl->validate_cfg_new((pmp), (cfg), (mc)))

#define VALIDATE_CFG_MODIFICATION(mp, cfg) \
		((mempart_rsrcmgr_fnctbl == NULL) ? ENOSYS : mempart_rsrcmgr_fnctbl->validate_cfg_change((mp), (cfg)))



/*==============================================================================
 * 
 * 				internal memory partition manager module types
 * 
*/
typedef enum
{
	mpart_type_ROOT,
	mpart_type_MEMPART_real,	// a real partition
	mpart_type_MEMPART_pseudo,	// a pseudo partition (symlink to a real partition)
	mpart_type_MEMPART_group,	// a directory containing pseudo partitions only
	mpart_type_MEMCLASS,

} mpart_type_t;

typedef struct
{
	mpart_qnode_t		hdr;
	union {
		struct mp_evt_info_s	mp;	// the event that was registered for
		struct mc_evt_info_s	mc;
	} evt_reg;
	union {
		memsize_t	size;			// used internally for delta from value
	} evt_data;						// used internally for event specific storage
	struct evtdest_s {
		int					rcvid;		// who to send the event to
		PROCESS *			prp;		// the process wishing to receive the events
	} evt_dest;
	void *				ocb;		// the ocb that that attached the event
} mempart_evt_t;

/*
typedef struct
{
	mpart_qnodehdr_t	hdr;
	mempart_evt_t		evt;
} mempart_evtlist_t;*/
typedef mpart_qnodehdr_t	mempart_evtlist_t;

typedef struct mempartmgr_attr_s
{
	mpart_qnode_t			hdr;	// sibling list
	iofunc_attr_t			attr;
	mpart_type_t			type;
	const char * 			name;

	struct mempartmgr_attr_s *	parent;
	mpart_qnodehdr_t		children;

	mempart_id_t			mpid;
	
	mempart_evtlist_t		event_list[max(NUM_MEMPART_EVTTYPES, NUM_MEMCLASS_EVTTYPES)];	

} mempartmgr_attr_t;

/*
 * GET_MEMPART_ATTR
 * 
 * macro to extract a pointer to the relevant 'mempartmgr_attr_t' from an ocb
*/
#define GET_MEMPART_ATTR(_ocb_) \
		((mempartmgr_attr_t *)((unsigned int)(((iofunc_ocb_t *)(_ocb_))->attr) - offsetof(mempartmgr_attr_t, attr)))

/*
 * Attribute locking/unlocking macros
*/
#define MEMPART_ATTR_LOCK(a)	(((a) == NULL) ? EOK : iofunc_attr_lock(&((a)->attr)))
#define MEMPART_ATTR_UNLOCK(a)	if ((a) != NULL) iofunc_attr_unlock(&((a)->attr))

extern mempartmgr_attr_t  *root_mpart;
extern const resmgr_io_funcs_t mempartmgr_io_funcs;
extern dev_t  mempartmgr_devno;

/* memory partitioning resource manager connect and I/O functions */
extern int mempartmgr_stat(resmgr_context_t *ctp, io_stat_t *msg, RESMGR_OCB_T *ocb);
extern int mempartmgr_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *ocb);
extern int mempartmgr_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *reserved);
extern int mempartmgr_close(resmgr_context_t *ctp, void *reserved, RESMGR_OCB_T *_ocb);
extern int mempartmgr_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_unlink(resmgr_context_t *ctp, io_unlink_t *msg, RESMGR_HANDLE_T *handle, void *reserved);
extern int mempartmgr_link(resmgr_context_t *ctp, io_link_t *msg, RESMGR_HANDLE_T *handle, io_link_extra_t *extra);
extern int mempartmgr_readlink(resmgr_context_t *ctp, io_readlink_t *msg, RESMGR_HANDLE_T *handle, void *reserved);
extern int mempartmgr_mknod(resmgr_context_t *ctp, io_mknod_t *msg, RESMGR_HANDLE_T *handle, void *reserved);
extern int mempartmgr_chown(resmgr_context_t *ctp, io_chown_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_chmod(resmgr_context_t *ctp, io_chmod_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_lseek(resmgr_context_t *ctp, io_lseek_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_lock(resmgr_context_t *ctp, io_lock_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_unblock(resmgr_context_t *ctp, io_pulse_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_openfd(resmgr_context_t *ctp, io_openfd_t *msg, RESMGR_OCB_T *_ocb);
extern int mempartmgr_fdinfo(resmgr_context_t *ctp, io_fdinfo_t *msg, RESMGR_OCB_T *_ocb);

/* memory partitioning resource manager externally accessible functions */
extern int validate_association(mempartmgr_attr_t *attr, struct _cred_info *cred);
extern void mempartmgr_event_trigger(mempartmgr_attr_t *attr, mempart_evttype_t evtype, ...);
void mempartmgr_event_trigger2(mempartmgr_attr_t *attr, memclass_evttype_t evtype,
								memclass_sizeinfo_t *size_info,	memclass_sizeinfo_t *prev_size_info);

/* memory partitioning resource manager internal support routines */
extern mempartmgr_attr_t *path_find(mempartmgr_attr_t *root, char *path, mempartmgr_attr_t **parent);
extern bool isStringOfDigits(char *part_name);
extern mempartmgr_attr_t *find_pid_in_mpart_hierarchy(pid_t pid, mempartmgr_attr_t *mp);
extern int check_access_perms(resmgr_context_t *ctp, mempartmgr_attr_t *attr, mode_t check,
								struct _client_info *info, bool recurse);
extern int unregister_events(iofunc_ocb_t *ocb, int num_evts);
extern int register_mclass_event(struct mc_evt_info_s *evt, struct evtdest_s *evtdest, void *ocb);
extern int register_mpart_event(struct mp_evt_info_s *evt, struct evtdest_s *evtdest, void *ocb);

#endif	/* _include_MEMPARTMGR_H_ */

__SRCVERSION("$IQ: mempartmgr.h,v 1.23 $");

