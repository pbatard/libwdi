#!/bin/sh
# Create and upload a Zadig release
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

zadig_version=2.1.0.163
target_dir=/e/dailies/libwdi

type -P git &>/dev/null || { echo "Git not found. Aborting." >&2; exit 1; }
type -P upx &>/dev/null || { echo "UPX executable not found. Aborting." >&2; exit 1; }

# Build Zadig for Vista and later (KMDF v1.11)
git clean -fdx
./autogen.sh --disable-shared --with-wdfver=1011

cd libwdi
make -j2
cd ../examples
make -j2 zadig.exe
# For the app icon to show during UAC, the app needs to have SYSTEM access which MinGW may not grant by default
# (NB this only matters for local apps - an app extracted from a 7z will always have SYSTEM access)
# SetACL can be downloaded from http://helgeklein.com/
type -P SetACL &>/dev/null && { SetACL -on ./zadig.exe -ot file -actn ace -ace "n:S-1-5-18;p:read,read_ex;s:y"; }
upx --lzma zadig.exe
cp zadig.exe $target_dir/zadig.exe
cd ..

# Build Zadig for XP (KMDF v1.09)
git clean -fdx
./autogen.sh --disable-shared --with-wdfver=1009

cd libwdi
make -j2
cd ../examples
make -j2 zadig.exe
type -P SetACL &>/dev/null && { SetACL -on ./zadig.exe -ot file -actn ace -ace "n:S-1-5-18;p:read,read_ex;s:y"; }
upx --lzma zadig.exe
cp zadig.exe $target_dir/zadig_xp.exe
cmd.exe /k zadig_sign.bat "$target_dir/zadig.exe" "$target_dir/zadig_xp.exe"
cd ..
