#
# Copyright (c) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
#

bin_PROGRAMS = x2x

AM_CFLAGS = $(X11_CFLAGS) -Wall
AM_LIBS = $(X11_LIBS)

x2x_SOURCES = lawyerese.c x2x.c

lawyerese.c: COPYING.win32 COPYING
	echo 'char *lawyerese =' > $@.tmp && \
		sed -e 's|.*|"\0\\n"|g' $^ >> $@.tmp && \
		echo ";" >> $@.tmp && \
		mv $@.tmp $@ || rm -f $@.tmp

# if cygwin
#   x2x_SOURCES += keymap.c winmsg.c
#   x2x_OBJS += x2xwin.res
#   x2x_LDADD += -luser32 -lgdi32
#   x2x_CFLAGS += -DWIN_2_X
#   x2x resources: x2xwin.rc, nocursor.cur, resource.h...
# endif
