INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = installer

installer_SOURCES = installer.h installer.c
installer_LDFLAGS = $(AM_LDFLAGS) -mno-cygwin
installer_LDADD = -lsetupapi -lole32

