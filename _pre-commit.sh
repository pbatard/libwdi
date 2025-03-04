#!/bin/sh
#
# Bumps the nano version according to the number of commits on this branch
#
# To have git run this script on commit, create a "pre-commit" text file in
# .git/hooks/ with the following content:
# #!/bin/sh
# if [ -x ./_pre-commit.sh ]; then
# 	. ./_pre-commit.sh
# fi

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

if [ -x ./_detect-amend.sh ]; then
	. ./_detect-amend.sh
fi

BUILD=`git rev-list HEAD --count`
# adjust so that we match the github commit count
((BUILD++))
# there may be a better way to prevent improper nano on amend. For now the detection
# of a .amend file in the current directory will do
if [ -f ./.amend ]; then
	((BUILD--))
	rm ./.amend;
fi
echo "setting nano to $BUILD"

cat > _library.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ FILEVERSION \1,\2,\3,@@BUILD@@/
s/^[ \t]*PRODUCTVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ PRODUCTVERSION \1,\2,\3,@@BUILD@@/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*"\(.*\)\..*"/\1VALUE "FileVersion", "\2.@@BUILD@@"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*"\(.*\)\..*"/\1VALUE "ProductVersion", "\2.@@BUILD@@"/
s/\xef\xbf\xbd/\xa9/
_EOF

cat > _zadig.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*\(.*\),\(.*\),.*,0/ FILEVERSION \1,\2,@@BUILD@@,0/
s/^[ \t]*PRODUCTVERSION[ \t]*\(.*\),\(.*\),.*,0/ PRODUCTVERSION \1,\2,@@BUILD@@,0/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*"\(.*\)\..*"/\1VALUE "FileVersion", "\2.@@BUILD@@"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*"\(.*\)\..*"/\1VALUE "ProductVersion", "\2.@@BUILD@@"/
s/^\(.*\)"Zadig \(.*\)\..*"\(.*\)/\1"Zadig \2.@@BUILD@@"\3/
s/\xef\xbf\xbd/\xa9/
_EOF

# First run sed to substitute our variable in the sed command file
sed -i -e "s/@@BUILD@@/$BUILD/g" _library.sed
sed -i -e "s/@@BUILD@@/$BUILD/g" _zadig.sed

# Run sed to update the nano version, and add the modified files
sed -b -i -f _library.sed libwdi/libwdi.rc
sed -b -i -f _library.sed examples/wdi-simple.rc
# SED is an ass when it comes to replacing at the end of a line and preserving the EOL sequence
unix2dos -q libwdi/libwdi.rc
unix2dos -q examples/wdi-simple.rc
sed -b -i -f _zadig.sed examples/zadig.rc
sed -b -i -f _zadig.sed examples/zadig.h
rm _library.sed _zadig.sed
git add libwdi/libwdi.rc examples/zadig.rc examples/zadig.h examples/wdi-simple.rc
