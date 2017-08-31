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

#include <sys/mman.h>
#include <sh/cpu.h>
#include "kdintl.h"


void
cpu_init_map(void) {
}


void *
cpu_map(paddr64_t paddr, unsigned size, unsigned prot, int colour, unsigned *mapped_len) {
	if((paddr < SH_P1SIZE) && (colour == -1)) {
		*mapped_len = SH_P1SIZE - (uintptr_t)paddr;
		return (void *)SH_PHYS_TO_P1((uintptr_t)paddr);
	}
	//ZZZ Deal with paddrs >= SH_P1SIZE, PROT_NOCACHE and colours
	return NULL;
}


void
cpu_unmap(void *p, unsigned size) {
	//ZZZ deal with non-P1 addresses
}


unsigned
cpu_vaddrinfo(void *p, paddr64_t *paddrp, unsigned *lenp) {
	uintptr_t	vaddr = (uintptr_t)p;

	if(SH_IS_P1(vaddr) || SH_IS_P2(vaddr) || SH_IS_P4(vaddr)) {
		*paddrp = vaddr & ~0xc0000000;
		*lenp = SH_P0SIZE - (unsigned)*paddrp;
		return PROT_READ|PROT_WRITE|PROT_EXEC;
	}
	return PROT_NONE;
}
