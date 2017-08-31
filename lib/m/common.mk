#
# Copyright 2004, QNX Software Systems Ltd. All Rights Reserved.
#
# This source code may contain confidential information of QNX Software
# Systems Ltd.  (QSSL) and its licensors. Any use, reproduction,
# modification, disclosure, distribution or transfer of this software,
# or any software which includes or is based upon any of this code, is
# prohibited unless expressly authorized by QSSL by written agreement. For
# more information (including whether this source code file has been
# published) please email licensing@qnx.com.
#
ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

define PINFO
PINFO DESCRIPTION=Math functions library
endef

NAME=m

EXTRA_INCVPATH = $(PRODUCT_ROOT)/c/inc $(PRODUCT_ROOT)/c/public $(PROJECT_ROOT)/inc


include $(MKFILES_ROOT)/qmacros.mk

# We have to use the 4.2.1 (or better) compiler to get the vfp support
# This can be removed once 4.2.1 (or better) is the default. 2007/10/10	
ifneq ($(filter vfp, $(VARIANTS)),)
GCC_VERSION=4.2.1
ifeq ($(wildcard $(QNX_HOST)/etc/qcc/gcc/4.2.1*),)
# If no 4.2.1 compiler present, kill the variant	
ALL_DEPENDENCIES=$(PRE_TARGET)
TARGET_INSTALL=
endif
endif
	
include $(MKFILES_ROOT)/qtargets.mk

include $(PROJECT_ROOT)/soversion.mk
-include $(PRODUCT_ROOT)/c/lib/soversion.mk
ifndef LIBC_SO_VERSION
LIBC_SO_VERSION=2
endif
-include $(PRODUCT_ROOT)/cpp/soversion.mk
ifndef LIBCPP_SO_VERSION
LIBCPP_SO_VERSION=2
endif

SO_VERSION=$(LIBM_SO_VERSION)
SONAME_SO=libm.so

ifeq ($(filter g, $(VARIANTS)),)
CCFLAGS += -O2
endif

# This next line is to work around a qcc problem - it can be removed
# once qcc has been updated. 2007/10/10
VFP_qcc=-Wa,-mfpu=vfp


CCVFLAG_vfp += -mfpu=vfp -mfloat-abi=softfp $(VFP_$(COMPILER_DRIVER))

CCFLAGS += $(CCFLAGS_$(COMPILER_TYPE)) $(CCFLAGS_$(basename $@))
CCFLAGS += -D_LIBC_SO_VERSION=$(LIBC_SO_VERSION) -D_LIBCPP_SO_VERSION=$(LIBCPP_SO_VERSION) -D_LIBM_SO_VERSION=$(LIBM_SO_VERSION)

# Avoid optimizer bugs (dinkum does this fo their lib
CCFLAGS_copysign = -O0
CCFLAGS_copysignf = -O0
CCFLAGS_copysignl = -O0
CCFLAGS_feclearexcept = -O0
CCFLAGS_fegetenv = -O0
CCFLAGS_fegetexceptflag = -O0
CCFLAGS_fegetround = -O0
CCFLAGS_fegettrapenable = -O0
CCFLAGS_feholdexcept = -O0
CCFLAGS_feraiseexcept = -O0
CCFLAGS_fesetenv = -O0
CCFLAGS_fesetexceptflag = -O0
CCFLAGS_fesetround = -O0
CCFLAGS_fesettrapenable = -O0
CCFLAGS_fetestexcept = -O0
CCFLAGS_feupdateenv = -O0
CCFLAGS_nexttoward = -O0
CCFLAGS_nexttowardf = -O0
CCFLAGS_nexttowardl = -O0


#
# Don't display gcc warnings in Dinkim libs
#
CCFLAGS_gcc += -Wno-uninitialized -Wno-missing-braces -Wno-deprecated-declarations
