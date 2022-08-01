AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = zadig wdi-simple

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

zadig_rc.lo: zadig.rc zadig_resource.h
	$(pkg_v_rc)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

zadig_SOURCES = zadig.c zadig_net.c zadig_parser.c zadig_stdlg.c profile.c
zadig_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
zadig_LDFLAGS = -mwindows $(AM_LDFLAGS) -static
zadig_LDADD = zadig_rc.lo -L../libwdi/.libs -lwdi -lwininet -luuid -lcomctl32

wdi-simple_rc.lo: wdi-simple.rc
	$(pkg_v_rc)$(LIBTOOL) $(AM_V_lt) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile $(RC) $(ARCH_RCFLAGS) -i $< -o $@

wdi_simple_SOURCES = wdi-simple.c
wdi_simple_CFLAGS = -I../libwdi $(ARCH_CFLAGS) $(AM_CFLAGS)
wdi_simple_LDFLAGS = $(AM_LDFLAGS) -static
wdi_simple_LDADD = wdi-simple_rc.lo -L../libwdi/.libs -lwdi
