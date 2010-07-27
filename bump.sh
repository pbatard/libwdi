#!/bin/sh

# This script bumps the version and updates the rc files and git tree accordingly

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

TAG=$(git describe --tags --abbrev=0 2>/dev/null)
if [ ! -n "$TAG" ]; then
  echo Unable to read tag - aborting.
  exit 1
fi
if [ ! ${TAG:0:1} = 'w' ]; then
  echo Tag '$TAG' does not start with 'w' - aborting
  exit 1
fi
TAGVER=${TAG:1}
# increment - ideally, we'd check that tagver is really numeric here
TAGVER=`expr $TAGVER + 1`
echo Bumping version to w$TAGVER

cat > cmd.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ FILEVERSION \1,\2,\3,@@TAGVER@@/
s/^[ \t]*PRODUCTVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ PRODUCTVERSION \1,\2,\3,@@TAGVER@@/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*"\(.*\),[ \t]*\(.*\),[ \t]*\(.*\),.*"/\1VALUE "FileVersion", "\2, \3, \4, @@TAGVER@@"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*"\(.*\),[ \t]*\(.*\),[ \t]*\(.*\),.*"/\1VALUE "ProductVersion", "\2, \3, \4, @@TAGVER@@"/
_EOF

# First run sed to substitute our variable in the sed command file
sed -e "s/@@TAGVER@@/$TAGVER/g" cmd.sed > cmd_.sed
mv cmd_.sed cmd.sed

# Run sed to update the .rc files minor version
sed -f cmd.sed libwdi/libwdi.rc > libwdi/libwdi_.rc
mv libwdi/libwdi_.rc libwdi/libwdi.rc
sed -f cmd.sed examples/zadic.rc > examples/zadic_.rc
mv examples/zadic_.rc examples/zadic.rc
sed -f cmd.sed examples/zadig.rc > examples/zadig_.rc
mv examples/zadig_.rc examples/zadig.rc

rm cmd.sed

# Update VID data while we're at it
cd libwdi
. vid_data.sh
cd ..

git commit -a -m "bumped internal version"
git tag "w$TAGVER"