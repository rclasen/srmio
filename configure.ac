#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([srmio], [0.1.0~git0], [bj@zuto.de])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_SRCDIR([srmio.h])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lsrmio':

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([dlfcn.h fcntl.h getopt.h inttypes.h limits.h memory.h stddef.h stdint.h stdlib.h string.h sys/time.h termios.h unistd.h])


AC_SYS_POSIX_TERMIOS
AM_CONDITIONAL([HAVE_TERMIOS],[ test "x$ac_cv_sys_posix_termios" = xyes ])
AS_IF([test "x$ac_cv_sys_posix_termios" = xyes], [
  AC_SUBST([HAVE_TERMIOS], true)
  AC_DEFINE([HAVE_TERMIOS],[1],[Define if sytem has posix termios for serial device access.])
  AC_CHECK_FUNC([cfmakeraw],
    [AC_DEFINE([HAVE_CFMAKERAW],[1], [Define if cfmakeraw exists.])],)
],[])



# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT16_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_TYPE_SIZE_T
AC_STRUCT_TM


# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([localtime_r ])
AC_CHECK_FUNCS([memset strdup strrchr strerror getopt_long ])

for func in memset strdup strrchr strerror getopt_long; do
	eval have_func=\$ac_cv_func_$func
	if test "x$have_func" != xyes; then
		AC_MSG_ERROR([missing working $func: $have_func])
	fi
done
# AC_CPP_FUNC
# ------------------ #
# Checks to see if ANSI C99 CPP variable __func__ works.
# If not, perhaps __FUNCTION__ works instead. 
# If not, we'll just define __func__ to "". 
AC_DEFUN([AC_CPP_FUNC],
[AC_REQUIRE([AC_PROG_CC_STDC])dnl
AC_CACHE_CHECK([for an ANSI C99-conforming __func__], ac_cv_cpp_func,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
[[char *foo = __func__;]])],
  [ac_cv_cpp_func=yes],
  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
[[char *foo = __FUNCTION__;]])],
  [ac_cv_cpp_func=__FUNCTION__],
  [ac_cv_cpp_func=no])])])
if test $ac_cv_cpp_func = __FUNCTION__; then
  AC_DEFINE(__func__,__FUNCTION__,
            [Define to __FUNCTION__ or "" if __func__ does not conform to ANSI C.])
elif test $ac_cv_cpp_func = no; then
  AC_DEFINE(__func__,"",
            [Define to __FUNCTION__ or "" if __func__ does not conform to ANSI C.])
fi
])# AC_CPP_FUNC

AC_CPP_FUNC


# TODO: getopt_long alternatives (FreeBSD)
# TODO: provide alternative implementations, use gnulib?

AC_DEFUN([AC_DLOPEN_IFELSE],[
  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([],[[
#include <dlfcn.h>
	  return 0 == dlopen($1, RTLD_NOW) ? 1 : 0;
    ]])
  ], [$2] , [$3] )
])


AC_ARG_WITH([ftd2xx],
  [AS_HELP_STRING([--with-ftd2xx=<filename>],
    [support libftd2xx for FTDI device serial access @<:@default=check@:>@])],
  [],
  [with_ftd2xx=check])

HAVE_D2XX=
AS_IF([test "x$with_ftd2xx" != xno], [
  AC_CHECK_LIB([ftd2xx], [FT_OpenEx], [
    AC_MSG_CHECKING([for ftd2xx filename])
    #for lib in libftd2xx.so ftd2xx.dll libftd2xx.dylib "$with_ftd2xx"; do
    AC_DLOPEN_IFELSE(["libftd2xx.so"],[
      AC_SUBST([HAVE_D2XX],[true])
      AC_DEFINE([HAVE_D2XX],[1],[Define if system has libftd2xx.])
      AC_DEFINE([D2XX_LIBNAME],"libftd2xx.so",[Define to libftd2xx filname for dlopen.])
    ], [
      AC_DLOPEN_IFELSE(["libftd2xx.dylib"],[
        AC_SUBST([HAVE_D2XX],[true])
        AC_DEFINE([HAVE_D2XX],[1],[Define if system has libftd2xx.])
        AC_DEFINE([D2XX_LIBNAME],"libftd2xx.dylib",[Define to libftd2xx filname for dlopen.])
      ],[
        AC_DLOPEN_IFELSE(["ftd2xx.dll"],[
          AC_SUBST([HAVE_D2XX],[true])
          AC_DEFINE([HAVE_D2XX],[1],[Define if system has libftd2xx.])
          AC_DEFINE([D2XX_LIBNAME],"ftd2xx.dll",[Define to libftd2xx filname for dlopen.])
        ],[
        ])
      ])
    ])

    if test -z "$HAVE_D2XX" ; then
      if test "x$with_ftd2xx" != xcheck; then
        AC_MSG_FAILURE([--with-ftd2x was given, but test for ftd2xx failed])
      else
        AC_MSG_RESULT([none])
      fi
    else
      AC_MSG_RESULT(["found"])
    fi
  ],[
    if test "x$with_ftd2xx" != xcheck; then
      AC_MSG_FAILURE([--with-ftd2x was given, but test for ftd2xx failed])
    fi
  ])
])

AM_CONDITIONAL([HAVE_D2XX], [ test -n "$HAVE_D2XX" ])


AC_CONFIG_FILES([Makefile])

AC_CONFIG_COMMANDS([genconfheader.sh],[
. $srcdir/genconfheader.sh > srmio_config.h
],[
HEADER_STDINT_H=$ac_cv_header_stdint_h
HEADER_INTTYPES_H=$ac_cv_header_inttypes_h
HEADER_STDBOOL_H=$ac_cv_header_stdbool_h
HAVE__BOOL=$ac_cv_type__Bool
HEADER_TIME_WITH_SYS_TIME=$ac_cv_header_time
HEADER_SYS_TIME_H=$ac_cv_header_sys_time_h
HEADER_TERMIOS_H=$ac_cv_header_termios_h
HAVE_TERMIOS=$ac_cv_sys_posix_termios
HAVE_D2XX=$HAVE_D2XX
])

AC_OUTPUT
