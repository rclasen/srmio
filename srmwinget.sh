#!/bin/sh
#
# Copyright (c) 2008 Rainer Clasen
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms described in the file LICENSE included in this
# distribution.
#

# Example script to retrieve files into a srmwin compatible directory tree
# Note: this is mostly untested.

set -e

: ${srmbase:="$HOME/.wine/drive_c/Program Files/SRM Training System/_data.srm"}
: ${srmdev:="/dev/ttyUSB1"}


# get file
tmp=`tempfile -d "$srmbase" -s ".srm" `
srmcmd $srmopt -g -w "$tmp" "$srmdev"

# build filename
athlete=`srmcmd -n -r "$tmp"`
time=`srmcmd -d -r "$tmp"`
dir="$srmbase/_$athlete.SRM/`date -d"@$time" +"%Y_%m"`.SRM"
[ -d "$dir" ] || mkdir -p "$dir"

# note: somehow depends on the selected time format:
fname=`date -d"@$time" +"r%d%m%y"`
path=""
for x in A B C D E F G H I J; do
	path="$dir/$fname$x.srm"
	if [ -r "$path" ]; then
		path=""
	else
		break;
	fi
done
if [ -z "$path" ]; then
	echo "cannot find unused filename for >$dir/$fname<">&2
	exit 1
fi

# move
mv "$tmp"  "$path"

