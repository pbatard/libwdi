INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = setdrv setdrv_gui

setdrv_SOURCES = setdrv.c
setdrv_CFLAGS = $(NO_CYGWIN) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
setdrv_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
setdrv_LDADD = -L../libwdi/.libs -lwdi

setdrv_gui_rc.o: setdrv_gui.rc resource.h
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(RCFLAGS) -i $< -o $@

setdrv_gui_SOURCES = setdrv_gui.c
setdrv_gui_CFLAGS = $(NO_CYGWIN) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
setdrv_gui_LDFLAGS = -mwindows $(NO_CYGWIN) $(AM_LDFLAGS) -static
setdrv_gui_LDADD = setdrv_gui_rc.o -L../libwdi/.libs -lwdi
