#!/bin/sh
#
# Copyright (c) 2008 Rainer Clasen
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms described in the file LICENSE included in this
# distribution.
#


aclocal
libtoolize=libtoolize
if which glibtoolize > /dev/null 2>&1; then
	libtoolize=glibtoolize
fi
$libtoolize --copy --force --ltdl
autoheader
automake --add-missing --copy --force-missing
autoconf

