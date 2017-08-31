# Copyright 2001, QNX Software Systems Ltd. All Rights Reserved
#  
# This source code has been published by QNX Software Systems Ltd. (QSSL).
# However, any use, reproduction, modification, distribution or transfer of
# this software, or any software which includes or is based upon any of this
# code, is only permitted under the terms of the QNX Realtime Plaform End User
# License Agreement (see licensing.qnx.com for details) or as otherwise
# expressly authorized by a written license agreement from QSSL. For more
# information, please email licensing@qnx.com.
#
ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

.PHONY: first all iclean clean spotless install qinstall hinstall

first: all
	
EXTRA_INCVPATH = $(PROJECT_ROOT)/inc $(PROJECT_ROOT)/../../services/system/public $(PROJECT_ROOT)/../m/public $(PROJECT_ROOT)/../m/inc $(PROJECT_ROOT)/../pm/public $(PROJECT_ROOT)/../../services/registry/public

ASSEMBLER_TYPE_x86 = gcc
ASMOFF_FORMAT_x86 = cpp
DEFFILE = asmoff.def

OS=nto

#
# The mcount function is used for profiling, and needs the frame pointer
# to find the caller's adress
#
CCFLAGS_mcount = -fno-omit-frame-pointer

#
# Special compiler/assembler options for startup code
#
ASFLAGS_crt1+=-static
ASFLAGS_mcrt1+=-static
ASFLAGS_startup+=-shared

CCFLAGS_startup_ppc_gcc_gcc = -G0
CCFLAGS_startup_ppc_gcc_qcc = -Wc,-G0
CCFLAGS_startup_mips_gcc_gcc = -G0
CCFLAGS_startup_mips_gcc_qcc = -Wc,-G0
CCFLAGS_startup_sh_gcc_gcc = -mprefergot
CCFLAGS_startup_sh_gcc_qcc = -mprefergot
CCFLAGS_startup=-fpic -finhibit-size-directive $(CCFLAGS_startup_$(CPU)_$(COMPILER_TYPE)_$(COMPILER_DRIVER))

#
# This shrinks the library on PPC by avoiding unnecessary F.P. instructions
# on vararg functions that never do any floating point.
#
CCFLAGS_execl_ppc = -msoft-float
CCFLAGS_execle_ppc = -msoft-float
CCFLAGS_execlp_ppc = -msoft-float
CCFLAGS_execlpe_ppc = -msoft-float
CCFLAGS_fcntl_ppc = -msoft-float
CCFLAGS_iodir_ppc = -msoft-float
CCFLAGS_open_ppc = -msoft-float
CCFLAGS_mq_open_ppc = -msoft-float
CCFLAGS_sem_open_ppc = -msoft-float
CCFLAGS_typed_mem_open_ppc = -msoft-float
CCFLAGS_traceevent_ppc = -msoft-float
CCFLAGS_hwi_find_item_ppc = -msoft-float
CCFLAGS_sopen_ppc = -msoft-float
CCFLAGS_spawnl_ppc = -msoft-float
CCFLAGS_spawnle_ppc = -msoft-float
CCFLAGS_spawnlp_ppc = -msoft-float
CCFLAGS_spawnlpe_ppc = -msoft-float
CCFLAGS_mount_ppc = -msoft-float
CCFLAGS_ioctl_ppc = -msoft-float
CCFLAGS_open64_ppc = -msoft-float

#
# This works around an optimizer bug in the compiler
#
CCFLAGS_ioctl_sh += -O1

#
# And try to shrink the literal pools...
#
CCFLAGS_sh += -msmallpools

CCFLAGS_ansi += -D__INLINE_FUNCTIONS__

#
# Don't display gcc warnings in Dinkim libs
#
CCFLAGS_gcc_ansi += -Wno-uninitialized -Wno-switch -Wno-missing-braces -Wno-char-subscripts
CCFLAGS_gcc_stdio += -Wno-uninitialized -Wno-char-subscripts
CCFLAGS_gcc_string += -Wno-uninitialized

include $(MKFILES_ROOT)/qrules.mk

