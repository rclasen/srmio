AUTOMAKE_OPTIONS=foreign

#AM_CFLAGS=-g -Wall -Wextra -DDEBUG


man1_INS=srmcmd.man_in srmdump.man_in
man1_MANS=srmcmd.man srmdump.man
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

DISTCLEANFILES=srmio_config.h
include_HEADERS= \
	srmio.h \
	srmio_config.h

if HAVE_D2XX
D2XX_SRC = d2xx.c
D2XX_LD = -ldl -lpthread
else
D2XX_SRC =
D2XX_LD =
endif

if HAVE_TERMIOS
TERMIOS_SRC = ios.c
else
TERMIOS_SRC =
endif

lib_LTLIBRARIES=libsrmio.la

libsrmio_la_LDFLAGS= $(D2XX_LD)
libsrmio_la_SOURCES= \
	common.h \
	io.h \
	pc.h \
	\
	buf.c \
	chunk.c \
	common.c \
	$(D2XX_SRC) \
	data.c \
	ftypes.c \
	file_srm.c \
	file_wkt.c \
	fixup.c \
	io.c \
	$(TERMIOS_SRC) \
	marker.c \
	pc.c \
	pc5.c \
	pc7.c \
	split.c

bin_PROGRAMS=srmcmd srmdump

srmcmd_LDFLAGS= -lsrmio $(D2XX_LD)
srmcmd_SOURCES= \
	srmcmd.c

srmdump_LDFLAGS= -lsrmio $(D2XX_LD)
srmdump_SOURCES= \
	srmdump.c

CLEANFILES=$(MANS)
.man_in.man:
	sed -e 's/%PACKAGE_VERSION%/$(PACKAGE_VERSION)/' $< > $@
