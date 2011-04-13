#!/bin/sh
# Changes the version number
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
echo "changing version to $MAJOR.$MINOR.$MICRO"
sed -e "s/^AC_INIT(\[\([^ ]*\)\], \[[^ ]*\]\(.*\)/AC_INIT([\1], [$MAJOR.$MINOR.$MICRO]\2/" configure.ac > configure.ac~
mv configure.ac~ configure.ac
cat > cmd.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*.*,.*,.*,\(.*\)/ FILEVERSION @@MAJOR@@,@@MINOR@@,@@MICRO@@,\1/
s/^[ \t]*PRODUCTVERSION[ \t]*.*,.*,.*,\(.*\)/ PRODUCTVERSION @@MAJOR@@,@@MINOR@@,@@MICRO@@,\1/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*".*,[ \t]*.*,[ \t]*.*,\(.*\)"/\1VALUE "FileVersion", "@@MAJOR@@, @@MINOR@@, @@MICRO@@,\2"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*".*,[ \t]*.*,[ \t]*.*,\(.*\)"/\1VALUE "ProductVersion", "@@MAJOR@@, @@MINOR@@, @@MICRO@@,\2"/
s/^\(.*\)"Zadig, Version \(.*\)\.\(.*\)"\(.*\)/\1"Zadig, Version @@MAJOR@@.@@MINOR@@.@@MICRO@@.\3"\4/
s/^zadig_version=.*\.\(.*\)/zadig_version=@@MAJOR@@.@@MINOR@@.@@MICRO@@.\1/
_EOF

# First run sed to substitute our variable in the sed command file
sed -e "s/@@MAJOR@@/$MAJOR/g" -e "s/@@MINOR@@/$MINOR/g" -e "s/@@MICRO@@/$MICRO/g" cmd.sed > cmd.sed~
mv cmd.sed~ cmd.sed
# Run sed to update the .rc files minor version
sed -f cmd.sed libwdi/libwdi.rc > libwdi/libwdi.rc~
mv libwdi/libwdi.rc~ libwdi/libwdi.rc
sed -f cmd.sed examples/zadic.rc > examples/zadic.rc~
mv examples/zadic.rc~ examples/zadic.rc
sed -f cmd.sed examples/zadig.rc > examples/zadig.rc~
mv examples/zadig.rc~ examples/zadig.rc
sed -f cmd.sed examples/wdi-simple.rc > examples/wdi-simple.rc~
mv examples/wdi-simple.rc~ examples/wdi-simple.rc
sed -f cmd.sed _bm.sh > _bm.sh~
mv _bm.sh~ _bm.sh
rm cmd.sed
