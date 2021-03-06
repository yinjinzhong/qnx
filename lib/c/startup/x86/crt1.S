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




.extern		main
.extern		exit
.extern		_CMain
.extern		_init
.extern		_fini
.extern		atexit
.extern		errno

	.text
	.byte	'N', 'I', 'A', 'M'      /* Used by debugger for setting a break point */
    .long	main
	
#if defined(VARIANT_wcc)
.globl		_cstart_
_cstart_:
    jmp		_CMain
	.type	_cstart_,@function
	.size	_cstart_,.-_cstart_
#else

.globl		_start
_start:
#if defined(__PIC__)
	call	1f
1:	popl	%ebx
	addl	$_GLOBAL_OFFSET_TABLE_+[.-1b],%ebx
    call	_CMain@PLT
#else
	/* Stash EDX to EBX */
	movl	%edx,%ebx
	/* Call _init_libc -- need to extract argc, argv, env and auxv first */
	// argc
	movl 0(%esp),%esi
	// argv
	leal 4(%esp),%ecx
	// envp
	leal (%ecx,%esi,4),%eax
	leal 4(%eax),%edi
	leal 8(%eax),%edx
	cmpl $0,4(%eax)
	je .Ldoneargv
	.align 4
	// Scan for auxv
.Lloop:
	movl (%edx),%eax
	addl $4,%edx
	testl %eax,%eax
	jne .Lloop
.Ldoneargv:
	pushl %ebx
	pushl %edx
	pushl %edi
	pushl %ecx
	pushl %esi
	call _init_libc
	// Leave the args on the stack, we'll just pass them to main()
	
	pushl	$_fini
	call	atexit
	addl	$4,%esp
	call 	_init
.Lmain:
	movl	$0,errno
    call	main
	pushl	%eax
	call	exit
#endif
	int		$3	/* Should never get here.... */
	.type	_start,@function
	.size	_start,.-_start
#endif
