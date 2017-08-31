/*
 * $QNXLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 *
 * You must obtain a written license from and pay applicable 
 * license fees to QNX Software Systems before you may reproduce, 
 * modify or distribute this software, or any work that includes 
 * all or part of this software.   Free development licenses are 
 * available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email 
 * licensing@qnx.com.
 * 
 * This file may contain contributions from others.  Please review 
 * this entire file for other proprietary rights or license notices, 
 * as well as the QNX Development Suite License Guide at 
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "kdebug.h"

unsigned
vaddrinfo(struct kdebug_entry *p, uintptr_t vaddr, paddr_t *addr, size_t *size) {
	struct kdebug_info	*kinfo;
	paddr64_t			paddr;
	unsigned			prot;

	kinfo = private->kdebug_info;
	if(kinfo != NULL) {
		if(kinfo->vaddr_to_paddr2) {
			paddr64_t	psize;
			unsigned	r;

			psize = *size;
			r = kinfo->vaddr_to_paddr2(p, vaddr, &paddr, &psize);
			*addr = paddr;
			*size = psize;
			return r;
		}
		*addr = vaddr;
		return kinfo->vaddr_to_paddr(p, (uintptr_t *)addr, (unsigned *)size);
	}
	prot = cpu_vaddrinfo((void *)vaddr, &paddr, size);
	*addr = paddr;
	return prot;
}
