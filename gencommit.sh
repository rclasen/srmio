#!/bin/sh

file="$1"
if [ -z "$file" ]; then
	echo "missing commit ID filename">&2
	exit 1
fi

if [ -r "$file" ] ; then
	last_commit="`cat "$file"`"
else
	last_commit="unknown"
fi

date="`date`"

cat <<EOF
/* auto-generated by $0 on $date */
const char *srmio_commit = "$last_commit";
EOF

