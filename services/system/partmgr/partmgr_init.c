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
 * partmgr_init
 * 
 * NOTE:
 * The partmgr_xxx() code provides management of the "/proc/<pid>/partition/"
 * namespace. This could be implemented as /pathmgr/ *.c code (similar to
 * devtext.c, etc) however it is currently lumped in the /partmgr/ directory
 * so that it can be loaded as a separate module (ie. /proc/<pid>/partition/
 * won't show up in the name space unless the 'partmgr' module is included). An
 * alternate packaging stategy would be to always include the 'partmgr_xxx()'
 * code so that the /proc/<pid>/partition/ name is always present, but empty and
 * only if the 'partmgr' module is included (which has the mempartmgr_xxx() and
 * schedpartmgr_xxx() code) will the namespace under /proc/<pid>/partition/
 * be populated with information. The second approach requires the mempartmgr
 * and schedpartmgr to expose their interface via a function table so load time
 * linkage can be established. The first approach allows 'partmgr' to call
 * directly into 'mempartmgr' and 'schedpartmgr' routines (since both will be
 * loaded in the same module). The first approach will be coded with macros
 * for the functions called so that the second approach is easily implemented. 
 * 
 * Provide resource manager initialization for the partitioning module
 * 
*/

#include "partmgr.h"

static void partmgr_init(const char * const path);


/*******************************************************************************
 * initialize_partmgr
 * 
 * Called from kmain.c during module initialization. This function will
 * initialize the partitioning resource manager
*/
void initialize_partmgr(unsigned version, unsigned pass)
{
	if(version != LIBMOD_VERSION_CHECK) {
		kprintf("Version mismatch between procnto (%d) and libmod_partmgr (%d)\n",
					version, LIBMOD_VERSION_CHECK);
		crash();
	}
	/*
	 * FIX ME
	 * if this code becomes separate from mempartmgr/schedpartmgr and that code
	 * is not installed (module not loaded), don't install anything
	*/
	switch(pass)
	{
		case 10:
		{
			MEMPARTMGR_INIT("/partition");
//			SCHEDPARTMGR_INIT("/partition");

			partmgr_init("/proc");

			kprintf("partition resource manager module loaded\n");
			
			break;
		}
		default:
			break;
	}
}


/*
 * partmgr_init
*/
static resmgr_attr_t  attr;
const resmgr_io_funcs_t partmgr_io_funcs =
{
	_RESMGR_IO_NFUNCS,
	STRUCT_FLD(read) partmgr_read,
	STRUCT_FLD(write) NULL,					// this line needed for watcom compiler
	STRUCT_FLD(close_ocb) partmgr_close,	// included as per comment in partmgr_close.c
	STRUCT_FLD(stat) partmgr_stat,
	STRUCT_FLD(notify) NULL,				// this line needed for watcom compiler
	STRUCT_FLD(devctl) partmgr_devctl,
	STRUCT_FLD(unblock) partmgr_unblock,
	STRUCT_FLD(pathconf) NULL,				// this line needed for watcom compiler
	STRUCT_FLD(lseek) partmgr_lseek,
	STRUCT_FLD(chmod) partmgr_chmod,
	STRUCT_FLD(chown) partmgr_chown,
	STRUCT_FLD(utime) NULL,					// this line needed for watcom compiler
	STRUCT_FLD(openfd) partmgr_openfd,
	STRUCT_FLD(fdinfo) partmgr_fdinfo,
};

static const resmgr_connect_funcs_t partmgr_connect_funcs =
{
	_RESMGR_CONNECT_NFUNCS,
	STRUCT_FLD(open) partmgr_open,
	STRUCT_FLD(unlink) partmgr_unlink,
};

dev_t  partmgr_devno;
static void partmgr_init(const char * const path)
{
	memset(&attr, 0, sizeof(attr));
	attr.nparts_max = 1;
	attr.msg_max_size = 2048;
	attr.flags |= _RESMGR_FLAG_DIR;
//	kprintf("%s: attaching %s\n", __func__, path);
	resmgr_attach(dpp, &attr, path, _FTYPE_ANY, _RESMGR_FLAG_DIR | _RESMGR_FLAG_AFTER, &partmgr_connect_funcs, &partmgr_io_funcs, 0);
	rsrcdbmgr_proc_devno(_MAJOR_FSYS, &partmgr_devno, -1, 0);
}


__SRCVERSION("$IQ: partmgr_init.c,v 1.23 $");

