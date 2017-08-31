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
 *  sys/dir.h
 *

 */
#ifndef _DIR_H_INCLUDED
#define _DIR_H_INCLUDED

#if defined(__WATCOMC__) && !defined(_ENABLE_AUTODEPEND)
 #pragma read_only_file;
#endif

#include <dirent.h>
#include <limits.h>

#ifndef __PLATFORM_H_INCLUDED
#include <sys/platform.h>
#endif

__BEGIN_DECLS

#include <_pack64.h>

struct direct {
    unsigned long d_fileno;
    unsigned short d_reclen;
    unsigned short d_namlen;
    char d_name[1];
};

extern int alphasort(const void *__a, const void *__b);
extern int scandir(char *__dirname, struct direct *(*namelist[]),
        int (*select)(struct dirent *), int (*compar)(const void *,const void *));


#include <_packpop.h>

__END_DECLS

#endif

/* __SRCVERSION("dir.h $Rev: 153052 $"); */
