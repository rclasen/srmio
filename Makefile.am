AUTOMAKE_OPTIONS=foreign

SUBDIRS =

AM_CFLAGS=-g -Wall -Wextra
AM_CPPFLAGS =
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
	gencommit.sh \
	genconfheader.sh \
	getcommit.sh \
	srmwinget.sh \
	.commit

MOSTLYCLEANFILES= commit.c
DISTCLEANFILES= srmio_config.h
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

if HAVE_WINCOM
WINCOM_SRC = seriow32.c
else
WINCOM_SRC =
endif

LIBSRMIO=libsrmio.la
libsrmio_la_LDFLAGS = -version-info 1:0:0
libsrmio_la_LIBADD=$(D2XX_LIB)
libsrmio_la_DEPENDENCIES=
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
	$(WINCOM_SRC) \
	list.c \
	marker.c \
	pc.c \
	pc5.c \
	pc7.c \
	split.c \
	store.c \
	commit.c

.PHONY: .commit
.commit:
	$(SHELL) $(srcdir)/getcommit.sh $(srcdir) $@

commit.c: .commit
	$(SHELL) $(srcdir)/gencommit.sh $< > $@

lib_LTLIBRARIES=$(LIBSRMIO)

srmcmd_LDADD= $(LIBSRMIO) $(D2XX_LIB)
srmcmd_DEPENDENCIES= $(LIBSRMIO)
srmcmd_SOURCES= \
	srmcmd.c

srmdump_LDADD= $(LIBSRMIO) $(D2XX_LIB)
srmdump_DEPENDENCIES= $(LIBSRMIO)
srmdump_SOURCES= \
	srmdump.c

srmsync_LDADD= $(LIBSRMIO) $(D2XX_LIB)
srmsync_DEPENDENCIES= $(LIBSRMIO)
srmsync_SOURCES= \
	srmsync.c

bin_PROGRAMS=srmcmd srmdump srmsync


CLEANFILES=$(MANS)
.man_in.man:
	sed -e 's/%PACKAGE_VERSION%/$(PACKAGE_VERSION)/' $< > $@
