AC_PREREQ([2.53])
AC_INIT(rlm_zeromq.c)
AC_REVISION($Revision$)
AC_DEFUN(modname,[rlm_zeromq])

m4_include([/usr/share/aclocal/pkg.m4])
PKG_PROG_PKG_CONFIG([0.25])

if test x$with_[]modname != xno; then

    AC_PROG_CC

    smart_try_dir=
    FR_SMART_CHECK_LIB(zmq, zmq_connect)
    if test "x$ac_cv_lib_zmq_zmq_connect" != "xyes"; then
        fail="$fail libzmq"
    fi

    smart_try_dir=
    FR_SMART_CHECK_INCLUDE(zmq.h)
    if test "$ac_cv_header_zmq_h" != "yes"; then
        fail="$fail zmq.h"
    fi

    LIBBSON_CFLAGS=
    LIBBSON_LDFLAGS=
    PKG_CHECK_MODULES([LIBBSON], [libbson-1.0],[ fail=; ],[ fail="$fail libbson-1.0" ])

    targetname=modname

else
    targetname=
    echo \*\*\* module modname is disabled.
fi

if test x"$fail" != x""; then
    if test x"${enable_strict_dependencies}" = x"yes"; then
        AC_MSG_ERROR([set --without-]modname[ to disable it explicitly.])
    else
        AC_MSG_WARN([silently not building ]modname[.])
        AC_MSG_ERROR([FAILURE: ]modname[ requires: $fail.]);
        targetname=""
    fi
fi

mod_ldflags="$SMART_LIBS $LIBBSON_LIBS"
mod_cflags="$SMART_CPPFLAGS $LIBBSON_CFLAGS"

AC_SUBST(mod_ldflags)
AC_SUBST(mod_cflags)
AC_SUBST(targetname)
AC_OUTPUT(all.mk)
