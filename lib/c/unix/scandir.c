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




#include <sys/dir.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>

int alphasort(const void* a, const void* b)
{
    return strcmp((*(struct direct **)a)->d_name, (*(struct direct **)b)->d_name);
}

int scandir(char *dirname, struct direct *(*namelist[]),
    int (*select)(struct dirent *), int (*compar)(const void *, const void *))
{
    DIR *dirp;
    struct dirent *de;
    struct direct *dp;
    int nentries = 0;
    int nalloc = 0;

    if ((dirp = opendir(dirname)) == NULL) {
	return -1;
    }
    *namelist = NULL;
    while ((de = readdir(dirp)) != NULL) {
	if (select && (*select) (de) == 0)
	    continue;
	if (nalloc == nentries) {
	    nalloc += 32;		/* grab descent sized chunks */
	    *namelist = realloc(*namelist, sizeof dp * nalloc);
	    if (*namelist == NULL) {
		errno = ENOMEM;
		return -1;
	    }
	}
	//NTO, unlike QNX4, doesn't have a set buffer size for the name, 
	//so that needs to be allocated as well at this time.
	if ((dp = malloc(sizeof(*dp) + de->d_namelen + 1)) == NULL) {
	    errno = ENOMEM;
	    return -1;
	}
	dp->d_fileno = 0;
	dp->d_namlen = de->d_namelen;
	strcpy(dp->d_name, de->d_name);
	dp->d_reclen = sizeof(*dp) + de->d_namelen + 1;
	(*namelist)[nentries++] = dp;
    }
    closedir(dirp);
    if ((*namelist = realloc(*namelist, nentries * sizeof dp)) == NULL) {
	errno = ENOMEM;
	return -1;
    }

	if (compar)
		qsort(*namelist, nentries, sizeof *namelist, compar);

    return nentries;
}

__SRCVERSION("scandir.c $Rev: 153052 $");
