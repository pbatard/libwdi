#!/bin/sh
# Create a Zadig release
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

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
md5sum $target_dir/zadig.exe