#!/bin/sh

# This file is used *internally*  by the developers to produce the binaries
# It is not meant for general use

date=`date +%Y.%m.%d`

# standard Zadig
git clean -fdx
(glibtoolize --version) < /dev/null > /dev/null 2>&1 && LIBTOOLIZE=glibtoolize || LIBTOOLIZE=libtoolize
$LIBTOOLIZE --copy --force || exit 1
aclocal || exit 1
autoheader || exit 1
autoconf || exit 1
automake -a -c || exit 1
./configure --disable-shared --enable-toggable-debug --enable-examples-build --disable-debug --with-ddkdir="E:/WinDDK/7600.16385.0" --with-libusb0="D:/libusb-win32"
make -j2
target_dir=e:/dailies/libwdi/$date
mkdir -p $target_dir
cp -v examples/zadig.exe $target_dir

# libusb0 only Zadig with IA64
make clean
./configure --disable-shared --enable-toggable-debug --enable-examples-build --disable-debug --enable-ia64 --with-libusb0="D:/libusb-win32"
make -j2
cp -v examples/zadig.exe $target_dir/zadig0.exe
md5sum $target_dir/zadig.exe