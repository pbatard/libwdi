AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = zadig wdi-simple

AM_V_WINDRES_0 = @echo "  RC     $@";$(WINDRES)
AM_V_WINDRES_1 = $(WINDRES)
AM_V_WINDRES_  = $(AM_V_WINDRES_$(AM_DEFAULT_VERBOSITY))
AM_V_WINDRES   = $(AM_V_WINDRES_$(V))

%_rc.o: %.rc
	$(AM_V_WINDRES) $(AM_RCFLAGS) -i $< -o $@

zadig_SOURCES = zadig.c zadig_net.c zadig_parser.c zadig_stdlg.c profile.c
zadig_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
zadig_LDFLAGS = -mwindows $(AM_LDFLAGS) -static
zadig_LDADD = zadig_rc.o -L../libwdi/.libs -lwdi -lwininet -luuid -lcomctl32

wdi_simple_SOURCES = wdi-simple.c
wdi_simple_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
wdi_simple_LDFLAGS = $(AM_LDFLAGS) -static
wdi_simple_LDADD = wdi-simple_rc.o -L../libwdi/.libs -lwdi