ifndef LIBC_SO_VERSION
include $(PROJECT_ROOT)/lib/soversion.mk
-include $(PRODUCT_ROOT)/cpp/soversion.mk
ifndef LIBCPP_SO_VERSION
LIBCPP_SO_VERSION=2
endif
-include $(PRODUCT_ROOT)/m/soversion.mk
ifndef LIBM_SO_VERISION
LIBM_SO_VERSION=2
endif
endif

ifeq ($(filter g, $(VARIANTS)),)
CCF_opt_gcc_    = -O2 -fomit-frame-pointer
CCF_opt_gcc_qcc = -O2 -fomit-frame-pointer
CCF_gcc_    = -O2 
CCF_gcc_qcc = -O2 
endif

# Never want debug information for startup files - causes compiler
# to get confused about the sections.
ifeq ($(SECTION),startup)
override DEBUG=
endif

CCF_wcc_	= -U__INLINE_FUNCTIONS__

# ION wants MALLOC_PC
CCFLAGS_alloc_ion = -DMALLOC_PC

#what to do when someone does printf("%s",null). default behavior is to sigsegv
CCFLAGS_stdio_qss = -DSTDIO_OPT_PERCENT_S_NULL=NULL

#ION and IOX want printf("%s",NULL) to not sigsegv. 
CCFLAGS_stdio_ion = -DSTDIO_OPT_PERCENT_S_NULL=\"\(null\)\"
CCFLAGS_stdio_iox = -DSTDIO_OPT_PERCENT_S_NULL=\"\(null\)\" 


CCFLAGS += $(CCF_$(COMPILER_TYPE)_$(COMPILER_DRIVER)) \
		$(CCFLAGS_$(SECTION))	\
		$(CCFLAGS_$(COMPILER_TYPE)_$(SECTION))	\
		$(CCFLAGS_$(COMPILER_TYPE)_$(CPU)) \
		$(CCFLAGS_$(CPU))	\
		$(CCFLAGS_$(basename $@)) \
		$(CCFLAGS_$(basename $@)_$(CPU)) \
		$(CCFLAGS_$(basename $@)_$(COMPILER_TYPE)_$(COMPILER_DRIVER)_$(CPU)) \
		$(CCFLAGS_$(SECTION)_$(BUILDENV)) \
		-D_LIBC_SO_VERSION=$(LIBC_SO_VERSION) \
		-D_LIBCPP_SO_VERSION=$(LIBCPP_SO_VERSION) \
		-D_LIBM_SO_VERSION=$(LIBM_SO_VERSION)



ASFLAGS += $(ASFLAGS_$(SECTION))	\
		$(ASFLAGS_$(COMPILER_TYPE)_$(CPU)) \
		$(ASFLAGS_$(CPU))	\
		$(ASFLAGS_$(basename $@)) \
		$(ASFLAGS_$(basename $@)_$(CPU)) 
		

# Some sections we only want to build in certain environments	
BUILDENV_dllmgr = iox
BUILDENV_dllmisc = iox
BUILDENV_ldd = qss iox
ifeq ($(BUILDENV),)
BUILDENV=qss
endif
ifneq ($(filter $(BUILDENV), $(if $(BUILDENV_$(SECTION)),$(BUILDENV_$(SECTION)),$(BUILDENV))),$(BUILDENV))
OBJS=
endif


ifeq ($(COMPILER),wcc)
wcc_comp:=$(firstword $(CC_nto_x86_wcc))
ifeq ($(wcc_comp)$,)
OBJS=
else	
ifeq ($(wildcard $(wcc_comp)*),)
OBJS=
endif
endif
endif

	
all: $(OBJS)

install: all qinstall

qinstall  iclean:
	# Nothing to do
	
hinstall:
	# Nothing to do
	
spotless clean:
	$(RM_HOST) *.o $(DEFFILE) $(EXTRA_CLEAN)
	
ldd.o: ldd.c $(firstword $(wildcard ../relocs.ci* ../../relocs.ci*))

-include $(MKFILES_ROOT)/lint.mk
EXTRA_LINTFLAGS = lint.cfg
