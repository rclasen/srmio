#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([srmio], [0.1.0~git0], [bj@zuto.de])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_CONFIG_SRCDIR([srmio.h])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

LT_CONFIG_LTDL_DIR([libltdl])
LT_INIT([dlopen])
LTDL_INIT([recursive])

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
AM_CONDITIONAL([HAVE_LIBDL], [ test "x$libltdl_cv_lib_dl_dlopen" = xyes ])

AC_CHECK_LIB([pthread],[pthread_create],[
  ac_cv_lib_pthread=yes
], [
  ac_cv_lib_pthread=no
])
AM_CONDITIONAL([HAVE_LIBPTHREAD], [ test "x$ac_cv_lib_pthread" = xyes ])
AS_IF([ test "x$ac_cv_lib_pthread" = xyes ],[
  AC_SUBST([HAVE_LIBPTHREAD],[true])
])



# Checks for header files.
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_CHECK_HEADERS([dlfcn.h  fcntl.h getopt.h inttypes.h limits.h malloc.h memory.h stddef.h stdint.h stdlib.h string.h sys/stat.h sys/time.h termios.h unistd.h windows.h])

AC_CHECK_HEADER([ftd2xx.h],[
  AC_DEFINE([HAVE_FTD2XX_H],[1],[Define to 1 if you have the <ftd2xx.h> header file.])
], [], AC_INCLUDES_DEFAULT([
#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif
]))

io_backends=""
AC_SYS_POSIX_TERMIOS
AM_CONDITIONAL([HAVE_TERMIOS],[ test "x$ac_cv_sys_posix_termios" = xyes ])
AS_IF([test "x$ac_cv_sys_posix_termios" = xyes], [
  AC_SUBST([HAVE_TERMIOS], true)
  AC_DEFINE([HAVE_TERMIOS],[1],[Define if sytem has posix termios for serial device access.])
  io_backends="$io_backends termios"
],[])



# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_SIZE_T
AC_STRUCT_TM

for tp in c_int8_t c_int16_t c_int32_t c_int64_t c_uint8_t c_uint16_t c_uint32_t c_uint64_t type_size_t; do
	eval have_type=\$ac_cv_$tp
	if test "x$have_type" != xyes; then
		AC_MSG_WARN([*** missing $tp: $have_type])
	fi
done


# Checks for library functions.
dnl AC_FUNC_MALLOC - unneeded, never called with size=0
dnl AC_FUNC_REALLOC - unneeded, never called with size=0
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_CHECK_FUNCS([cfmakeraw localtime_r mkstemps nanosleep ])
AC_CHECK_FUNCS([gettimeofday memset mkdir strcasecmp strdup strrchr strerror getopt_long ])

for func in gettimeofday memset mkdir strcasecmp strdup strrchr strerror getopt_long; do
	eval have_func=\$ac_cv_func_$func
	if test "x$have_func" != xyes; then
		AC_MSG_WARN([*** missing working $func: $have_func])
	fi
done

# check for windows mkdir(dir) - stolen from wine
AC_CACHE_CHECK([whether mkdir takes only one argument],
  ac_cv_one_arg_mkdir,
  AC_TRY_LINK([[
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
  ]],[[
mkdir("foo");
  ]], [
    ac_cv_one_arg_mkdir=yes
  ], [
    ac_cv_one_arg_mkdir=no
  ])
)
if test "$ac_cv_one_arg_mkdir" = "yes"
then
  AC_DEFINE([HAVE_ONE_ARG_MKDIR], [1], [Define if mkdir takes only one argument])
fi


# check for windows Sleep(msec)
AC_CACHE_CHECK([for windows Sleep using milliseconds],
  ac_cv_msec_sleep,
  AC_TRY_LINK([[
#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif
  ]], [[
Sleep(1000);
  ]], [
    ac_cv_msec_sleep=yes
  ], [
    ac_cv_msec_sleep=no
  ])
)
if test "$ac_cv_msec_sleep" = "yes"
then
  AC_DEFINE([HAVE_MSEC_SLEEP], [1], [Define to use Windows Sleep using milliseconds])
fi

AC_CACHE_CHECK([for windwos comm SetCommState],
  ac_cv_func_SetCommState,
  AC_TRY_LINK([[
#ifdef HAVE_WINDOWS_H
# include <windows.h>
#endif
 ]],[[
HANDLE fh;
DCB dcb;
SetCommState(fh, &dcb);
 ]], [
   ac_cv_func_SetCommState=yes
 ], [
   ac_cv_func_SetCommState=no
 ])
)

