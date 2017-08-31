ifeq ($(NOMODULE_SUPPORT),)
#Use standard linker with "-r" so that modules work
LDBOOTSTRAP_nto_x86_gcc=$(LR_nto_x86_gcc) -nostartfiles -u_start
LDBOOTSTRAP_nto_x86_gcc_qcc=$(LR_nto_x86_gcc_qcc) -nostartup -Wl,-u_start
LDBOOTSTRAP_nto_x86_wcc=$(LDBOOTSTRAP_nto_x86_gcc_qcc)
else	
CHECK_FOR_UNDEFS=$(ECHO_HOST) "Not checking for unresolveds on x86"
endif

ifeq ($(COMPILER),$(NOWATCOM))
wcc_comp:=$(firstword $(CC_nto_x86_wcc))
ifneq ($(wcc_comp)$,)
ifneq ($(wildcard $(wcc_comp)*),)
select_compiler := _nto_x86_wcc
DEFCOMPILER_TYPE_x86=wcc
endif
endif
endif
ASSEMBLER_TYPE_x86=gcc
ASMOFF_FORMAT_x86=cpp

ifneq ($(origin BUILD_SRC_ROOT),undefined)
# On the build machine
ifeq ($(COMPILER)-$(COMPILER_TYPE),-gcc)
# In an "o" directory and not using Watcom - don't compile anything
$(FULLNAME):
	# nothing to do
FULLNAME=/dev/null
endif
endif


CCFLAGS += -D_PADDR_BITS=64

CC_nto_x86_gcc += -O2 -fomit-frame-pointer -Wpointer-arith
CC_nto_x86_gcc_qcc += -O2 -Wc,-fomit-frame-pointer -Wc,-Wpointer-arith
