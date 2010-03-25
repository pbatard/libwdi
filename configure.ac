AC_INIT([libusb-wdi], [1.0.0])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([lib/installer_library.c])
AC_CONFIG_MACRO_DIR([m4])
AM_CONFIG_HEADER([config.h])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PREREQ([2.50])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_C_INLINE
AM_PROG_CC_C_O
AC_DEFINE([_GNU_SOURCE], [], [Use GNU extensions])
AC_DEFINE([OPT_M32], [], [32 bit support])
AC_DEFINE([OPT_M64], [], [64 bit support])

# TODO: better CFLAGS

AC_MSG_CHECKING([development environment])
case $host in
*-mingw*)
	AC_MSG_RESULT([MinGW])
	AM_CFLAGS="-Wshadow"	
	;;
*-cygwin*)
	AC_MSG_RESULT([cygwin])
	AM_CFLAGS=""	
	# -mno-cygwin
	save_CFLAGS="${CFLAGS}"
	CFLAGS="${CFLAGS} -mno-cygwin"
	AC_MSG_CHECKING([if -mno-cygwin is supported])
	AC_TRY_COMPILE(, [;],
		has_mno_cygwin=yes,
		has_mno_cygwin=no)
	if test "x$has_mno_cygwin" = "xyes"; then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([you must ensure that -mno-cygwin is available on cygwin])
	fi
	CFLAGS="${save_CFLAGS}"	
	;;	
*)
	AC_MSG_ERROR([unsupported development environment])
esac

#LIBS="-lsetupapi -lole32"
AM_LDFLAGS="-no-undefined -avoid-version"
AC_CHECK_TOOL(RC, windres, no)

# 32 bit support
save_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -m32"
AC_MSG_CHECKING([whether the compiler can produce 32 bit binaries])
AC_TRY_COMPILE(, [;],
	compiler_has_m32=yes,
	compiler_has_m32=no)
if test "x$compiler_has_m32" = "xyes"; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi
CFLAGS="${save_CFLAGS}"
AM_CONDITIONAL([OPT_M32], [test "x$compiler_has_m32" == "xyes"])

# 64 bit support
save_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} -m64"
AC_MSG_CHECKING([whether the compiler can produce 64 bit binaries])
AC_TRY_COMPILE(, [;],
	compiler_has_m64=yes,
	compiler_has_m64=no)
if test "x$compiler_has_m64" = "xyes"; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi
CFLAGS="${save_CFLAGS}"
AM_CONDITIONAL([OPT_M64], [test "x$compiler_has_m64" == "xyes"])

# Message logging
AC_ARG_ENABLE([log], [AS_HELP_STRING([--disable-log], [disable all logging])],
	[log_enabled=$enableval],
	[log_enabled='yes'])
if test "x$log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_LOGGING], 1, [Message logging])
fi

# --enable-debug : check whether they want to have debug symbols:
AC_ARG_ENABLE(debug, AS_HELP_STRING([--disable-debug], [disable debugging]),
	[debug_enabled=$enableval],
	[debug_enabled='yes'])
if test "x$debug_enabled" = "xyes" ; then
  CFLAGS="-g -O2"
else
  CFLAGS="-O2"
fi

AC_ARG_ENABLE([debug-log], [AS_HELP_STRING([--enable-debug-log],
	[enable debug logging (default n)])],
	[debug_log_enabled=$enableval],
	[debug_log_enabled='no'])
if test "x$debug_log_enabled" != "xno"; then
	AC_DEFINE([ENABLE_DEBUG_LOGGING], 1, [Debug message logging])
fi

# Examples build
AC_ARG_ENABLE([examples-build], [AS_HELP_STRING([--enable-examples-build],
	[build example applications (default n)])],
	[build_examples=$enableval],
	[build_examples='no'])
AM_CONDITIONAL([BUILD_EXAMPLES], [test "x$build_examples" != "xno"])

# check for -Wno-pointer-sign compiler support (GCC >= 4)
saved_cflags="$CFLAGS"
CFLAGS="$CFLAGS -Wno-pointer-sign"
AC_COMPILE_IFELSE(AC_LANG_PROGRAM([]),
	nopointersign_cflags="-Wno-pointer-sign", nopointersign_cflags="")
CFLAGS="$saved_cflags"

AM_CFLAGS="$AM_CFLAGS -std=gnu99 -Wall -Wundef -Wunused -Wstrict-prototypes -Werror-implicit-function-declaration $nopointersign_cflags"

AC_SUBST(VISIBILITY_CFLAGS)
AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_CONFIG_FILES([Makefile] [embedder/Makefile] [installer/Makefile] [lib/Makefile] [examples/Makefile])
AC_OUTPUT
