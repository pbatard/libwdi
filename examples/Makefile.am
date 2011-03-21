INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = zadic zadig inf-wizard wdi-simple

zadic_rc.o: zadic.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

zadic_SOURCES = zadic.c
zadic_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
zadic_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
zadic_LDADD = zadic_rc.o -L../libwdi/.libs -lwdi

zadig_rc.o: zadig.rc resource.h
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

zadig_SOURCES = zadig.c zadig_stdlg.c profile.c
zadig_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
zadig_LDFLAGS = -mwindows $(NO_CYGWIN) $(AM_LDFLAGS) -static
zadig_LDADD = zadig_rc.o -L../libwdi/.libs -lwdi

inf_wizard_rc.o: inf_wizard_rc.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

inf_wizard_SOURCES = inf_wizard.c
inf_wizard_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
inf_wizard_LDFLAGS = -mwindows $(NO_CYGWIN) $(AM_LDFLAGS) -static
inf_wizard_LDADD = inf_wizard_rc.o -L../libwdi/.libs -lwdi -lcomctl32

wdi-simple_rc.o: wdi-simple.rc
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(RC) $(ARCH_RCFLAGS) -static -i $< -o $@

wdi_simple_SOURCES = wdi-simple.c
wdi_simple_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(NO_CYGWIN) $(AM_CFLAGS)
wdi_simple_LDFLAGS = $(NO_CYGWIN) $(AM_LDFLAGS) -static
wdi_simple_LDADD = wdi-simple_rc.o -L../libwdi/.libs -lwdi
