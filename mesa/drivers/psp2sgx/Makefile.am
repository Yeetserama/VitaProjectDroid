# Makefile.am for psp2sgx gallium driver

noinst_LIBRARY = libpsp2sgx.la

libpsp2sgx_la_SOURCES = \
	psp2sgx_gallium.c \
	psp2sgx_driver.c \
	psp2sgx_gallium.h

libpsp2sgx_la_LDFLAGS = -version-info 0:0:0

# Compiler flags
AM_CFLAGS = \
	-I$(top_srcdir)/mesa/include \
	-I$(top_srcdir)/mesa/src/gallium/include \
	-I$(top_srcdir)/mesa/src/util \
	-I$(top_srcdir)/mesa/src/compiler \
	-I$(top_srcdir)/mesa/src/mapi

# Link against base Mesa libraries
libpsp2sgx_la_LIBADD = \
	-lmesau \
	-lglapi \
	-ldrm

# Add this driver to the build if enabled
if ENABLE_PSP2SGX
  gallium_DRivers += psp2sgx
  BUILT_DRIVERS += libpsp2sgx.la
endif