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

#include "externs.h"
#include "procmgr_internal.h"
#include "apm.h"

int procmgr_fork(resmgr_context_t *ctp, proc_fork_t *msg) {
	struct loader_context				*lcp;
	struct _thread_attr					attr;
	PROCESS								*prp;
	proc_create_attr_t					extra = {NULL};
	mempart_list_t			*mempart_list;
#define MEMPART_MAX_PARTLIST_ENTRIES	16	// FIX ME - 16 mempart_id_t's
	union {
		mempart_list_t					mpl;
		char		space[MEMPART_LIST_T_SIZE(MEMPART_MAX_PARTLIST_ENTRIES)];											
	} _mempart_list;

	if(!(lcp = procmgr_context_alloc(0, LC_FORK))) {
		return ENOMEM;
	}

	{
		unsigned i;
		PROCESS *pprp = proc_lock_pid(ctp->info.pid);
		mempart_list = &_mempart_list.mpl;

		KASSERT(pprp != NULL);

#if defined(MEMPART_DFLT_INHERIT_BEHVR_1)
		{
		mempart_flags_t getlist_flags = mempart_flags_t_GETLIST_INHERITABLE | mempart_flags_t_GETLIST_CREDENTIALS;
		struct _cred_info cred = pprp->cred->info;

	#ifndef NDEBUG
		/* all of the parents partitions (excluding NO_INHERIT ones) will be inherited */
		int  n = MEMPART_GETLIST(pprp, mempart_list, MEMPART_MAX_PARTLIST_ENTRIES, getlist_flags, &cred);
		if (n != 0) {kprintf("getlist ret %d (%d)\n", n, MEMPART_MAX_PARTLIST_ENTRIES); crash();}
	#else	/* NDEBUG */
			MEMPART_GETLIST(pprp, mempart_list, MEMPART_MAX_PARTLIST_ENTRIES, getlist_flags, &cred);
	#endif	/* NDEBUG */
		}
#elif defined(MEMPART_DFLT_INHERIT_BEHVR_2)
		/* only the sysram partition of the parent will be inherited */
		mempart_list->num_entries = 1;
		mempart_list->i[0].t.id = mempart_getid(pprp, sys_memclass_id);
		mempart_list->i[0].flags = default_mempart_flags;
#else
	#error
	#error no default partition inheritance defined
	#error
#endif	/* MEMPART_DFLT_INHERIT_BEHVR_x */

		/* make sure a sysram partition is inheritable */
		for (i=0; i<mempart_list->num_entries; i++)
			if (mempart_get_classid(mempart_list->i[i].t.id) == sys_memclass_id)
				break;
		if (i >= mempart_list->num_entries)
		{
			proc_unlock(pprp);
			procmgr_context_free(lcp);
			return EACCES;	// no permission for parents sysram partition
		}

		/* 
		 * By default, the mempart_list->i[].flags are inherited from the
		 * parent. However, in the case of the the initial processes created
		 * by procnto, we do not want this inheritance. Instead we want to use
		 * the 'default_mempart_flags'. These flags will apply only to the
		 * partition of the system memory class since that is (currently) the
		 * only partition procnto is associated with.
		*/
		if (pprp == procnto_prp)
		{
			unsigned i;
			for (i=0; i<mempart_list->num_entries; i++)
				mempart_list->i[i].flags = default_mempart_flags;
		}
		proc_unlock(pprp);
	}

	/*
	 * force the first partition entry to be for the sysram memory class. This
	 * is done first of all because kerext_process_create() requires a sysram
	 * memory class partition in order to perform the first association and
	 * secondly it ensures the fastest possible access to the sysram partition
	 * for a process (ie. first list entry).
	 * The only time that this assertion should go off is when an explicit
	 * partition list is provided by the user which does not have a sysram
	 * partition as the first entry and list reordering has not occured. 
	*/ 
	KASSERT(mempart_get_classid(mempart_list->i[0].t.id) == sys_memclass_id);

	lcp->flags = msg->i.flags;
	lcp->ppid = ctp->info.pid;
	lcp->rcvid = ctp->rcvid;
	memcpy(&lcp->msg, msg, min(sizeof lcp->msg, sizeof *msg));
	SignalProcmask(lcp->ppid, ctp->info.tid, 0, 0, &lcp->mask);

#ifndef NDEBUG
	if (MEMPART_INSTALLED() && (mempart_list == NULL))
	{
		kprintf("ins: %d\n", MEMPART_INSTALLED());
		crash();
	}
#endif	/* NDEBUG */
	
	extra.mpart_list = mempart_list;
	proc_wlock_adp(sysmgr_prp);
	lcp->process = ProcessCreate(lcp->ppid, lcp, &extra);
	proc_unlock_adp(sysmgr_prp);
	if(lcp->process == (void *)-1) {
		procmgr_context_free(lcp);
		return errno;
	}

	if((prp = proc_lock_pid(lcp->ppid))) {
		pathmgr_node_access(prp->root);
		lcp->process->root = pathmgr_node_clone(prp->root);
		pathmgr_node_complete(prp->root);

		pathmgr_node_access(prp->cwd);
		lcp->process->cwd = pathmgr_node_clone(prp->cwd);
		pathmgr_node_complete(prp->cwd);

		proc_unlock(prp);
	}

	procmgr_thread_attr(&attr, lcp, ctp);

	if(ThreadCreate(lcp->process->pid, loader_fork, lcp, &attr) == -1) {
		MsgSendPulse(PROCMGR_COID, attr.__param.__sched_priority, PROC_CODE_TERM, lcp->process->pid);
		return ENOMEM;
	}
	return _RESMGR_NOREPLY;
}

__SRCVERSION("procmgr_fork.c $Rev: 156323 $");
