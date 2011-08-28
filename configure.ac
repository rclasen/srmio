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
AC_CHECK_HEADERS([dlfcn.h fcntl.h getopt.h inttypes.h limits.h memory.h stddef.h stdint.h stdlib.h string.h sys/time.h unistd.h windows.h])


AC_SYS_POSIX_TERMIOS
AM_CONDITIONAL([HAVE_TERMIOS],[ test "x$ac_cv_sys_posix_termios" = xyes ])
AS_IF([test "x$ac_cv_sys_posix_termios" = xyes], [
  AC_SUBST([HAVE_TERMIOS], true)
  AC_DEFINE([HAVE_TERMIOS],[1],[Define if sytem has posix termios for serial device access.])

  AC_CHECK_HEADERS([termios.h])
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
AC_CHECK_FUNCS([localtime_r mkstemps])
AC_CHECK_FUNCS([memset strdup strrchr strerror getopt_long ])

for func in memset strdup strrchr strerror getopt_long; do
	eval have_func=\$ac_cv_func_$func
	if test "x$have_func" != xyes; then
		AC_MSG_ERROR([missing working $func: $have_func])
	fi
done

# check for windows mkdir(dir) - stolen from wine
AC_CACHE_CHECK([whether mkdir takes only one argument],
	ac_cv_one_arg_mkdir,
	AC_TRY_LINK(
		[[ #include <sys/stat.h>
#include <unistd.h>]],
		[[_mkdir("foo");]],
		[ac_cv_one_arg_mkdir=yes],
		[ac_cv_one_arg_mkdir=no]
	)
)
if test "$ac_cv_one_arg_mkdir" = "yes"
then
  AC_DEFINE([HAVE_ONE_ARG_MKDIR], [1], [Define if mkdir takes only one argument])
fi


# check for windows Sleep(msec)
AC_CACHE_CHECK([for windows Sleep using milliseconds],
	ac_cv_msec_sleep,
	AC_COMPILE_IFELSE(
		[AC_LANG_PROGRAM(
			[[#include <windows.h>]],
			[[Sleep(1000);]]
			)],
		[ac_cv_msec_sleep=yes],
		[ac_cv_msec_sleep=no])
)
if test "$ac_cv_msec_sleep" = "yes"
then
  AC_DEFINE([HAVE_MSEC_SLEEP], [1], [Define to use Windows Sleep using milliseconds])
fi


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
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif
	  return 0 == dlopen($1, RTLD_NOW) ? 1 : 0;
    ]])
  ], [$2] , [$3] )
])


AC_ARG_WITH([ftd2xx],
  [AS_HELP_STRING([--with-ftd2xx=<filename>],
    [support libftd2xx for FTDI device serial access @<:@default=check@:>@])],
  [],
  [with_ftd2xx=check])

ac_cv_d2xx_libname=""
AS_IF([test "x$with_ftd2xx" == xyes || test "x$with_ftd2xx" == xcheck], [
  AC_CHECK_LIB([ftd2xx], [FT_OpenEx], [
    AC_MSG_CHECKING([for ftd2xx filename])
    #for lib in libftd2xx.so ftd2xx.dll libftd2xx.dylib "$with_ftd2xx"; do
    AC_DLOPEN_IFELSE(["libftd2xx.so"],[
      ac_cv_d2xx_libname="libftd2xx.so"
    ], [
      AC_DLOPEN_IFELSE(["libftd2xx.dylib"],[
        ac_cv_d2xx_libname="libftd2xx.dylib"
      ],[
        AC_DLOPEN_IFELSE(["ftd2xx.dll"],[
          ac_cv_d2xx_libname="ftd2xx.dll"
        ],[
        ])
      ])
    ])
  ])

  if test -z "$ac_cv_d2xx_libname" ; then
    if test "x$with_ftd2xx" != xcheck; then
      AC_MSG_FAILURE([--with-ftd2x was given, but test for ftd2xx failed])
    else
      AC_MSG_RESULT([none])
    fi
  else
    AC_MSG_RESULT("$ac_cv_d2xx_libname")
  fi
], [test "x$with_ftd2xx" != xno ], [
  ac_cv_d2xx_libname="$with_ftd2xx"
  AC_MSG_CHECKING([for ftd2xx filename])
  AC_MSG_RESULT("$ac_cv_d2xx_libname" - forced by user)
])

AM_CONDITIONAL([HAVE_D2XX], [ test -n "$ac_cv_d2xx_libname" ])

AS_IF([ test -n "$ac_cv_d2xx_libname"], [
  AC_SUBST([HAVE_D2XX],[true])
  AC_DEFINE([HAVE_D2XX],[1],[Define if system has libftd2xx.])
  AC_DEFINE_UNQUOTED([D2XX_LIBNAME],"$ac_cv_d2xx_libname",[Define to libftd2xx filname for dlopen.])
  AC_CHECK_LIB([pthread],[pthread_create],[
    ac_cv_pthread=yes
  ], [
    ac_cv_pthread=no
  ])
  AC_CHECK_LIB([dl],[dlopen],[
    ac_cv_dl=yes
  ], [
    ac_cv_dl=no
  ])
])


AM_CONDITIONAL([HAVE_DL], [ test "x$ac_cv_dl" = xyes ])
AS_IF([ test "x$ac_cv_dl" = xyes ],[
  AC_SUBST([HAVE_DL],[true])
])

AM_CONDITIONAL([HAVE_PTHREAD], [ test "x$ac_cv_pthread" = xyes ])
AS_IF([ test "x$ac_cv_pthread" = xyes ],[
  AC_SUBST([HAVE_PTHREAD],[true])
])


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
HAVE_TERMIOS=$ac_cv_sys_posix_termios
HAVE_D2XX=$HAVE_D2XX
])

AC_OUTPUT
