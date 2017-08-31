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




	.text
	.align 2

	.extern	_CMain
	.extern	_init
	.globl	_start

	/*
	 * On entry:
	 *	r0 = at_exit pointer (set to 0 by kernel, possibly non-zero from ldd)
	 *  Stack is layed out as follows:
	 *
	 *			:
	 *		 auxv[0]
	 *		 0
	 *		 envp[n]
	 *		 	:
	 *		 envp[0]
	 *		 0
	 *		 argv[argc]
	 *		 	:
	 *		 argv[0]
	 *	sp-> argc
	 */
_start:
	mov		r4, r0				// save at_exit pointer
	ldr		r0, [sp]			// argc
	add		r1, sp, #4			// argv
	add		r2, r1, r0, lsl #2	// skip over argv list
	add		r2, r2, #4			// envp
	mov		r3, r2				// skip over envp list
0:	ldr		ip, [r3], #4
	teq		ip, #0
	bne		0b
	str		r4, [sp, #-4]!
	mov		r4, r0				// save argc, argv, envp
	mov		r5, r1
	mov		r6, r2
	bl		_init_libc			// _init_libc(argc, argv, envp, auxv, at_exit)
	add		sp, sp, #4

	ldr		r0, =_fini
	bl		atexit
	bl		_init
	ldr		r0, =errno
	mov		r1, #0
	str		r1, [r0]
	mov		r0, r4
	mov		r1, r5
	mov		r2, r6
	bl		main
	bl		exit

	/*
	 * We should never get here
	 */
0:	b	0b

	.size _start,.-_start
	.type _start,function
