/*
 * Portions Copyright 2003, QNX Software Systems Ltd. All Rights Reserved.
 *
 * This source code may contain confidential information of QNX Software
 * Systems Ltd.  (QSSL) and its licensors. Any use, reproduction,
 * modification, disclosure, distribution or transfer of this software,
 * or any software which includes or is based upon any of this code, is
 * prohibited unless expressly authorized by QSSL by written agreement. For
 * more information (including whether this source code file has been
 * published) please email licensing@qnx.com.
 */

/*
 * (c) Copyright 1990, 1991 Conor P. Cahill (uunet!virtech!cpcahil).  
 * You may copy, distribute, and use this software as long as this
 * copyright statement is not removed.
 */
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/neutrino.h>
#include <sys/storage.h>
#include <sys/syspage.h>
#include "context.h"
#include <errno.h>
#include <sys/wait.h>

#include "malloc-lib.h"
#include "tostring.h"

using namespace std;

void *operator new(size_t size) {
	int line;
  line = (int)__builtin_return_address(0);
  return(debug_malloc(0, line, size));
}

void operator delete(void *ptr) 
{
	int line;
	line = (int)__builtin_return_address(0);
  debug_free((char *)NULL, line, ptr);
}

