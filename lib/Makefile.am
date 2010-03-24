BUILT_SOURCES = resource.h
lib_LTLIBRARIES = libusb-wdi.la

LIB_SRC = resource.h infs.h usbi.h installer.h installer_library.h installer_library.c

EXTRA_DIST = $(LIB_SRC)

#if OS_WINDOWS
#OS_SRC = $(WINDOWS_USB_SRC)
#.rc.lo:
#	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@
#endif

libusb_wdi_la_CFLAGS = $(VISIBILITY_CFLAGS) $(AM_CFLAGS)
libusb_wdi_la_LIBADD = -L../../libusb-pbatard/libusb/.libs -lusb-1.0
libusb_wdi_la_SOURCES = $(LIB_SRC)

resource.h:
	../embedder/embedder.exe resource.h

clean-local:
	-rm -rf resource.h
