#!/bin/sh
# Changes the version number for Zadig
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

if [ ! -n "$1" ]; then
  echo "you must provide a version number (eg. 1.0.2)"
  exit 1
else
  MAJOR=`echo $1 | sed "s/\(.*\)[.].*[.].*/\1/"`
  MINOR=`echo $1 | sed "s/.*[.]\(.*\)[.].*/\1/"`
  MICRO=`echo $1 | sed "s/.*[.].*[.]\(.*\)/\1/"`
fi
case $MAJOR in *[!0-9]*) 
  echo "$MAJOR is not a number"
  exit 1
esac
case $MINOR in *[!0-9]*) 
  echo "$MINOR is not a number"
  exit 1
esac
case $MICRO in *[!0-9]*) 
  echo "$MICRO is not a number"
  exit 1
esac
echo "changing Zadig version to $MAJOR.$MINOR.$MICRO"

cat > cmd.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*.*,.*,.*,\(.*\)/ FILEVERSION @@MAJOR@@,@@MINOR@@,@@MICRO@@,\1/
s/^[ \t]*PRODUCTVERSION[ \t]*.*,.*,.*,\(.*\)/ PRODUCTVERSION @@MAJOR@@,@@MINOR@@,@@MICRO@@,\1/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*".*\..*\..*\.\(.*\)"/\1VALUE "FileVersion", "@@MAJOR@@.@@MINOR@@.@@MICRO@@.\2"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*".*\..*\..*\.\(.*\)"/\1VALUE "ProductVersion", "@@MAJOR@@.@@MINOR@@.@@MICRO@@.\2"/
s/^\(.*\)"Zadig \(.*\)\.\(.*\)"\(.*\)/\1"Zadig @@MAJOR@@.@@MINOR@@.@@MICRO@@.\3"\4/
_EOF

sed -i -e "s/@@MAJOR@@/$MAJOR/g" -e "s/@@MINOR@@/$MINOR/g" -e "s/@@MICRO@@/$MICRO/g" cmd.sed
sed -i -f cmd.sed examples/zadig.rc
sed -i 's/$/\r/' examples/zadig.rc
sed -i -f cmd.sed examples/zadig.h
sed -i 's/$/\r/' examples/zadig.h

rm cmd.sed

# Update VID data while we're at it
cd libwdi
. vid_data.sh
cd ..
