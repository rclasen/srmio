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
$libtoolize --copy --force
autoheader
automake --add-missing --copy --force-missing
autoconf

# hack to nuke rpath:
#  http://wiki.debian.org/RpathIssue
#  http://fedoraproject.org/wiki/RPath_Packaging_Draft
ed libtool > /dev/null <<EOF
,s|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=""|g
,s|^runpath_var=LD_RUN_PATH|runpath_var=DIE_RPATH_DIE|g
wq
EOF

