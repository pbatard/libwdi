INCLUDES = -I$(top_srcdir)

BUILT_SOURCES = embedded.h
noinst_PROGRAMS =
noinst_EXES =
lib_LTLIBRARIES = libwdi.la
LIB_SRC = resource.h logging.h tokenizer.h installer.h libwdi.h logging.c tokenizer.c vid_data.c libwdi_dlg.c libwdi.c

if OPT_M32
noinst_PROGRAMS += installer_x86
noinst_EXES += installer_x86.exe
installer_x86_SOURCES = installer.h installer.c
installer_x86_CFLAGS = -m32 $(NO_CYGWIN) $(AM_CFLAGS)
installer_x86_LDFLAGS = -m32 $(NO_CYGWIN) $(AM_LDFLAGS) -static
installer_x86_LDADD = -lsetupapi -lnewdev
endif

if OPT_M64
noinst_PROGRAMS += installer_x64
noinst_EXES += installer_x64.exe
installer_x64_SOURCES = installer.h installer.c
installer_x64_CFLAGS = -m64 -D_WIN64 $(NO_CYGWIN) $(AM_CFLAGS)
installer_x64_LDFLAGS = -m64 $(NO_CYGWIN) $(AM_LDFLAGS) -static
installer_x64_LDADD = -lsetupapi -lnewdev
endif

# call host's gcc to allow 64 bit cross compilation from a 32 bit Windows host
# as we can't use $(CC), we assert that the host's default compiler is always gcc
embedder: embedder.h embedder_files.h embedder.c
	gcc -o $@ -I.. embedder.c -lversion 

EXTRA_DIST = $(LIB_SRC)

libwdi_rc.lo: libwdi.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

libwdi_la_CFLAGS = $(ARCH_CFLAGS) $(NO_CYGWIN) $(VISIBILITY_CFLAGS) $(AM_CFLAGS)
libwdi_la_LDLAGS = $(NO_CYGWIN) $(AM_LDFLAGS)
libwdi_la_LIBADD = libwdi_rc.lo -lsetupapi -lole32
libwdi_la_SOURCES = $(LIB_SRC)

embedded.h: embedder $(noinst_PROGRAMS)
	./embedder.exe embedded.h

clean-local:
	-rm -rf embedded.h embedder.exe
