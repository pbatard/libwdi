lib_LTLIBRARIES = libconfig.la

libconfig_la_CFLAGS = $(VISIBILITY_CFLAGS) $(AM_CFLAGS) -UHAVE_CONFIG_H -Wno-strict-prototypes
libconfig_la_LDLAGS = $(AM_LDFLAGS)
#libconfig_la_LIBADD = libwdi_rc.lo -lsetupapi -lole32
libconfig_la_SOURCES = libconfig.c scanner.c grammar.c parsectx.h wincompat.h \
	scanctx.c scanctx.h strbuf.c strbuf.h

