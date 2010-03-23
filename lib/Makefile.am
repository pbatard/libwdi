lib_LTLIBRARIES = libusb-wdi.la

LIB_SRC = resource.h infs.h usbi.h installer.h installer_library.h installer_library.c

EXTRA_DIST = $(LIB_SRC)

#if OS_WINDOWS
#OS_SRC = $(WINDOWS_USB_SRC)
#.rc.lo:
#	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@
#endif

libusb_wdi_la_CFLAGS = $(VISIBILITY_CFLAGS) $(AM_CFLAGS) -lusb-1.0 -L/d/libusb-pbatard/libusb/.libs
libusb_wdi_la_SOURCES = $(LIB_SRC)

#???
#hdrdir = $(includedir)/libusb-1.0
#hdr_HEADERS = libusb.h
