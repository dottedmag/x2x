#
# Copyright (c) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
#

bin_PROGRAMS = x2x

x2x_CFLAGS = $(X2X_CFLAGS) -Wall
x2x_LDADD = $(X2X_LDADD)

x2x_SOURCES = lawyrese.c x2x.c

# if cygwin
#   x2x_SOURCES += keymap.c winmsg.c
#   x2x_OBJS += x2xwin.res
#   x2x_LDADD += -luser32 -lgdi32
#   x2x_CFLAGS += -DWIN_2_X
#   x2x resources: x2xwin.rc, nocursor.cur, resource.h...
# endif
