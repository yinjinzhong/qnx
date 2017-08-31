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

/*==============================================================================
 * 
 * mempartmgr_devctl
 * 
 * Provide resource manager devctl() processing for the memory partitioning
 * module
 * 
*/

#include "mempartmgr.h"

typedef char	mempart_path_t[PATH_MAX+1];
static int build_path(mempartmgr_attr_t *mp, mempart_path_t path, unsigned pos);

int mempartmgr_devctl(resmgr_context_t *ctp, io_devctl_t *msg, RESMGR_OCB_T *_ocb)
{
	iofunc_ocb_t *ocb = (iofunc_ocb_t *)_ocb;
	void	*data = (void *)(msg + 1);
    int		nbytes = 0;

    switch(msg->i.dcmd)
    {
    	// FIX ME - these SET/GET flags should be handled by iofunc_devctl_default()
    	//			but leave it for now until I am sure that the mempart_dcmd_flags_t
    	//			can be set in them as well
    	case DCMD_ALL_GETFLAGS:
    	{
			if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->o.nbytes < sizeof(ocb->ioflag))
				return EOVERFLOW;
			else
			{
				int  ioflag = ocb ? ((iofunc_ocb_t *)ocb)->ioflag : 0;
				*((int *)data) = (ioflag & ~O_ACCMODE) | ((ioflag - 1) & O_ACCMODE);
				nbytes = sizeof(ioflag);
				break;
			}
    	}
		case DCMD_ALL_SETFLAGS:
/* FIX ME - appears not to be being obeyed .. should it ?
			if ((ocb->ioflag & _IO_FLAG_WR) == 0)
				return EBADF;
*/			
			if (msg->i.nbytes < sizeof(ocb->ioflag))
				return EINVAL;
			if(ocb)
				ocb->ioflag = (ocb->ioflag & ~O_SETFLAG) | (*((int *)data) & O_SETFLAG);
			break;

    	case MEMPART_GET_INFO:
    	{
    		mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
    		mempart_info_t *info_out = (mempart_info_t *)data;
			int  r = EOK;

			if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->o.nbytes < sizeof(*info_out))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;

			memset(info_out, 0, sizeof(*info_out));

			/* can only be called on 'real' partitions */
			if (mp->type != mpart_type_MEMPART_real)
				r = EBADF;
			else if (MEMPART_GETINFO(mp->mpid, info_out) == NULL)
				r = EIO;
			else
			{
				info_out->id = mp->mpid;
				nbytes = sizeof(*info_out);
			}

			MEMPART_ATTR_UNLOCK(mp);
			if (r != EOK)
				return r;
    		break;
    	}

		case MEMPART_CFG_CHG:
		{
			int  r = EOK;
			mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
			mempart_cfgchg_t  *cfg = (mempart_cfgchg_t *)data;

			if ((ocb->ioflag & _IO_FLAG_WR) == 0)
				return EBADF;
			else if (msg->i.nbytes < sizeof(*cfg))
				return EINVAL;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;

			/* follow link (ie resolve pseudo partition) */
/* FIX ME - we don't do this for the caller because it presents complications when
 * 			trying to resolve a memory class for example. It is better that the
 * 			caller knows right away that they are dealing with a pseudo partition
 * 			and take actions to resolve the issue themselves. This way they are
 * 			fully aware of the symlinks. This can be done by issuing a stat() on
 * 			the partition name and determing whether it is a symlink or a directory
 * 			however I would like a better mechanism since a directory does not
 * 			uniquely identifiy a partition either since a pid directory can reside
 * 			under a partition name alongside a child partition. It would be nice
 * 			to have a unique 'identification'. Perhaps if mempart_getid() returned
 * 			an identifier in the low 16 bits (vector index ?) and used the MSb to
 * 			mark a pseudo partition then the following unique interpretations
 * 			of devctl(mempart_getid()) could be made
 * 				fail - not a partition, must be a pid since thats the only other
 * 						possible entry
 * 				success and (mempart_id_t & 0x80000000) = 1 then a pseudo partition,
 * 						use readlink to resolve to a real partition
 * 				success and (mempart_id_t & 0x80000000) = 0 then a real partition
 * 
			if (mp->type == mpart_type_MEMPART_pseudo)
				mp = (mempartmgr_attr_t *)mp->mpart;
*/
			/* can only be called on 'real' partitions */
			if (mp->type != mpart_type_MEMPART_real)
				r = EBADF;
			else if (mp->mpid == mempart_id_t_INVALID)
				r = EIO;
			/* caller did not provide info on what to change */
			else if (cfg->valid == cfgchg_t_NONE)
				r = EINVAL;

			if (r != EOK)
			{
				MEMPART_ATTR_UNLOCK(mp);
				return r;
			}

			/*
			 * external representation of 'memsize_t' can be larger than the
			 * internal representation of 'memsize_t' so ckeck for overflows
			 * on incoming size attributes
			*/
#ifdef _MEMSIZE_and_memsize_are_different_
			if (MEMSIZE_OFLOW(cfg->val.attr.size.min) ||
				MEMSIZE_OFLOW(cfg->val.attr.size.max))
			{
#ifndef NDEBUG
				if (ker_verbose) {
					kprintf("partition size attribute %d bit overflow\n",
							sizeof(_MEMSIZE_T_) * 8);
				}
#endif
				MEMPART_ATTR_UNLOCK(mp);
				return EINVAL;
			}
#endif	/* _MEMSIZE_and_memsize_are_different_ */
			
			/*
			 * Unless we are changing everything, get the current configuration
			 * so that the changes can be merged
			*/
			if (!(cfg->valid == cfgchg_t_ALL))
			{
				mempart_t *mpart = MEMPART_ID_TO_T(mp->mpid);
				mempart_cfg_t  *cur_cfg = &mpart->info.cur_cfg;

				/* if not changing the alloc policy, use the current */
				if (!(cfg->valid & cfgchg_t_ALLOC_POLICY))
					cfg->val.policy.alloc = cur_cfg->policy.alloc;
				
				/* if not changing the terminal partition policy, use the current */
				if (!(cfg->valid & cfgchg_t_LOCK_POLICY))
					cfg->val.policy.config_lock = GET_MPART_POLICY_CFG_LOCK(cur_cfg);
				
				/* if not changing the terminal partition policy, use the current */
				if (!(cfg->valid & cfgchg_t_TERMINAL_POLICY))
					cfg->val.policy.terminal = GET_MPART_POLICY_TERMINAL(cur_cfg);

				/* if not changing the permanent partition policy, use the current */
				if (!(cfg->valid & cfgchg_t_PERMANENT_POLICY))
					cfg->val.policy.permanent = GET_MPART_POLICY_PERMANENT(cur_cfg);
				
				/* if not changing the maximum size attribute, use the current */
				if (!(cfg->valid & cfgchg_t_ATTR_MIN))
					cfg->val.attr.size.max = cur_cfg->attr.size.max;				

				/* if not changing the minimum size attribute, use the current */
				if (!(cfg->valid & cfgchg_t_ATTR_MIN))
					cfg->val.attr.size.min = cur_cfg->attr.size.min;				
			}

			/* sanity check the parameters */
			if ((r = VALIDATE_CFG_MODIFICATION(mp->mpid, &cfg->val)) != EOK)
			{
#ifndef NDEBUG
				if (ker_verbose > 1) {
					kprintf("Config Modification validation err %d\n", r);
				}
#endif	/* NDEBUG */
				MEMPART_ATTR_UNLOCK(mp);
				return r;
			}

			/* should be good to change */
			if ((r = MEMPART_CHANGE(mp->mpid, &cfg->val)) != EOK)
			{
				MEMPART_ATTR_UNLOCK(mp);
				return r;
			}
			
			MEMPART_ATTR_UNLOCK(mp);
			break;
		}

    	case MEMPART_GET_ID:
    	{
    		int r;
    		mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);

			if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->o.nbytes < sizeof(mempart_id_t))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;

			/*
			 * for now, give back the mempart_t * as the mempart_id_t. It a
			 * different value is used in the future, will need to need to
			 * account for this in the procmgr_spawn()/ProcessCreate() code
			 * as it is currently assumed to be a mempart_t * there as well.
			*/
			*((mempart_id_t *)data) = mp->mpid;
			nbytes = sizeof(mempart_id_t);
			MEMPART_ATTR_UNLOCK(mp);
			break;
    	}
    	
