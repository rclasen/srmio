AUTOMAKE_OPTIONS=foreign

#AM_CFLAGS=-g -Wall -Wextra -DDEBUG


man1_INS=srmcmd.man_in
man1_MANS=srmcmd.man
# TODO: build _MANS automagically

man_INS=$(man1_INS)

EXTRA_DIST= $(man_INS) \
	Changes \
	LICENSE \
	README \
	debian/docs \
	debian/compat \
	debian/copyright \
	debian/rules \
	debian/changelog \
	debian/dirs \
	debian/control \
	genautomake.sh \
	genconfheader.sh \
	srmwinget.sh

include_HEADERS= \
	srmio.h \
	srmio_config.h
DISTCLEANFILES=srmio_config.h

lib_LTLIBRARIES=libsrmio.la
libsrmio_la_SOURCES= \
	common.h \
	\
	buf.c \
	chunk.c \
	common.c \
	data.c \
	ftypes.c \
	file_srm.c \
	file_wkt.c \
	fixup.c \
	io.c \
	ios.c \
	marker.c \
	pc.c \
	pc5.c \
	split.c

bin_PROGRAMS=srmcmd

srmcmd_LDFLAGS= -lsrmio
srmcmd_SOURCES= \
	srmcmd.c

CLEANFILES=$(MANS)
.man_in.man:
	sed -e 's/%PACKAGE_VERSION%/$(PACKAGE_VERSION)/' $< > $@
