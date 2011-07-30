#!/bin/bash
set -e

project="cnc-control"

basedir="$(dirname "$0")"
[ "${basedir:0:1}" = "/" ] || basedir="$PWD/$basedir"

origin="$basedir"
tmpdir="/tmp"

do_git_tag=1
[ "$1" = "--notag" ] && do_git_tag=0

major="$(cat $origin/firmware/cpu-firmware/main.h | grep -e 'VERSION_MAJOR' | head -n1 | cut -f2)"
minor="$(cat $origin/firmware/cpu-firmware/main.h | grep -e 'VERSION_MINOR' | head -n1 | cut -f2)"
version="$major.$minor"
if [ -z "$major" -o -z "$minor" ]; then
	echo "Could not determine version!"
	exit 1
fi
release_name="$project-$version"
tarball="$release_name.tar.bz2"
tagname="release-$version"
tagmsg="$project-$version release"

export GIT_DIR="$origin/.git"

cd "$tmpdir"
rm -Rf "$release_name" "$tarball"
echo "Creating target directory"
mkdir "$release_name"
cd "$release_name"
echo "git checkout"
git checkout -f

find "$tmpdir/$release_name" -name makerelease.sh | xargs rm
find "$tmpdir/$release_name" -name .gitignore | xargs rm

echo "creating tarball"
cd "$tmpdir"
tar cjf "$tarball" "$release_name"
mv "$tarball" "$origin"

echo "running CPU testbuild"
cd "$tmpdir/$release_name/firmware/cpu-firmware"
make
echo "running COPROC testbuild"
cd "$tmpdir/$release_name/firmware/coproc-firmware"
make

echo "removing testbuild"
cd "$tmpdir"
rm -R "$release_name"

if [ "$do_git_tag" -ne 0 ]; then
	echo "Tagging GIT"
	cd "$origin"
	git tag -m "$tagmsg" -a "$tagname"
fi

echo
echo "built release $version"
