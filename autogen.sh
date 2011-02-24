#!/bin/sh

# rebuilds the Windows def file by exporting all LIBDWI API calls
create_def()
{
  echo "rebuidling libwdi.def file"
  echo "LIBRARY" > libwdi/libwdi.def
  echo "EXPORTS" >> libwdi/libwdi.def
  sed -n -e "s/.*LIBWDI_API.*\([[:blank:]]\)\(wdi.*\)(.*/  \2/p" libwdi/libwdi.c libwdi/vid_data.c libwdi/logging.c >> libwdi/libwdi.def
  # We need to manually define a whole set of DLL aliases if we want the MS
  # DLLs to be usable with dynamically linked MinGW executables. This is
  # because it is not possible to avoid the @ decoration from import WINAPI
  # calls in MinGW generated objects, and .def based MS generated DLLs don't
  # have such a decoration => linking to MS DLL will fail without aliases.
  # Currently, the maximum size is 16 and all sizes are multiples of 4
  for i in 4 8 12 16
  do
    sed -n -e "s/.*LIBWDI_API.*\([[:blank:]]\)\(wdi.*\)(.*/  \2@$i = \2/p" libwdi/libwdi.c libwdi/vid_data.c libwdi/logging.c >> libwdi/libwdi.def
  done
}

# use glibtoolize if it is available
(glibtoolize --version) < /dev/null > /dev/null 2>&1 && LIBTOOLIZE=glibtoolize || LIBTOOLIZE=libtoolize

$LIBTOOLIZE --copy --force || exit 1
# Force ltmain's NLS test to set locale to C always. Prevents an
# issue when compiling shared libs with MinGW on Chinese locale.
type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
sed -e s/\\\\\${\$lt_var+set}/set/g ltmain.sh > lttmp.sh
mv lttmp.sh ltmain.sh
#
aclocal || exit 1
autoheader || exit 1
autoconf || exit 1
automake -a -c || exit 1
./configure --enable-toggable-debug --enable-examples-build --disable-debug --with-ddkdir="E:/WinDDK/7600.16385.0" --with-libusb0="D:/libusb-win32" --with-libusbk="D:/lusbw" $*
# rebuild .def, if sed is available
type -P sed &>/dev/null && create_def