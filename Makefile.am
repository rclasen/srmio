AUTOMAKE_OPTIONS=foreign

SUBDIRS = libltdl

#AM_CFLAGS=-g -Wall -Wextra -DDEBUG
AM_CPPFLAGS = $(LTDLINCL)
ACLOCAL_AMFLAGS = -I m4

man1_INS=srmcmd.man_in srmdump.man_in srmsync.man_in
man1_MANS=srmcmd.man srmdump.man srmsync.man
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

if HAVE_LIBDL
D2XX_LIB_DL = -ldl
else
D2XX_LIB_DL =
endif

if HAVE_LIBPTHREAD
D2XX_LIB_PTHREAD = -lpthread
else
D2XX_LIB_PTHREAD =
endif

if HAVE_D2XX
D2XX_SRC = d2xx.c
D2XX_LIB = $(D2XX_LIB_DL) $(D2XX_LIB_PTHREAD)
else
D2XX_SRC =
D2XX_LIB =
endif

if HAVE_TERMIOS
TERMIOS_SRC = serios.c
else
TERMIOS_SRC =
endif

lib_LTLIBRARIES=libsrmio.la

libsrmio_la_LIBADD=$(LIBLTDL) $(D2XX_LIB)
libsrmio_la_DEPENDENCIES=$(LTDLDEPS)
libsrmio_la_SOURCES= \
	common.h \
	serio.h \
	pc.h \
	\
	buf.c \
	chunk.c \
	common.c \
	$(D2XX_SRC) \
	data.c \
	error.c \
	ftypes.c \
	file_srm.c \
	file_wkt.c \
	fixup.c \
	serio.c \
	$(TERMIOS_SRC) \
	list.c \
	marker.c \
	pc.c \
	pc5.c \
	pc7.c \
	split.c \
	store.c

bin_PROGRAMS=srmcmd srmdump srmsync

srmcmd_LDADD= libsrmio.la $(D2XX_LIB)
srmcmd_DEPENDENCIES= $(LTDLDEPS)
srmcmd_SOURCES= \
	srmcmd.c

srmdump_LDADD= libsrmio.la $(D2XX_LIB)
srmdump_DEPENDENCIES= $(LTDLDEPS)
srmdump_SOURCES= \
	srmdump.c

srmsync_LDADD= libsrmio.la $(D2XX_LIB)
srmsync_DEPENDENCIES= $(LTDLDEPS)
srmsync_SOURCES= \
	srmsync.c


CLEANFILES=$(MANS)
.man_in.man:
	sed -e 's/%PACKAGE_VERSION%/$(PACKAGE_VERSION)/' $< > $@
