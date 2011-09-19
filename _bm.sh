#!/bin/sh
# Create and upload a Zadig release
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

zadig_version=1.1.1.142
target_dir=/e/dailies/libwdi

type -P git &>/dev/null || { echo "Git not found. Aborting." >&2; exit 1; }
type -P 7zr &>/dev/null || { echo "7-zip (7zr) executable not found. Aborting." >&2; exit 1; }

git clean -fdx
(glibtoolize --version) < /dev/null > /dev/null 2>&1 && LIBTOOLIZE=glibtoolize || LIBTOOLIZE=libtoolize
$LIBTOOLIZE --copy --force || exit 1
aclocal || exit 1
autoheader || exit 1
autoconf || exit 1
automake -a -c || exit 1
./configure --disable-shared --enable-toggable-debug --enable-examples-build --disable-debug --with-ddkdir="E:/WinDDK/7600.16385.0" --with-libusb0="D:/libusb-win32" --with-libusbk="D:/libusbK/bin"

cd libwdi
make -j2
cd ../examples
make zadig.exe
7zr a $target_dir/zadig_v$zadig_version.7z zadig.exe
cd ..

scp $target_dir/zadig_v$zadig_version.7z pbatard,libwdi@frs.sf.net:/home/pfs/project/l/li/libwdi/zadig
