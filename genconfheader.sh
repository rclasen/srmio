#!/bin/sh
#
# Copyright (c) 2008 Rainer Clasen
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms described in the file LICENSE included in this
# distribution.
#


# Must be sourced by config.status to pickup the variables

cat <<EOF
# ifdef __cplusplus
extern "C"
{
# endif

EOF

if [ -n "$HEADER_STDINT_H" ]; then
	echo "#include <stdint.h>" 
fi
if [ -n "$HEADER_INTTYPES_H" ]; then
	echo "#include <inttypes.h>"
fi

if [ -n "$HEADER_STDBOOL_H" ]; then
	echo "#include <stdbool.h>"

else
	if [ -z "$HAVE__BOOL" ]; then
cat <<EOF
#ifdef __cplusplus
typedef bool _Bool;
#else
#define _Bool signed char
#endif
EOF
	fi
cat <<EOF
#define bool _Bool
#define false 0
#define true 1
#define __bool_true_false_are_defined 1
EOF
fi


if [ -n "$HEADER_TIME_WITH_SYS_TIME" ]; then
	echo "#include <sys/time.h>"
	echo "#include <time.h>"
elif [ -n "$HEADER_SYS_TIME_H" ]; then
	echo "#include <sys/time.h>"
else
	echo "#include <time.h>"
fi

if [ "$HAVE_TERMIOS"x = yesx ]; then
	echo "#define SRMIO_HAVE_TERMIOS"
fi

if [ -n "$HAVE_D2XX" ]; then
	echo "#define SRMIO_HAVE_D2XX"
fi

cat<<EOF

# ifdef __cplusplus
}
# endif
EOF

