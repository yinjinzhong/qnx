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
 *  sh/syspage.h
 *

 */

#ifndef __SYSPAGE_H_INCLUDED
#error sh/syspage.h should not be included directly.
#endif

#ifdef __SH__

extern struct cpupage_entry *_cpupage_ptr;

#endif /* __SH__ */
 
#if defined(ENABLE_DEPRECATED_SYSPAGE_SECTIONS)
/*
 *	Support hardware flags
 */
#define SH_HW_BUS_ISA		(1UL <<  0)	/* Machine has ISA bus */
#define SH_HW_BUS_EISA		(1UL <<  1)	/* Machine has EISA bus */
#define SH_HW_BUS_PCI		(1UL <<  2)	/* Machine has PCI bus */
#define SH_HW_BUS_MCA		(1UL <<  3)	/* Machine has micro channel bus */
#define SH_HW_BUS_VME		(1UL <<  2)	/* Machine has VME bus */

struct sh_boxinfo_entry {
	unsigned long	hw_flags;		
	unsigned long	spare [9];
};
#endif

struct sh_syspage_entry {
	syspage_entry_info	DEPRECATED_SECTION_NAME(boxinfo);
	_SPPTR(_Uint32t)	exceptptr;
	volatile unsigned long imask;
	unsigned long	flags;
};

#define SH_SYSPAGE_FLAGS_PERMANENT_MAP		0x1

#define	SH_CPU_FLAG_MOVLICO			(1<<0)	/* Has movli/movco instructions */
#define	SH_CPU_FLAG_SMP				(1<<1)	/* SMP multi-cpu system         */

struct sh_kernel_entry {
	unsigned long	code[2];
};

struct sh_cpupage_entry {
	volatile unsigned long	imask;
};

/* __SRCVERSION("syspage.h $Rev: 153052 $"); */