#ifdef USE_PROC_OBJ_LISTS
    	case MEMPART_GET_ASSOC_PLIST:
		{
			mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
			mempart_t  *mpart;
    		mempart_plist_t *plist_out = (mempart_plist_t *)data;
			prp_node_t  *prp_list;
			unsigned int  num_pids = 0;
			unsigned int  num_entries;
			int  r = EOK;

			if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (plist_out->num_entries < 0)
				return EINVAL;
			else if (msg->o.nbytes < MEMPART_PLIST_T_SIZE(plist_out->num_entries))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;

			/* can only be called on real partitions */
			if (mp->type != mpart_type_MEMPART_real)
			{
				MEMPART_ATTR_UNLOCK(mp);
				return EBADF;
			}

			mpart = MEMPART_ID_TO_T(mp->mpid);
			prp_list = (prp_node_t *)LIST_FIRST(mpart->prp_list);
			num_pids = LIST_COUNT(mpart->prp_list);
			num_entries = min(num_pids, plist_out->num_entries);

			if (num_entries > 0)
			{
				unsigned i;
				for (i=0; i<num_entries; i++)
				{
					plist_out->pid[i] = prp_list->prp->pid;
					prp_list = (prp_node_t *)LIST_NEXT(prp_list);
				}
			}
			if (num_entries < num_pids)
				plist_out->num_entries = -(num_pids - num_entries);
			else
				plist_out->num_entries = num_pids;

			nbytes = MEMPART_PLIST_T_SIZE(num_entries);
			MEMPART_ATTR_UNLOCK(mp);
			break;
		}
