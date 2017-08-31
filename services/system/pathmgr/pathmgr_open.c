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
#include <fcntl.h>
#include <dirent.h>
#include <sys/dcmd_all.h>
#include "pathmgr_node.h"
#include "pathmgr_object.h"
#include "pathmgr_proto.h"
#include <sys/pathmgr.h>

struct node_ocb {
	struct node_entry				*node;
	off_t							offset;
};

int devmem_check_perm(resmgr_context_t *ctp, mode_t mode, uid_t uid, gid_t gid, int check);

static int node_read(resmgr_context_t *ctp, io_read_t *msg, void *vocb) {
	struct node_entry				*node;
	unsigned						size;
	struct dirent					*d;
	struct node_ocb					*ocb = vocb;
	int								nbytes, skip;

	//Our NAME objects always return 0 bytes like a dev null
	if ((node = ocb->node) && node->object && node->object->hdr.type == OBJECT_NAME) {
		_IO_SET_READ_NBYTES(ctp, 0);
		return _RESMGR_PTR(ctp, NULL, 0);
	}

	// Lock access to the node so that it's children don't get deleted as
	// we're walking the list.
	pathmgr_node_access( ocb->node );

	//Need to be able to handle a seek here too ...
	if (!(node = ocb->node->child)) {
		pathmgr_node_complete( ocb->node );
		return EBADF;
	}
	skip = ocb->offset;

	size = min(msg->i.nbytes, ctp->msg_max_size);
	d = (struct dirent *)(void *)msg;
	nbytes = offsetof(struct dirent, d_name) + 1;

	/*
	 If we are in a valid proc maintained directory,
	 we want to make sure that we don't actually display
	 any of the hidden entries used for cwd operations.
	 -- also in the open code
	*/
	while(nbytes < size && node && node != (struct node_entry *)-1) {
		if (node->child_objects || node->object) {
			if (skip <= 0) {
				size_t  nmlen = strlen(node->name);
				if((nbytes + nmlen) >= size) {
					break;
				}
				memset(d, 0, sizeof(struct dirent));
				d->d_namelen = nmlen;
				d->d_reclen = (offsetof(struct dirent, d_name) + nmlen + 1 + 7) & ~7;
				memcpy(d->d_name, node->name, nmlen + 1);	// '\0' guaranteed to be copied
				d->d_ino = (ino_t)node;
				nbytes += d->d_reclen;
				d = (struct dirent *)((unsigned)d + d->d_reclen);
				ocb->offset++;
			}
			skip--;
		}
		node = node->sibling;
	}
	
	pathmgr_node_complete( ocb->node );

	nbytes -= offsetof(struct dirent, d_name) + 1;

	resmgr_endian_context(ctp, _IO_READ, S_IFDIR, 0);
	_IO_SET_READ_NBYTES(ctp, nbytes);
	return _RESMGR_PTR(ctp, msg, nbytes);
}

static int node_lseek(resmgr_context_t *ctp, io_lseek_t *msg, void *vocb) {
	struct node_ocb			*ocb = vocb;

	/* The only seek offset we support is to reset the file to 0*/
	if(msg->i.whence != SEEK_SET || msg->i.offset != 0) {
		return ENOSYS;
	}

	ocb->offset = 0;
	if(msg->i.combine_len & _IO_COMBINE_FLAG) {
		return EOK;
	}

	msg->o = ocb->offset;
	return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
}


static int node_close_ocb(resmgr_context_t *ctp, void *reserved, void *vocb) {
	struct node_ocb					*ocb = vocb;

	pathmgr_node_detach(ocb->node);
	_sfree(ocb, sizeof *ocb);
	return EOK;
}

static int node_devctl(resmgr_context_t *ctp, io_devctl_t *msg, void *vocb) {
	union {
		int								oflag;
		int								mountflag;
	}								*data = (void *)(msg + 1);
	unsigned						nbytes = 0;

	switch((unsigned)msg->i.dcmd) {
	case DCMD_ALL_GETFLAGS:
		data->oflag = O_RDONLY;
		nbytes = sizeof data->oflag;
		break;

	case DCMD_ALL_SETFLAGS:
		break;

	case DCMD_ALL_GETMOUNTFLAGS: {
		data->mountflag = 0;
		nbytes = sizeof data->mountflag;
		break;
	}

	default:
		return _RESMGR_DEFAULT;
	}

	if(nbytes) {
		msg->o.ret_val = 0;
		return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o + nbytes);
	}
	return EOK;
}

