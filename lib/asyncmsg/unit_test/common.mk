ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

USEFILE=

TESTS = $(basename $(notdir $(SRCS)))
INSTALLDIR=/dev/null
ICLEAN=$(TESTS)
ALL_DEPENDENCIES=$(TESTS)

LIBS += asyncmsg

EXTRA_LIBVPATH=$(PROJECT_ROOT)/../$(CPU)/a.be

include $(MKFILES_ROOT)/qtargets.mk

$(TESTS): %: %.o
	$(TARGET_BUILD)
