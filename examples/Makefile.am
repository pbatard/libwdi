SUBDIRS = libconfig

INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = zadic zadig inf-wizard

zadic_rc.o: zadic.rc resource.h
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

zadic_SOURCES = zadic.c
zadic_CFLAGS = $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
zadic_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
zadic_LDADD = zadic_rc.o -L../libwdi/.libs -lwdi

zadig_rc.o: zadig.rc resource.h
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

zadig_SOURCES = zadig.c zadig_stdlg.c
zadig_CFLAGS = $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
zadig_LDFLAGS = -mwindows $(NO_CYGWIN) $(AM_LDFLAGS) -static
zadig_LDADD = zadig_rc.o -L../libwdi/.libs -L./libconfig/.libs -lwdi -lconfig

inf_wizard_rc.o: inf_wizard_rc.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

inf_wizard_SOURCES = inf_wizard.c
inf_wizard_CFLAGS = $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
inf_wizard_LDFLAGS = -mwindows $(NO_CYGWIN) $(AM_LDFLAGS) -static
inf_wizard_LDADD = inf_wizard_rc.o -L../libwdi/.libs -lwdi -lcomctl32
