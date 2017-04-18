AUTOMAKE_OPTIONS = dist-bzip2 no-dist-gzip
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = libwdi

if BUILD_EXAMPLES
SUBDIRS += examples
ZADIG_VERSION = $(shell sed -n 's/^.*\"FileVersion\", \"\(.*\)\..*\"/\1/p' examples/zadig.rc)
endif

# This step produces the UPX compressed and signed releases that are made available for public download
# NB: UPX v3.09 or later is needed for LZMA compression (http://upx.sourceforge.net/)
zadig_release:
	@cp examples/zadig.exe ./zadig-$(ZADIG_VERSION).exe
	@upx --lzma zadig-$(ZADIG_VERSION).exe
	@cmd.exe //c _sign.cmd zadig-$(ZADIG_VERSION).exe
