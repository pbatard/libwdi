AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = zadic zadig wdi-simple

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

zadic_rc.o: zadic.rc
	$(pkg_v_rc)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

zadic_SOURCES = zadic.c
zadic_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
zadic_LDFLAGS = $(AM_LDFLAGS) -static
zadic_LDADD = zadic_rc.o -L../libwdi/.libs -lwdi

zadig_rc.o: zadig.rc zadig_resource.h
	$(pkg_v_rc)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

zadig_SOURCES = zadig.c zadig_net.c zadig_parser.c zadig_stdlg.c profile.c
zadig_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
zadig_LDFLAGS = -mwindows $(AM_LDFLAGS) -static
zadig_LDADD = zadig_rc.o -L../libwdi/.libs -lwdi -lwininet -luuid -lcomctl32

wdi-simple_rc.o: wdi-simple.rc
	$(pkg_v_rc)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

wdi_simple_SOURCES = wdi-simple.c
wdi_simple_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
wdi_simple_LDFLAGS = $(AM_LDFLAGS) -static
wdi_simple_LDADD = wdi-simple_rc.o -L../libwdi/.libs -lwdi
