INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = installer_x86 installer_x64

# TODO: warn if _X86 or _X64 is missing
if OPT_M32
M32 = -m32
endif
if OPT_M64
M64 = -m64
endif

installer_x86_SOURCES = installer.h installer.c
installer_x86_LDFLAGS = $(AM_LDFLAGS) -mno-cygwin -static
installer_x86_CFLAGS = $(AM_CFLAGS) $(M32)
installer_x86_LDADD = -lsetupapi -lole32

installer_x64_SOURCES = installer.h installer.c
installer_x64_LDFLAGS = $(AM_LDFLAGS) -mno-cygwin -static
installer_x64_CFLAGS = $(AM_CFLAGS) $(M64)
installer_x64_LDADD = -lsetupapi -lole32
