AUTOMAKE_OPTIONS=foreign

#AM_CFLAGS=-g -Wall -Wextra -DDEBUG

man1_MANS=srmcmd.man
EXTRA_DIST=$(MANS) \
	Changes \
	debian/docs \
	debian/compat \
	debian/copyright \
	debian/rules \
	debian/changelog \
	debian/dirs \
	debian/control \
	LICENSE \
	README

include_HEADERS= \
	srmio.h \
	srmio_config.h

lib_LTLIBRARIES=libsrmio.la
libsrmio_la_SOURCES= \
	out.h \
	common.h \
	\
	srmdata.c \
	srmfile.c \
	srmpc.c

bin_PROGRAMS=srmcmd

srmcmd_LDFLAGS= -lsrmio
srmcmd_SOURCES= \
	srmcmd.c

out.h: genconfheader.sh
