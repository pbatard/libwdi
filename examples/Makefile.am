INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = setdrv

setdrv_SOURCES = setdrv.c
# static ensures that the exe can be shared as a standalone, and still allow driver installation
setdrv_LDFLAGS = $(AM_LDFLAGS) -static
setdrv_LDADD = -L../../libusb-pbatard/libusb/.libs -lusb-1.0 -L../lib/.libs -lusb-wdi
