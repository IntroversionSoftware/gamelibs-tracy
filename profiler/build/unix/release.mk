CFLAGS := -O3
ifndef TRACY_NO_LTO
ifeq (1,$(shell $(CC) --version | grep clang > /dev/null && echo 1 || echo 0))
CFLAGS += -flto=thin
else
CFLAGS += -flto
endif
endif
DEFINES := -DNDEBUG
BUILD := release

include ../../../common/unix-release.mk

ifeq ($(LEGACY),1)
    include legacy.mk
else
    include build.mk
endif
