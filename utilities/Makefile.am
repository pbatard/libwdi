bin2coff_OPTIONS =

if OPT_M64
if !OPT_M32
bin2coff_OPTIONS += 64bit
endif
endif

pkg_v_localcc = $(pkg_v_localcc_$(V))
pkg_v_localcc_ = $(pkg_v_localcc_$(AM_DEFAULT_VERBOSITY))
pkg_v_localcc_0 = @echo "  CCLD   $@";

# call host's CC to allow for cross compilation
bin2coff: bin2coff.c
	$(pkg_v_localcc)$(CC_FOR_BUILD) -Wall -I.. bin2coff.c -o $@

cathash: cathash.c
	$(pkg_v_localcc)$(CC_FOR_BUILD) -Wall -I.. cathash.c -o $@

embedder: bin2coff embedder.h embedder_files.h embedder.c
	bin2coff test.7z test.o compressed_data $(bin2coff_OPTIONS)
	$(pkg_v_localcc)$(CC_FOR_BUILD) -I.. -I../common/7z embedder.c test.o -L../common/7z/.libs -l7z -o $@

#embedded.h: embedder $(noinst_PROGRAMS)
#	@./embedder embedded.h

all: bin2coff cathash

clean-local:
	-rm -rf embedder embedder.exe bin2coff bin2coff.exe cathash cathash.exe
