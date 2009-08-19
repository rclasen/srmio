#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([srmio], [0.0.3.2009081601], [bj@zuto.de])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_SRCDIR([srmio.h])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lsrmio':

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h getopt.h inttypes.h limits.h memory.h stddef.h stdint.h stdlib.h string.h sys/time.h termios.h unistd.h])


AC_SYS_POSIX_TERMIOS


# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_SIZE_T
AC_STRUCT_TM


# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([localtime_r ])
AC_CHECK_FUNCS([memset strdup strerror getopt_long ])

for func in memset strdup strerror getopt_long; do
	eval have_func=\$ac_cv_func_$func
	if test "x$have_func" != xyes; then
		AC_MSG_ERROR([missing working $func])
	fi
done

# TODO: getopt_long alternatives (FreeBSD)
# TODO: provide alternative implementations, use gnulib?

AC_CHECK_FUNC([cfmakeraw],
	[AC_DEFINE([HAVE_CFMAKERAW],[1], [Define if cfmakeraw exists.])],)


AC_CONFIG_FILES([Makefile])

AC_CONFIG_COMMANDS([genconfheader.sh],[
. $srcdir/genconfheader.sh > srmio_config.h
],[
HEADER_STDINT_H=$ac_cv_header_stdint_h
HEADER_INTTYPES_H=$ac_cv_header_inttypes_h
HEADER_TIME_WITH_SYS_TIME=$ac_cv_header_time
HEADER_SYS_TIME_H=$ac_cv_header_sys_time_h
HEADER_TERMIOS_H=$ac_cv_header_termios_h
])

AC_OUTPUT
