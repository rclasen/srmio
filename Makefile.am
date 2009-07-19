AUTOMAKE_OPTIONS=foreign

#AM_CFLAGS=-g -Wall -Wextra -DDEBUG

man1_MANS=srmcmd.man
EXTRA_DIST=$(MANS) \
	debug.h

include_HEADERS= \
	srmio.h

lib_LTLIBRARIES=libsrmio.la
libsrmio_la_SOURCES= \
	srmdata.c \
	srmfile.c \
	srmpc.c

bin_PROGRAMS=srmcmd

srmcmd_LDFLAGS= -lsrmio
srmcmd_SOURCES= srmcmd.c

