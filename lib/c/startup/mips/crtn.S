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





/* This is to make a placeholder note section in the executable */
	.section .note,"a"
	.align 4
	
	.set noreorder

	.section .init
	lw		$31,64($29)
	lw		$23,60($29)
	jr		$31
	 addiu	$29,72 

	.section .fini
	lw		$23,60($29)
	lw		$31,64($29)
	jr		$31
	 addiu	$29,72 