#endif	/* USE_PROC_OBJ_LISTS */

		case MEMCLASS_GET_INFO:
		{
    		mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
    		memclass_entry_t *memclass = NULL;
    		memclass_info_t *info_out = (memclass_info_t *)data;
			int  r = EOK;

			if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->o.nbytes < sizeof(*info_out))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;

			/* can only be called on 'class' partitions */
			if (mp->type != mpart_type_MEMCLASS)
				r = EBADF;
			else if ((memclass = (memclass_entry_t *)mp->mpid) == NULL)
				r = ENOENT;
			else if ((memclass->data.allocator.size_info == NULL) ||
					 (memclass->data.allocator.size_info(&info_out->size, &memclass->data.info) == NULL))
				r = EIO;
			else
			{		
				/* fill in remainder of the memclass_info_t */
				info_out->id = memclass->data.info.id;
				info_out->attr = memclass->data.info.attr;

				nbytes = sizeof(*info_out);
			}
			MEMPART_ATTR_UNLOCK(mp);
			if (r != EOK)
				return r;
			break;
		}

		case MEMPART_EVT_REG:
		{
			mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
    		mempart_evtreg_t *er = (mempart_evtreg_t *)data;
    		PROCESS *prp = proc_lookup_pid(ctp->info.pid);
			int  r = EOK;
			int  i;

			KASSERT(mp != NULL);
			KASSERT(er != NULL);

			if (mp->type != mpart_type_MEMPART_real)
				return EBADF;
			else if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->i.nbytes < MEMPART_EVTREG_T_SIZE(er->num_entries))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;
			
			for (i=0; i<er->num_entries; i++)
			{
				int r = EINVAL;
				struct evtdest_s evt_dest;
				
				evt_dest.prp = prp;
				evt_dest.rcvid = ctp->rcvid;
				if ((er->evt[i].info.type >= mempart_evttype_t_first) &&
					(er->evt[i].info.type <= mempart_evttype_t_last))
					r = register_mpart_event(&er->evt[i], &evt_dest, ocb);

				/* currently event registration is an all or nothing proposition */
				if (r != EOK)
				{
					unregister_events(ocb, NUM_MEMPART_EVTTYPES);
					MEMPART_ATTR_UNLOCK(mp);
					return r;
				}
			}
			MEMPART_ATTR_UNLOCK(mp);
			break;
		}

		case MEMCLASS_EVT_REG:
		{
			mempartmgr_attr_t  *mp = GET_MEMPART_ATTR(ocb);
    		memclass_evtreg_t *er = (memclass_evtreg_t *)data;
    		PROCESS *prp = proc_lookup_pid(ctp->info.pid);
			int  r = EOK;
			int  i;

			KASSERT(mp != NULL);
			KASSERT(er != NULL);

			if (mp->type != mpart_type_MEMCLASS)
				return EBADF;
			else if ((ocb->ioflag & _IO_FLAG_RD) == 0)
				return EBADF;
			else if (msg->i.nbytes < MEMCLASS_EVTREG_T_SIZE(er->num_entries))
				return EOVERFLOW;

			if ((r = MEMPART_ATTR_LOCK(mp)) != EOK)
				return r;
			
			for (i=0; i<er->num_entries; i++)
			{
				int r = EINVAL;
				struct evtdest_s evt_dest;
				
				evt_dest.prp = prp;
				evt_dest.rcvid = ctp->rcvid;
				if ((er->evt[i].info.type >= memclass_evttype_t_first) &&
					(er->evt[i].info.type <= memclass_evttype_t_last))
					r = register_mclass_event(&er->evt[i], &evt_dest, ocb);

				/* currently event registration is an all or nothing proposition */
				if (r != EOK)
				{
					unregister_events(ocb, NUM_MEMCLASS_EVTTYPES);
					MEMPART_ATTR_UNLOCK(mp);
					return r;
				}
			}
			MEMPART_ATTR_UNLOCK(mp);
			break;
		}
    	default:
        	return ENOSYS;
    }

    if(nbytes)
    {
        msg->o.ret_val = EOK;
        return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o + nbytes);
    }
	return EOK;
}

/*
 * build_path
 * 
 * recurse back through a partition hierarchy building up a path name to
 * <mpart> 
*/
static int build_path(mempartmgr_attr_t *mp, mempart_path_t path, unsigned pos)
{
	unsigned  len;
	unsigned  space;
	int  r;

	if (mp == NULL)
		return pos;

	if ((r = build_path(mp->parent, path, pos)) < 0)
		return r;

	pos = (unsigned)r;
	len = strlen(mp->name);
	space = sizeof(path) - pos;

	if (len > space)
		return -1;
			
	if (pos > 0)
		path[pos++] = '/';
	STRLCPY(&path[pos], mp->name, space);
	pos += len;
	return pos;	
}


__SRCVERSION("$IQ: mempartmgr_devctl.c,v 1.23 $");

