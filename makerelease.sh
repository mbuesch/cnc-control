#!/bin/sh

srcdir="$(dirname "$0")"
[ "$(echo "$srcdir" | cut -c1)" = '/' ] || srcdir="$PWD/$srcdir"

die() { echo "$*"; exit 1; }

# Import the makerelease.lib
for path in $(echo "$PATH" | tr ':' ' '); do
	[ -f "$MAKERELEASE_LIB" ] && break
	MAKERELEASE_LIB="$path/makerelease.lib"
done
[ -f "$MAKERELEASE_LIB" ] && . "$MAKERELEASE_LIB" || die "makerelease.lib not found."

hook_get_version()
{
	local file="$1/firmware/cpu-firmware/main.h"
	local major="$(cat "$file" | grep -e 'VERSION_MAJOR' | head -n1 | cut -f2)"
	local minor="$(cat "$file" | grep -e 'VERSION_MINOR' | head -n1 | cut -f2)"
	version="$major.$minor"
}

hook_testbuild()
{
	cd "$1/firmware/cpu-firmware" && make
	cd "$1/firmware/coproc-firmware" && make
}

project=cnc-control
makerelease $@
