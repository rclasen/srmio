#!/bin/sh

# sourced by config.status

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

if [ -n "$HEADER_TIME_WITH_SYS_TIME" ]; then
	echo "#include <sys/time.h>"
	echo "#include <time.h>"
elif [ -n "$HEADER_SYS_TIME_H" ]; then
	echo "#include <sys/time.h>"
else
	echo "#include <time.h>"
fi

if [ -n "$HEADER_TERMIOS_H" ]; then
	echo "#include <termios.h>"
fi

cat<<EOF

# ifdef __cplusplus
}
# endif
EOF