static int node_stat(resmgr_context_t *ctp, io_stat_t *msg, void *vocb) {
	union object					*o;
	struct node_ocb					*ocb = vocb;

	memset(&msg->o, 0x00, sizeof msg->o);
	msg->o.st_ino = INODE_XOR(ocb->node);
	msg->o.st_ctime =
	msg->o.st_mtime =
	msg->o.st_atime = time(0);
	msg->o.st_dev = path_devno;
	msg->o.st_rdev = 0;

	o = ocb->node->object;
	if((!o || o->hdr.type != OBJECT_PROC_SYMLINK) && ocb->node->child) {
		msg->o.st_nlink = 2;
		msg->o.st_mode = S_IFDIR | 0555;
		return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
	}

	if(o) {
		switch(o->hdr.type) {
		case OBJECT_PROC_SYMLINK:
			msg->o.st_size = o->symlink.len - 1;
			msg->o.st_nlink = 1;
			msg->o.st_mode = S_IFLNK | 0777;
			return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
		case OBJECT_NAME:
			msg->o.st_nlink = 1;
			msg->o.st_mode = S_IFNAM | 0666;
			return _RESMGR_PTR(ctp, &msg->o, sizeof msg->o);
		default:
			break;
		}
	}

	return ENOSYS;
}

static const resmgr_io_funcs_t node_funcs = {
	_RESMGR_IO_NFUNCS,
	node_read,          /* read */
	0,                  /* write */
	node_close_ocb,     /* close_ocb */
	node_stat,          /* stat */
	0,                  /* notify */
	node_devctl,        /* devctl */
	0,                  /* unblock */
	0,                  /* pathconf */
	node_lseek,         /* lseek */
};

static int detach(NODE *nop, int ret) {
	pathmgr_node_detach(nop);
	return ret;
}

static int reply_redirect(resmgr_context_t *ctp, io_open_t *msg, OBJECT *obp) {
	unsigned						eflag;
	uint32_t						remotend;
	struct _io_connect_link_reply	*linkp = (void *)msg;
	struct _server_info				*info = (void *)(linkp + 1);

	remotend = ctp->info.nd;

	_IO_SET_CONNECT_RET(ctp, _IO_CONNECT_RET_MSG);
	eflag = msg->connect.eflag;

	memset(linkp, 0x00, sizeof *linkp);
	memset(info, 0x00, sizeof *info);

	linkp->eflag = eflag;
	linkp->path_len = 0;		//This is key, no message stream comes after the info!

	//All we need for the new fd is the nid/pid/chid trio
	if(proc_thread_pool_reserve() != 0) {
		return EAGAIN;
	}
	info->nd = netmgr_remote_nd(remotend, obp->server.nd);
	proc_thread_pool_reserve_done();
	info->pid = obp->server.pid;
	info->chid = obp->server.chid;

	//Do I need to do a MsgKeyData(ctp->rcvid, _NTO_KEYDATA_CALCULATE, ... )

	return _RESMGR_PTR(ctp, msg, sizeof *linkp + sizeof *info);
}

static int
do_unlink(NODE *nop, OBJECT *o) {
	int		nob;
	OBJECT	*obj;

	// Make sure we only unlink once per object
	pathmgr_node_access(nop);
	if(nop->flags & NODE_FLAG_UNLINKED) {
		pathmgr_node_complete(nop);
		return detach(nop, ENOENT);
	}

	// Count the number of objects				

	nob = 0;
	for(obj = nop->object; obj != NULL; obj = obj->hdr.next) {
		nob++;
	}
	if(nob == 1) nop->flags |= NODE_FLAG_UNLINKED;
	pathmgr_node_complete(nop);
	pathmgr_object_detach(o);

	return detach(nop, EOK);
}

