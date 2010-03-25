BUILT_SOURCES = resource.h
lib_LTLIBRARIES = libwdi.la

LIB_SRC = resource.h logging.h usbi.h installer.h libwdi.h logging.c libwdi.c

EXTRA_DIST = $(LIB_SRC)

#if OS_WINDOWS
#OS_SRC = $(WINDOWS_USB_SRC)
#.rc.lo:
#	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@
#endif

libwdi_la_CFLAGS = $(VISIBILITY_CFLAGS) $(AM_CFLAGS)
libwdi_la_LIBADD = -lsetupapi -lole32
libwdi_la_SOURCES = $(LIB_SRC)

resource.h:
	../embedder/embedder.exe resource.h

clean-local:
	-rm -rf resource.h
