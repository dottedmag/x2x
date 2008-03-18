dnl
dnl Copyright (c) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
dnl

AC_PREREQ([2.57])
AC_INIT(x2x,[1.30],[http://x2x.dottedmag.net/newticket],x2x)
AM_INIT_AUTOMAKE([dist-bzip2])
AM_MAINTAINER_MODE

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL

PKG_CHECK_MODULES(X11, xext x11)

AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

XORG_MANPAGE_SECTIONS
XORG_RELEASE_VERSION

AC_OUTPUT([Makefile])
