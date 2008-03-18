#
# Copyright (c) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
#
# BSD-3
#

AM_CFLAGS = $(X11_CFLAGS) -Wall
AM_LDFLAGS = $(X11_LIBS)

bin_PROGRAMS = x2x

x2x_SOURCES = lawyerese.c x2x.c

if WIN32

AM_CFLAGS += -DWIN_2_X
AM_LDFLAGS += -luser32 -lgdi32 # Ugly hack
x2x_SOURCES += keymap.c winmsg.c x2xwin.rc

%.o: %.rc
	windres $^ -o $@

x2xwin.o: resource.h nocursor.cur

endif


# -- creating lawyerese.c --

if WIN32
COPYING_FILES = COPYING.win32
else
COPYING_FILES =
endif
COPYING_FILES += COPYING

lawyerese.c: $(COPYING_FILES)
	echo 'char *lawyerese =' > $@.tmp && \
		sed -e 's|.*|"\0\\n"|g' $^ >> $@.tmp && \
		echo ";" >> $@.tmp && \
		mv $@.tmp $@ || rm -f $@.tmp

CLEANFILES = lawyerese.c

# -- --
