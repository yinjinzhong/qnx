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
 * Compatibilty stubs for pre 6.3.0 systems
 */

typedef unsigned long long UDWType;
typedef long long DWType;

extern DWType __divdi3_i4( DWType, DWType );
extern UDWType __udivdi3_i4( UDWType, UDWType );

extern DWType __moddi3( DWType, DWType );
extern UDWType __umoddi3( UDWType, UDWType );

DWType __divdi3( DWType a, DWType b) __attribute__((weak));
UDWType __udivdi3( UDWType a, UDWType b) __attribute__((weak));

DWType __divdi3( DWType a, DWType b)
{
	return __divdi3_i4( a, b );
}

UDWType __udivdi3( UDWType a, UDWType b)
{
	return __divdi3_i4( a, b );
}

UDWType __umoddi3_bringin_stub( UDWType a, UDWType b )
{
	return __umoddi3( a, b );
}
DWType __moddi3_bringin_stub( DWType a, DWType b )
{
	return __moddi3( a, b );
}
