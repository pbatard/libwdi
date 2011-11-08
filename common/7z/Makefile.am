lib_LTLIBRARIES = lib7z.la

lib7z_la_CFLAGS = $(ARCH_CFLAGS) $(LIBCONFIG_CFLAGS) $(VISIBILITY_CFLAGS) -std=gnu99 -Wall -Wundef -Wunused -Werror-implicit-function-declaration
lib7z_la_CFLAGS += -D_7ZIP_ST
lib7z_la_LDLAGS = $(NO_CYGWIN) $(AM_LDFLAGS)
lib7z_la_LIBADD = $(LIBCONFIG_LIBADD)
lib7z_la_SOURCES = 7zAlloc.c 7zBuf.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zFile.c 7zIn.c 7zStream.c \
  Bcj2.c Bra.c Bra86.c CpuArch.c LzFind.c Lzma2Dec.c Lzma2Enc.c LzmaDec.c LzmaEnc.c