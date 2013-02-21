#!/bin/sh

srcdir="$1"
out="$2"

if [ -z "$srcdir" ]; then
	echo "missing source directory" >&2
	exit 1
fi

if [ -z "$out" ]; then
	echo "missing destination file" >&2
	exit 1
fi

last_commit="`cd "$srcdir" &&  git rev-parse --short --verify HEAD 2>/dev/null `"
if [ -n "$last_commit" ]; then
	echo "$last_commit" > "$out"
fi
