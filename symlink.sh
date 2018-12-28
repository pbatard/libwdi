#!/usr/bin/bash

src=$1
dest=$2

if [[ "$src" == "" ]]
then
  bin=`basename $0`
  echo "Usage: $bin <src> <dest>"
  echo "<src> and <dest> are relative to pwd."
  exit 0
fi

prefix=`git rev-parse --show-prefix`
hash=`echo -n "$src" | git hash-object -w --stdin`

git update-index --add --cacheinfo 120000 "$hash" "$prefix$dest"
git checkout -- "$dest"
