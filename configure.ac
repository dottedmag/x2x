dnl
dnl Copyright (c) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
dnl
dnl BSD-3
dnl

AC_PREREQ([2.57])
AC_INIT(x2x,[1.30],[http://x2x.dottedmag.net/newticket],x2x)
AM_INIT_AUTOMAKE([dist-bzip2])
AM_MAINTAINER_MODE

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL

PKG_CHECK_MODULES(X11, xext xtst x11)

XORG_MANPAGE_SECTIONS
XORG_RELEASE_VERSION

AC_ARG_ENABLE([win32],
    AS_HELP_STRING(
        [--enable-win32], 
        [enable Win32 support (-fromwin option). Disabled by default]))

AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)
AC_SUBST(enable_win32)

AM_CONDITIONAL(WIN32, [test x$enable_win32 = xyes])

AC_OUTPUT([Makefile])
