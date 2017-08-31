#
# $QNXLicenseA:
# Copyright 2007, QNX Software Systems. All Rights Reserved.
# 
# You must obtain a written license from and pay applicable license fees to QNX 
# Software Systems before you may reproduce, modify or distribute this software, 
# or any work that includes all or part of this software.   Free development 
# licenses are available for evaluation and non-commercial purposes.  For more 
# information visit http://licensing.qnx.com or email licensing@qnx.com.
#  
# This file may contain contributions from others.  Please review this entire 
# file for other proprietary rights or license notices, as well as the QNX 
# Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
# for other information.
# $
#

/*
 * out_*.S
 *	Routines for calling into the kernel from "outside"
 *
 * "Outside" here refers to other programs which are also loaded
 * in the local address space, probably k0seg.  While such code can
 * call us directly, the GP register's short data area pointer will
 * be incorrect.  This code saves the old GP value, sets GP to the
 * kernel's _gp location, runs the internal function, then restores
 * GP.
 *
 * The current code all assumes < 4 arguments, so adding words to
 * the stack is not expected to cause disruption.
 */
#include <mips/asm.h>
#include <mips/cpu.h>
#include <asm_variant.h>

	.text
	.align	4

	.set noreorder

/*
 * outside_get_inkernel()
 *	Return inkernel value
 *
 */
FRAME(outside_get_inkernel,sp,0,ra)
	GET_INKERNEL(v0,1)
	j	ra
	 nop
ENDFRAME(outside_get_inkernel)
