#!/bin/sh
#
# Copyright (c) 2008 Rainer Clasen
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms described in the file LICENSE included in this
# distribution.
#


aclocal
libtoolize --copy --force
autoheader
automake --add-missing --copy --force-missing
autoconf

