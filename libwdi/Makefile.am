INCLUDES = -I$(top_srcdir)

BUILT_SOURCES = resource.h
noinst_PROGRAMS = embedder installer_x86 installer_x64
lib_LTLIBRARIES = libwdi.la
LIB_SRC = resource.h logging.h usbi.h installer.h libwdi.h logging.c libwdi.c

# TODO: warn if _X86 or _X64 is missing
if OPT_M32
M32 = -m32
endif
if OPT_M64
M64 = -m64 -D_WIN64
endif

embedder_SOURCES = embedder.c

installer_x86_SOURCES = installer.h installer.c
installer_x86_LDFLAGS = $(AM_LDFLAGS) -static
installer_x86_CFLAGS = $(AM_CFLAGS) $(M32)
installer_x86_LDADD = -lsetupapi -lole32

installer_x64_SOURCES = installer.h installer.c
installer_x64_LDFLAGS = $(AM_LDFLAGS) -static
installer_x64_CFLAGS = $(AM_CFLAGS) $(M64)
installer_x64_LDADD = -lsetupapi -lole32

EXTRA_DIST = $(LIB_SRC)

#if OS_WINDOWS
#OS_SRC = $(WINDOWS_USB_SRC)
#.rc.lo:
#	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@
#endif

libwdi_la_CFLAGS = $(VISIBILITY_CFLAGS) $(AM_CFLAGS)
libwdi_la_LIBADD = -lsetupapi -lole32
libwdi_la_SOURCES = $(LIB_SRC)

resource.h: embedder.exe installer_x86.exe installer_x64.exe
	./embedder.exe resource.h

clean-local:
	-rm -rf resource.h