AM_CONDITIONAL([HAVE_WINCOM],[ test "x$ac_cv_func_SetCommState" = xyes ])
AS_IF([test "x$ac_cv_func_SetCommState" = xyes], [
  AC_SUBST([HAVE_WINCOM], true)
  AC_DEFINE([HAVE_WINCOM],[1],[Define if sytem has win32 commport API.])
  io_backends="$io_backends wincom"
],[])


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

AC_ARG_WITH([ftd2xx], [AS_HELP_STRING([--with-ftd2xx=<filename>],
    [support libftd2xx for FTDI device serial access @<:@default=check@:>@])
],[
],[
  if test "x$ac_cv_header_ftd2xx_h" = xyes ; then
    with_ftd2xx=check
  else
    with_ftd2xx=no
  fi
])


ac_cv_d2xx_libname=""
AC_MSG_CHECKING([for ftd2xx filename])

AS_IF([ test "x$with_ftd2xx" = xno ], [
  AC_MSG_RESULT([no])

], [test "x$with_ftd2xx" == xyes || test "x$with_ftd2xx" == xcheck], [

  case "$host" in
   *cygwin*|*mingw*)
     ac_cv_d2xx_libname=ftd2xx	# .dll
     ;;
   *)
     ac_cv_d2xx_libname=libftd2xx # .so libftd2xx.dylib
     ;;
  esac

  if test -z "$ac_cv_d2xx_libname" ; then
    if test "x$with_ftd2xx" != xcheck; then
      AC_MSG_FAILURE([--with-ftd2x was given, but test for ftd2xx failed])
    else
      AC_MSG_RESULT([none])
    fi
  else
    AC_MSG_RESULT("$ac_cv_d2xx_libname")
  fi

], [
  ac_cv_d2xx_libname="$with_ftd2xx"
  AC_MSG_RESULT("$ac_cv_d2xx_libname" - forced by user)
])

AM_CONDITIONAL([HAVE_D2XX], [ test -n "$ac_cv_d2xx_libname" ])

if test "x$with_ftd2xx" != xcheck && test -n "$ac_cv_d2xx_libname"; then
  if test "x$ac_cv_header_ftd2xx_h" != xyes; then
    AC_MSG_WARN([*** d2xx requires ftd2xx.h header])
  fi
fi

AS_IF([ test -n "$ac_cv_d2xx_libname" ], [
  AC_SUBST([HAVE_D2XX],[true])
  AC_DEFINE([HAVE_D2XX],[1],[Define if system has libftd2xx.])
  AC_DEFINE_UNQUOTED([D2XX_LIBNAME],"$ac_cv_d2xx_libname",[Define to libftd2xx filname for dlopen.])
  io_backends="$io_backends ftd2xx"

])

AS_IF([ test -n "$io_backends" ], [
  AC_MSG_NOTICE([detected serial IO backends:$io_backends])

], [
  AC_MSG_WARN([*** no working serial IO found ])
])

if test "x$ac_cv_func_nanosleep" != xyes && test "x$ac_cv_msec_sleep" != xyes ; then
  AC_MSG_WARN([*** no high-precision sleep function found])
fi

AC_CONFIG_FILES([Makefile libltdl/Makefile])

AC_CONFIG_COMMANDS([genconfheader.sh],[
. $srcdir/genconfheader.sh > "$tmp/srmio_config.h"
if diff "$tmp/srmio_config.h" srmio_config.h > /dev/null 2>&1; then
  : # unchanged
else
  rm -f srmio_config.h
  mv "$tmp/srmio_config.h" srmio_config.h
fi
],[
HEADER_STDINT_H=$ac_cv_header_stdint_h
HEADER_INTTYPES_H=$ac_cv_header_inttypes_h
HEADER_STDBOOL_H=$ac_cv_header_stdbool_h
HAVE__BOOL=$ac_cv_type__Bool
HEADER_TIME_WITH_SYS_TIME=$ac_cv_header_time
HEADER_SYS_TIME_H=$ac_cv_header_sys_time_h
HAVE_TERMIOS=$ac_cv_sys_posix_termios
HAVE_WINCOM=$ac_cv_func_SetCommState
HAVE_D2XX=$HAVE_D2XX
])

AC_OUTPUT
