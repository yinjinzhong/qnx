ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

define PINFO
PINFO DESCRIPTION=APS: test/demo commnd for Adaptive Partition Scheduling cr684
endef

USEFILE=$(PROJECT_ROOT)/$(NAME).c

include $(MKFILES_ROOT)/qtargets.mk

