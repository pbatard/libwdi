#!/bin/sh
# Create and upload a Zadig release
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

type -P git &>/dev/null || { echo "Git not found. Aborting." >&2; exit 1; }
type -P sed &>/dev/null || { echo "Sed not found. Aborting." >&2; exit 1; }
type -P upx &>/dev/null || { echo "UPX executable not found. Aborting." >&2; exit 1; }

git clean -fdx
./autogen.sh --disable-shared

zadig_version=`sed -n 's/^.*\"FileVersion\", \"\(.*\)\..*\"/\1/p' examples/zadig.rc`
echo Building Zadig v$zadig_version...

make -j12
make zadig_release