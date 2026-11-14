# Mesa gallium driver for PlayStation Vita SGX543MP4+

LIB = psp2sgx

SRCS = psp2sgx_gallium.c

# Include paths
CFLAGS += -I$(TOP_SRCDIR)/mesa/include
CFLAGS += -I$(TOP_SRCDIR)/mesa/src/mapi
CFLAGS += -I$(TOP_SRCDIR)/mesa/src/gallium/include
CFLAGS += -I$(TOP_SRCDIR)/mesa/src/util
CFLAGS += -I$(TOP_SRCDIR)/mesa/src/compiler

# Kernel DRM headers
CFLAGS += -I$(SUBDIR)/../..
CFLAGS += -I$(SUBDIR)/../../../..

# Link against Mesa base libraries
LIBS += -lmesau
LIBS += -lglapi
LIBS += -ldrm

# Debug flags
DEBUG ?= 0
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
endif

# Architecture
ARCH ?= $(SUBARCH)

.include <bsd.subdir.mk>