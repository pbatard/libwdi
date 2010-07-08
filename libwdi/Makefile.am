INCLUDES = -I$(top_srcdir)

BUILT_SOURCES = resource.h
noinst_PROGRAMS = embedder
noinst_EXES = embedder.exe
lib_LTLIBRARIES = libwdi.la
LIB_SRC = resource.h logging.h usbi.h installer.h libwdi.h logging.c libwdi.c

if OPT_M32
noinst_PROGRAMS += installer_x86
noinst_EXES += installer_x86.exe
installer_x86_SOURCES = installer.h installer.c
installer_x86_CFLAGS = -m32 $(NO_CYGWIN) $(AM_CFLAGS)
installer_x86_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
installer_x86_LDADD = -lsetupapi -lnewdev
endif

if OPT_M64
noinst_PROGRAMS += installer_x64
noinst_EXES += installer_x64.exe
installer_x64_CC = gcc $(NO_CYGWIN)
installer_x64_SOURCES = installer.h installer.c
installer_x64_CFLAGS = -m64 -D_WIN64 $(NO_CYGWIN) $(AM_CFLAGS)
installer_x64_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
installer_x64_LDADD = -lsetupapi -lnewdev
endif

embedder_SOURCES = embedder.h embedder_files.h embedder.c

EXTRA_DIST = $(LIB_SRC)

libwdi_rc.lo: libwdi.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@

libwdi_la_CFLAGS = $(NO_CYGWIN) $(VISIBILITY_CFLAGS) $(AM_CFLAGS)
libwdi_la_LDLAGS = $(NO_CYGWIN) $(AM_LDFLAGS)
libwdi_la_LIBADD = libwdi_rc.lo -lsetupapi -lole32
libwdi_la_SOURCES = $(LIB_SRC)

resource.h: $(noinst_EXES)
	./embedder.exe resource.h

clean-local:
	-rm -rf resource.h