enum { PATHMGR_SERVICE_OPEN, PATHMGR_SERVICE_CREAT, PATHMGR_SERVICE_LS };
static int service_error(io_open_t *msg, int action)
{
	if (action != PATHMGR_SERVICE_OPEN) {
		switch (msg->connect.file_type) {
		case _FTYPE_ANY:
			break;
		case _FTYPE_FILE:
		case _FTYPE_SHMEM:
		case _FTYPE_NAME:
			break;
		case _FTYPE_SOCKET:
			if (action != PATHMGR_SERVICE_CREAT)
				return(ENOSYS);
			break;
		default:
			return(ENOSYS);
		}
	}
	else if (msg->connect.file_type != _FTYPE_ANY) {
		return(ENOSYS);
	}
	return(ENOENT);
}

int pathmgr_open(resmgr_context_t *ctp, io_open_t *msg, void *handle, void *extra) {
	NODE						*nop;
	struct node_ocb				*ocb;
	const char					*result;

	if (msg->connect.path_len + sizeof(*msg) >= ctp->msg_max_size) {
		return ENAMETOOLONG;
	}

	nop = pathmgr_node_lookup(0, msg->connect.path, 
							 /* Break out if we hit non-server object, attach to the 
							    node to prevent it's deletion and avoid returning 
								empty nodes (for links, we could probably remove this) */
							 PATHMGR_LOOKUP_NOSERVER | PATHMGR_LOOKUP_ATTACH | PATHMGR_LOOKUP_NOEMPTIES |
							 /* When looking up links, ignore the autocreated entries,
							    this isn't perfect, but it avoids the obvious case when
								we have overlaid two links.  It would be better to be
								able to specify one specific node to lookup as an 
								alternative. Add to the TODO list. */
							 ((ctp->id == link_root_id) ? PATHMGR_LOOKUP_NOAUTO : 0), 
							 &result);

	if(nop) {
		union object				*o = nop->object;

		/* Only look for server/symlink entries (ie non-server) */
		if (ctp->id == link_root_id) { 
			if (o && o->hdr.type != OBJECT_PROC_SYMLINK) {
				return detach(nop, ENOENT);
			}
		} else {						
			/* Only look for non-link entries here */
			while (o && (o->hdr.type == OBJECT_PROC_SYMLINK)) {
				o = o->hdr.next;
			}
			/* If we don't have an object but we still have path then drop out
			   right away since code later on doesn't handle this situation */
			if (!o && result[0] != '\0') {
				return detach(nop, service_error(msg, PATHMGR_SERVICE_OPEN));
			}
		}

		if(o) {
			switch(o->hdr.type) {
			case OBJECT_PROC_SYMLINK:
				if(result[0] == '\0') {
					if(msg->connect.subtype == _IO_CONNECT_READLINK) {
						return detach(nop, reply_symlink(ctp, 0, &msg->link_reply, &o->symlink, 0, 0));
					}
					if(S_ISLNK(msg->connect.mode) && !(msg->connect.eflag & _IO_CONNECT_EFLAG_DIR)) {
						break;
					}
				}
				return detach(nop, reply_symlink(ctp, msg->connect.eflag, &msg->link_reply, &o->symlink, msg->connect.path, result));

			case OBJECT_MEM_SHARED:
				if((msg->connect.eflag & _IO_CONNECT_EFLAG_DIR) || result[0] != '\0') {
					return detach(nop, ENOTDIR);
				}

				if( (msg->connect.subtype != _IO_CONNECT_OPEN)  &&
					(msg->connect.subtype != _IO_CONNECT_COMBINE) &&
					(msg->connect.subtype != _IO_CONNECT_COMBINE_CLOSE) ) {
					break;
				}

				if((msg->connect.ioflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
					return detach(nop, EEXIST);
				}

				return detach(nop, mem_open(ctp, msg, o, extra));

			case OBJECT_NAME:
				if (result[0] == '\0' && msg->connect.file_type == _FTYPE_NAME) {
					return detach(nop, reply_redirect(ctp, msg, o));
				}
				else if (result[0] == '\0') {
					break;
				}
				return detach(nop, ENOENT);

			case OBJECT_SERVER:
				if(nop->child && result[0] == '\0') {
					break;
				}
				return detach(nop, service_error(msg, PATHMGR_SERVICE_OPEN));

			default:
				return detach(nop, ENOENT);
			}
		}
		switch(msg->connect.subtype) {
		case _IO_CONNECT_READLINK:
			return detach(nop, ENOSYS);

		case _IO_CONNECT_UNLINK:
			if(!o) {
				return detach(nop, nop->child ? ENOTEMPTY : ENOENT);
			}
			switch(o->hdr.type) {
			case OBJECT_NAME:
			case OBJECT_SERVER:
				if(msg->connect.eflag & _IO_CONNECT_EFLAG_DIR) {
					return detach(nop, (o->server.hdr.flags & PATHMGR_FLAG_DIR) ? ENOSYS : ENOTDIR);
				}
				if(!(o->server.hdr.flags & PATHMGR_FLAG_STICKY)) {
					return detach(nop, (o->server.hdr.flags & PATHMGR_FLAG_DIR) ? EBUSY : ENOSYS);
				}
				pathmgr_object_detach(o);
				return detach(nop, EOK);

			case OBJECT_PROC_SYMLINK:
				if(!proc_isaccess(0, &ctp->info)) {
					return detach(nop, EPERM);
				}
				return do_unlink(nop, o);
			case OBJECT_MEM_SHARED:
				/* @@@ We should really have an object callout for this */
				if(o->mem.pm.mode && devmem_check_perm(ctp, o->mem.pm.mode, o->mem.pm.uid, o->mem.pm.gid, S_ISUID)) {
					return detach(nop, EPERM);
				}
				// This is not ideal, but we only need to adjust ref count once
				// to account for multiple unlink races (PR18121)
				// Note - we could do the ref coutning in do_unlink but this
				// function is used for more than OBJECT_SHARED
				pathmgr_node_access(nop);
				if((nop->flags & NODE_FLAG_UNLINKED) == 0) {
					OBJECT_SHM_ADJ_REF_COUNT(o, -1);
				}
				pathmgr_node_complete(nop);
				return do_unlink(nop, o);
			default:
				break;
			}
			return detach(nop, ENOSYS);

		case _IO_CONNECT_MKNOD:
			return detach(nop, EEXIST);

		default:
			break;
		}

		/*
		 We don't want to show proc's hidden entries, so if that
		 is all the object that there is then don't allow opening.
		 Also if we have a longer path, we should exit out now since
		 a long path should have matched some sort of object.
		*/
		if (!o && nop->child_objects == 0) {
			return detach(nop, ENOENT) ;
		}
		if(service_error(msg, PATHMGR_SERVICE_LS) == ENOSYS) {
			return detach(nop, ENOSYS);
		}

		if(!(ocb = _scalloc(sizeof *ocb))) {
			return detach(nop, ENOMEM);
		}
		ocb->node = nop;
		if(resmgr_open_bind(ctp, ocb, &node_funcs) == -1) {
			return detach(nop, errno);
		}
		return EOK;
	}

	if(msg->connect.subtype == _IO_CONNECT_MKNOD) {
		return ENOSYS;
	}
	else if(!(msg->connect.ioflag & O_CREAT)) {
		return service_error(msg, PATHMGR_SERVICE_CREAT);
	}

	switch(msg->connect.file_type) {
	case _FTYPE_FILE:
	case _FTYPE_SHMEM: {
		OBJECT						*obp;
		int							status;

		if(!(obp = pathmgr_object_attach(0, msg->connect.path, OBJECT_MEM_SHARED, 0, 0))) {
			return ENOMEM;
		}
		if((status = mem_open(ctp, msg, &obp->mem, extra)) != EOK) {
			pathmgr_object_detach(obp);
		}
		OBJECT_SHM_ADJ_REF_COUNT(obp, 1);
		return status;

	}

   case _FTYPE_TYMEM:
		return tymem_open(ctp, msg, NULL, extra);			   
		
	default:
		break;
	}
	return service_error(msg, PATHMGR_SERVICE_OPEN);
}

int pathmgr_dounlink(resmgr_context_t *ctp, io_unlink_t *msg, void *handle, void *extra) {
	return pathmgr_open(ctp, (io_open_t *)msg, handle, extra);
}

int pathmgr_domknod(resmgr_context_t *ctp, io_mknod_t *msg, void *handle, void *reserved) {
	return pathmgr_open(ctp, (io_open_t *)msg, handle, reserved);
}

int pathmgr_readlink(resmgr_context_t *ctp, io_readlink_t *msg, void *handle, void *extra) {
	return pathmgr_open(ctp, (io_open_t *)msg, handle, extra);
}


__SRCVERSION("pathmgr_open.c $Rev: 156315 $");
