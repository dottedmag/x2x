/*
 * x2x: Uses the XTEST extension to forward mouse movements and
 * keystrokes from a window on one display to another display.  Useful
 * for desks with multiple keyboards.
 *
 * Copyright (c) 1997
 * Digital Equipment Corporation.  All rights reserved.
 *
 * By downloading, installing, using, modifying or distributing this
 * software, you agree to the following:
 *
 * 1. CONDITIONS. Subject to the following conditions, you may download,
 * install, use, modify and distribute this software in source and binary
 * forms:
 *
 * a) Any source code, binary code and associated documentation
 * (including the online manual) used, modified or distributed must
 * reproduce and retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * b) No right is granted to use any trade name, trademark or logo of
 * Digital Equipment Corporation.  Neither the "Digital Equipment
 * Corporation" name nor any trademark or logo of Digital Equipment
 * Corporation may be used to endorse or promote products derived from
 * this software without the prior written permission of Digital
 * Equipment Corporation.
 *
 * 2.  DISCLAIMER.  THIS SOFTWARE IS PROVIDED BY DIGITAL "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL DIGITAL BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Modified on 3 Oct 1998 by Charles Briscoe-Smith:
 *   added options -north and -south
 */

/* Cygwin version with -fromwin to allow source to be a Windows
 * machine that is not running a X server.
 * Adapted by Mark Hayter 2003 using win2vnc ClientConnect.cpp code
 *
 * Original win2vnc copyright follows:
 */
// win2vnc, adapted from vncviewer by Fredrik Hubinette 2001
//
// Original copyright follows:
//
//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
//
//  This file is part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
// If the source code for the VNC system is not available from the place
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h> /* for selection */
#include <X11/Xos.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#ifdef WIN_2_X
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <windowsx.h>
#include <assert.h>
#include "keymap.h"
#include "resource.h"

#define X2X_MSG_HOTKEY (WM_USER + 1)

#define DPMSModeOn        0
extern Status DPMSForceLevel(Display *, unsigned short);
#else
#include <X11/extensions/dpms.h>
#endif




/*#define DEBUG*/

#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif

#define UTF8_STRING "UTF8_STRING"

/**********
 * definitions for edge
 **********/
#define EDGE_NONE   0 /* don't transfer between edges of screens */
#define EDGE_NORTH  1 /* from display is on the north side of to display */
#define EDGE_SOUTH  2 /* from display is on the south side of to display */
#define EDGE_EAST   3 /* from display is on the east side of to display */
#define EDGE_WEST   4 /* from display is on the west side of to display */

/**********
 * functions
 **********/
static void    ParseCommandLine();
static Display *OpenAndCheckDisplay();
static Bool    CheckTestExtension();
#ifndef WIN_2_X
static int     ErrorHandler();
#endif
static void    DoX2X();
static void    InitDpyInfo();
static void    DoConnect();
static void    DoDisconnect();
static void    RegisterEventHandlers();
static Bool    ProcessEvent();
static Bool    ProcessMotionNotify();
static Bool    ProcessExpose();
static Bool    ProcessEnterNotify();
static Bool    ProcessButtonPress();
static Bool    ProcessButtonRelease();
static Bool    ProcessKeyEvent();
static Bool    ProcessConfigureNotify();
static Bool    ProcessClientMessage();
static Bool    ProcessSelectionRequest();
static void    SendPing();
static Bool    ProcessPropertyNotify();
static Bool    ProcessSelectionNotify();
static void    SendSelectionNotify();
static Bool    ProcessSelectionClear();
static Bool    ProcessVisibility();
static Bool    ProcessMapping();
static void    FakeThingsUp();
static void    FakeAction();
static void    RefreshPointerMapping();
static void    Usage();



/**********
 * stuff for selection forwarding
 **********/
typedef struct _dpyxtra {
  Display *otherDpy;
  int  sState;
  Atom pingAtom;
  Bool pingInProg;
  Window propWin;
} DPYXTRA, *PDPYXTRA;

/**********
 * structures for recording state of buttons and keys
 **********/
typedef struct _fakestr {
  struct _fakestr *pNext;
  int type;
  unsigned int thing;
} FAKE, *PFAKE;

#define FAKE_KEY    0
#define FAKE_BUTTON 1

#define N_BUTTONS   20

#define MAX_BUTTONMAPEVENTS 20

#define GETDPYXTRA(DPY,PDPYINFO)\
   (((DPY) == (PDPYINFO)->fromDpy) ?\
    &((PDPYINFO)->fromDpyXtra) : &((PDPYINFO)->toDpyXtra))

/* values for sState */
#define SELSTATE_ON     0
#define SELSTATE_OFF    1
#define SELSTATE_WAIT   2

/* special values for translated coordinates */
#define COORD_INCR     -1
#define COORD_DECR     -2
#define SPECIAL_COORD(COORD) (((COORD) < 0) ? (COORD) : 0)

/* max unreasonable coordinates before accepting it */
#define MAX_UNREASONABLES 10

/**********
 * display information
 **********/
typedef struct {
  /* stuff on "from" display */
  Display *fromDpy;
  Atom    fromDpyUtf8String;
  Window  root;
  Window  trigger;
  Window  big;
  GC      textGC;
  Atom    wmpAtom, wmdwAtom;
  Cursor  grabCursor;
  Font    fid;
  int     width, height, twidth, theight, tascent;
  Bool    vertical;
  int     lastFromCoord;
  int     unreasonableDelta;

#ifdef WIN_2_X
  int     unreasonableCount;
  /* From display info for Windows */
  HWND    bigwindow;
  HWND    edgewindow;
  int     onedge;
  int     fromWidth, fromHeight;
  int     wdelta;
  int     lastFromY;
  HWND    hwndNextViewer;
  // int     initialClipboardSeen;
  char    *winSelText;
  int     owntoXsel;
  int     expectSelNotify;
  int     expectOwnClip;
  int     winSSave;
  int     nXbuttons;
  RECT    monitorRect;
  RECT    screenRect;
  int     screenHeight;
  int     screenWidth;
  int     lastFromX;
#endif /* WIN_2_X */

  /* stuff on "to" display */
  Display *toDpy;
  Atom    toDpyUtf8String;
  Window  selWin;
  unsigned int inverseMap[N_BUTTONS + 1]; /* inverse of button mapping */

  /* state of connection */
  int     mode;                        /* connection */
  int     eventMask;                /* trigger */

  /* coordinate conversion stuff */
  int     toScreen;
  int     nScreens;
  short   **xTables; /* precalculated conversion tables */
  short   **yTables;
  int      fromConnCoord; /* location of cursor after conn/disc ops */
  int     fromDiscCoord;
  int     fromIncrCoord; /* location of cursor after incr/decr ops */
  int     fromDecrCoord;

  /* selection forwarding info */
  DPYXTRA fromDpyXtra;
  DPYXTRA toDpyXtra;
  Display *sDpy;
  XSelectionRequestEvent sEv;
  Time    sTime;

  /* for recording state of buttons and keys */
  PFAKE   pFakeThings;

} DPYINFO, *PDPYINFO;

/* shadow displays */
typedef struct _shadow {
  struct _shadow *pNext;
  char    *name;
  Display *dpy;
} SHADOW, *PSHADOW;

/* sticky keys */
typedef struct _sticky {
  struct _sticky *pNext;
  KeySym keysym;
} STICKY, *PSTICKY;

typedef int  (*HANDLER)(); /* event handler function */

/* These prototypes need the typedefs */
#ifdef WIN_2_X
static void MoveWindowToEdge(PDPYINFO);
static int MoveWindowToScreen(PDPYINFO);
static void DoWinConnect(PDPYINFO, int, int);
static void DoWinDisconnect(PDPYINFO, int, int);
static void SendButtonClick(PDPYINFO, int);
LRESULT CALLBACK WinProcessMessage (HWND, UINT, WPARAM, LPARAM);
void WinPointerEvent(PDPYINFO, int, int, DWORD, UINT);
void WinKeyEvent(PDPYINFO, int, DWORD);
void SendKeyEvent(PDPYINFO, KeySym, int, int, int);

static Bool    ProcessSelectionRequestW();
static Bool    ProcessSelectionNotifyW();
static Bool    ProcessSelectionClearW();

#ifdef DEBUGCHATTY
char *msgtotext(int);
#endif

#endif /* WIN_2_X */

/**********
 * top-level variables
 **********/
static char    *programStr = "x2x";
static char    *fromDpyName = NULL;
static char    *toDpyName   = NULL;
static char    *defaultFN   = "-*-times-bold-r-*-*-*-180-*-*-*-*-*-*";
static char    *fontName    = "-*-times-bold-r-*-*-*-180-*-*-*-*-*-*";
static char    *label       = NULL;
static char    *title       = NULL;
static char    *pingStr     = "PING"; /* atom for ping request */
static char    *geomStr     = NULL;
static Bool    waitDpy      = False;
static Bool    doBig        = False;
static Bool    doMouse      = True;
static int     doEdge       = EDGE_NONE;
static Bool    doSel        = True;
static Bool    doAutoUp     = True;
static Bool    doResurface  = False;
static PSHADOW shadows      = NULL;
static int     triggerw     = 2;
static Bool    doPointerMap = True;
static PSTICKY stickies     = NULL;
static Bool    doBtnBlock   = False;
static Bool    doCapsLkHack = False;
static Bool    doClipCheck  = False;
static Bool    doDpmsMouse  = False;
static int     logicalOffset= 0;
static int     nButtons     = 0;
static KeySym  buttonmap[N_BUTTONS + 1][MAX_BUTTONMAPEVENTS + 1];

#ifdef WIN_2_X
/* These are used to allow pointer comparisons */
static char *fromWinName = "x2xFromWin";
static int dummy;
static Display *fromWin = (Display *)&dummy;
static HWND hWndSave;
static HINSTANCE m_instance;
#endif

#ifdef DEBUG
#define debug printf
#else
void debug(const char* fmt, ...)
{
}
#endif

#ifdef WIN_2_X

#define MAX_WIN_ARGS 40

int STDCALL
WinMain (HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
  Display *fromDpy;
  PSHADOW pShadow;
  int argc;
  char *argv[MAX_WIN_ARGS];
  char *ap;

  if (lpCmd[0] == ' ')
  {
    lpCmd++;
  }

  argv[0] = programStr;
  argc = 1;
  ap = lpCmd;

  /* XXX mdh - Should probably deal with quotes properly too */
  while (*ap && (argc < MAX_WIN_ARGS)) {
    argv[argc++] = ap;
    while (*ap && (*ap != ' ')) ap++;
    if (*ap == ' ') *ap++ = 0;
  }
  m_instance = hInst;

#else /* Not WIN_2_X */
/**********
 * main
 **********/
int main(argc, argv)
int  argc;
char **argv;
{
  Display *fromDpy;
  PSHADOW pShadow;

#endif /* WIN_2_X */

  XrmInitialize();
  ParseCommandLine(argc, argv);

#ifdef WIN_2_X
  if (fromDpyName != fromWinName)
    fromDpyName = XDisplayName(fromDpyName);
#else
  fromDpyName = XDisplayName(fromDpyName);
#endif

  toDpyName   = XDisplayName(toDpyName);
  if (!strcasecmp(toDpyName, fromDpyName)) {
    fprintf(stderr, "%s: display names are both %s\n", programStr, toDpyName);
    exit(1);
  }

  /* no OS independent way to stop Xlib from complaining via stderr,
     but can always pipe stdout/stderr to /dev/null */
  /* convert to real name: */
#ifdef WIN_2_X
  if (fromDpyName == fromWinName) {
    /* From is Windows, don't need to open */
    fromDpy = fromWin;
  } else
    /* This ugly hanging else... */
#endif /* WIN_2_X */
    /* ... qualifies this while in WIN_2_X case with an X source */
  while ((fromDpy = XOpenDisplay(fromDpyName)) == NULL) {
    if (!waitDpy) {
      fprintf(stderr, "%s - error: can not open display %s\n",
              programStr, fromDpyName);
      exit(2);
    } /* END if */
    sleep(10);
  } /* END while fromDpy */

  /* toDpy is always the first shadow */
  pShadow = (PSHADOW)malloc(sizeof(SHADOW));
  pShadow->name = toDpyName;
  /* link into the global list */
  pShadow->pNext = shadows;
  shadows = pShadow;

  /* initialize all of the shadows, including the toDpy */
  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
    if (!(pShadow->dpy = OpenAndCheckDisplay(pShadow->name)))
      exit(3);

#ifndef WIN_2_X
  /* set error handler,
     so that program does not abort on non-critcal errors */
  XSetErrorHandler(ErrorHandler);
#endif

    /* run the x2x loop */
  DoX2X(fromDpy, shadows->dpy);

  /* shut down gracefully */

#ifdef WIN_2_X
  /* Only close if it is a real X from display */
  if (fromDpy != fromWin)
    XCloseDisplay(fromDpy);
#else
  XCloseDisplay(fromDpy);
#endif

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
    XCloseDisplay(pShadow->dpy);
  exit(0);

} /* END main */

static Display *OpenAndCheckDisplay(name)
char *name;
{
  Display *openDpy;

  /* convert to real name: */
  name = XDisplayName(name);
  while ((openDpy = XOpenDisplay(name)) == NULL) {
    if (!waitDpy) {
      fprintf(stderr, "%s - error: can not open display %s\n",
              programStr, name);
      return NULL;
    } /* END if */
    sleep(10);
  } /* END while openDpy */

  if (!CheckTestExtension(openDpy)) {
    fprintf(stderr,
            "%s - error: display %s does not support the test extension\n",
            programStr, name);
    return NULL;
  }
  return (openDpy);

} /* END OpenAndCheckDisplay */

/**********
 * use standard X functions to parse the command line
 **********/
static void ParseCommandLine(argc, argv)
int  argc;
char **argv;
{
  int     arg;
  PSHADOW pShadow;
  extern  char *lawyerese;
  PSTICKY pNewSticky;
  KeySym  keysym;
  int     button;
  int     eventno;
  char    *keyname, *argptr;

  debug("programStr = %s\n", programStr);

  /* Clear button map */
  for (button = 0; button <= N_BUTTONS; button++)
    buttonmap[button][0] = NoSymbol;

  for (arg = 1; arg < argc; ++arg) {
#ifdef WIN_2_X
    if (!strcasecmp(argv[arg], "-fromWin")) {
      fromDpyName = fromWinName;
      /* XXX mdh - For now only support edge windows getting big */
      /* Note: -east will override correctly (even if earlier on the line) */
      doBig = True;
      if (doEdge == EDGE_NONE) doEdge = EDGE_WEST;
      doCapsLkHack = True;

      debug("fromDpyName = %s\n", fromDpyName);
    } else
      /* Note this else will qualify the if below... */
#endif /* WIN_2_X */
    if (!strcasecmp(argv[arg], "-from")) {
      if (++arg >= argc) Usage();
      fromDpyName = argv[arg];

      debug("fromDpyName = %s\n", fromDpyName);
    } else if (!strcasecmp(argv[arg], "-to")) {
      if (++arg >= argc) Usage();
      toDpyName = argv[arg];

      debug("toDpyName = %s\n", toDpyName);
    } else if (!strcasecmp(argv[arg], "-font")) {
      if (++arg >= argc) Usage();
      fontName = argv[arg];

      debug("fontName = %s\n", fontName);
    } else if (!strcasecmp(argv[arg], "-label")) {
      if (++arg >= argc) Usage();
      label = argv[arg];

      debug("label = %s\n", label);
    } else if (!strcasecmp(argv[arg], "-title")) {
      if (++arg > argc) Usage();
      title = argv[arg];

      debug("title = %s\n", title);
    } else if (!strcasecmp(argv[arg], "-geometry")) {
      if (++arg >= argc) Usage();
      geomStr = argv[arg];

      debug("geometry = %s\n", geomStr);
    } else if (!strcasecmp(argv[arg], "-wait")) {
      waitDpy = True;

      debug("will wait for displays\n");
    } else if (!strcasecmp(argv[arg], "-big")) {
      doBig = True;

      debug("will create big window on from display\n");
    } else if (!strcasecmp(argv[arg], "-nomouse")) {
      doMouse = False;

      debug("will not capture mouse (eek!)\n");
    } else if (!strcasecmp(argv[arg], "-nopointermap")) {
      doPointerMap = False;

      debug("will not do pointer mapping\n");
    } else if (!strcasecmp(argv[arg], "-north")) {
      doEdge = EDGE_NORTH;

      debug("\"from\" is on the north side of \"to\"\n");
    } else if (!strcasecmp(argv[arg], "-south")) {
      doEdge = EDGE_SOUTH;

      debug("\"from\" is on the south side of \"to\"\n");
    } else if (!strcasecmp(argv[arg], "-east")) {
      doEdge = EDGE_EAST;

      debug("\"from\" is on the east side of \"to\"\n");
    } else if (!strcasecmp(argv[arg], "-west")) {
      doEdge = EDGE_WEST;

      debug("\"from\" is on the west side of \"to\"\n");
    } else if (!strcasecmp(argv[arg], "-nosel")) {
      doSel = False;

      debug("will not transmit X selections between displays\n");
    } else if (!strcasecmp(argv[arg], "-noautoup")) {
      doAutoUp = False;

      debug("will not automatically lift keys and buttons\n");
    } else if (!strcasecmp(argv[arg], "-buttonblock")) {
      doBtnBlock = True;

      debug("mouse buttons down will block disconnects\n");
    } else if (!strcasecmp(argv[arg], "-capslockhack")) {
      doCapsLkHack = True;

      debug("behavior of CapsLock will be hacked\n");
    } else if (!strcasecmp(argv[arg], "-dpmsmouse")) {
      doDpmsMouse = True;

      debug("mouse movement wakes monitor\n");
    } else if (!strcasecmp(argv[arg], "-offset")) {
      if (++arg >= argc) Usage();
      logicalOffset = atoi(argv[arg]);

      debug("logicalOffset %d\n", logicalOffset);
    } else if (!strcasecmp(argv[arg], "-clipcheck")) {
      doClipCheck = True;

      debug("Clipboard type will be checked for XA_STRING\n");
    } else if (!strcasecmp(argv[arg], "-nocapslockhack")) {
      doCapsLkHack = False;

      debug("behavior of CapsLock will not be hacked\n");
    } else if (!strcasecmp(argv[arg], "-sticky")) {
      if (++arg >= argc) Usage();
      if ((keysym = XStringToKeysym(argv[arg])) != NoSymbol) {
        pNewSticky = (PSTICKY)malloc(sizeof(STICKY));
        pNewSticky->pNext  = stickies;
        pNewSticky->keysym = keysym;
        stickies = pNewSticky;

        debug("will press/release sticky key: %s\n", argv[arg]);
      } else {
        printf("x2x: warning: can't translate %s\n", argv[arg]);
      }
    } else if (!strcasecmp(argv[arg], "-buttonmap")) {
      if (++arg >= argc) Usage();
      button = atoi(argv[arg]);

      if ((button < 1) || (button > N_BUTTONS))
        printf("x2x: warning: invalid button %d\n", button);
      else if (++arg >= argc)
        Usage();
      else
      {
        debug("will map button %d to keysyms '%s'\n", button, argv[arg]);

        argptr  = argv[arg];
        eventno = 0;
        while ((keyname = strtok(argptr, " \t\n\r")) != NULL)
        {
          if ((keysym = XStringToKeysym(keyname)) == NoSymbol)
            printf("x2x: warning: can't translate %s\n", keyname);
          else if (eventno + 1 >= MAX_BUTTONMAPEVENTS)
            printf("x2x: warning: too many keys mapped to button %d\n",
                   button);
          else
            buttonmap[button][eventno++] = keysym;
          argptr = NULL;
        }
        buttonmap[button][eventno] = NoSymbol;
      }
    } else if (!strcasecmp(argv[arg], "-resurface")) {
      doResurface = True;

      debug("will resurface the trigger window when obscured\n");
    } else if (!strcasecmp(argv[arg], "-shadow")) {
      if (++arg >= argc) Usage();
      pShadow = (PSHADOW)malloc(sizeof(SHADOW));
      pShadow->name = argv[arg];

      /* into the global list of shadows */
      pShadow->pNext = shadows;
      shadows = pShadow;

    } else if (!strcasecmp(argv[arg], "-triggerw")) {
      if (++arg >= argc) Usage();
      triggerw = atoi(argv[arg]);
    } else if (!strcasecmp(argv[arg], "-copyright")) {
      printf(lawyerese);
    } else {
      Usage();
    } /* END if... */
  } /* END for */

} /* END ParseCommandLine */

static void Usage()
{
#ifdef WIN_2_X
  printf("Usage: x2x [-fromwin | -from <DISPLAY>][-to <DISPLAY>] options..\n");
#else
  printf("Usage: x2x [-to <DISPLAY> | -from <DISPLAY>] options...\n");
#endif
  printf("       -copyright\n");
  printf("       -font <FONTNAME>\n");
  printf("       -geometry <GEOMETRY>\n");
  printf("       -wait\n");
  printf("       -big\n");
  printf("       -buttonblock\n");
  printf("       -nomouse\n");
  printf("       -nopointermap\n");
  printf("       -north\n");
  printf("       -south\n");
  printf("       -east\n");
  printf("       -west\n");
  printf("       -nosel\n");
  printf("       -noautoup\n");
  printf("       -resurface\n");
  printf("       -capslockhack\n");
  printf("       -nocapslockhack\n");
  printf("       -clipcheck\n");
  printf("       -shadow <DISPLAY>\n");
  printf("       -sticky <sticky key>\n");
  printf("       -label <LABEL>\n");
  printf("       -title <TITLE>\n");
  printf("       -buttonmap <button#> \"<keysym> ...\"\n");
#ifdef WIN_2_X
  printf("       -offset [-]<pixel offset of \"to\">\n");
  printf("WIN_2_X build allows Windows or X as -from display\n");
  printf("Note that -fromwin sets default to -big -west -capslockhack\n");
  printf("A Windows shortcut of the form:\n");
  printf("C:\\cygwin\\usr\\X11R6\\bin\\run.exe /usr/X11R6/bin/x2x -fromwin -to <to> -east\n");
  printf("Should work to start the app from the desktop or start menu, but\n");
  printf("c:\\cygwin\\bin may need to be added to the PATH to get cygwin1.dll\n");
#endif
  exit(4);

} /* END Usage */

/**********
 * call the library to check for the test extension
 **********/
static Bool CheckTestExtension(dpy)
Display  *dpy;
{
  int eventb, errorb;
  int vmajor, vminor;

  return (XTestQueryExtension(dpy, &eventb, &errorb, &vmajor, &vminor));

} /* END CheckTestExtension */

#ifndef WIN_2_X
int ErrorHandler(Display *disp, XErrorEvent *event) {

  int bufflen = 1024;
  char *buff;
  buff = malloc(bufflen);

  XGetErrorText(disp,event->error_code,buff,bufflen);
  debug("x2x:ErrHandler(): Display:`%s` \t error_code:`%i` \n",
        XDisplayName(NULL),event->error_code);
  printf(" %s \n",buff);
  free(buff);
  return True;
}
#endif

#define X2X_DISCONNECTED    0
#define X2X_AWAIT_RELEASE   1
#define X2X_CONNECTED       2
#define X2X_CONN_RELEASE    3

static void DoX2X(fromDpy, toDpy)
Display *fromDpy;
Display *toDpy;
{
  DPYINFO   dpyInfo;
  int       nfds;
  fd_set    fdset;
  Bool      fromPending;
  int       fromConn, toConn;

  /* set up displays */
  dpyInfo.fromDpy = fromDpy;
  dpyInfo.toDpy = toDpy;
  InitDpyInfo(&dpyInfo);
  RegisterEventHandlers(&dpyInfo);

  /* set up for select */
#ifdef WIN_2_X
  fromConn = (fromDpy == fromWin) ? 0 : XConnectionNumber(fromDpy);
#else
  fromConn = XConnectionNumber(fromDpy);
#endif /* WIN_2_X */

  toConn   = XConnectionNumber(toDpy);
  nfds = (fromConn > toConn ? fromConn : toConn) + 1;

#ifdef WIN_2_X
  if (fromDpy == fromWin) {
    MSG                msg;                /* A Win32 message structure. */
    int         nowQuit;

    nowQuit = 0;

    /* XXX mdh - This is not quite right becaue it only does To events */
    /* XXX mdh - when there are From events                            */
    /* This is mostly ok, but can cause windows->X paste to take a while */
    /* As a compromise, try a 1 second tick to cause polling */

    SetTimer(dpyInfo.bigwindow, 1, 1000 /* in ms */, NULL);
    /* GetMessage blocks until the next Windows event */
    /* It returns 0 if the app should quit */
    while (!nowQuit && GetMessage (&msg, NULL, 0, 0)) {
      /* XXX mdh - Translate may not be needed if all Key processing is */
      /* XXX mdh - done using the Key events rather than CHAR events    */
      TranslateMessage (&msg);
      DispatchMessage (&msg);
      /* Done a Windows event, now loop while -to has something */
      while (XPending(toDpy) || dpyInfo.expectSelNotify) {
        if (ProcessEvent(toDpy, &dpyInfo)) { /* done! */
          nowQuit = 1;
          break;
        }
        /* PeekMessage is a non-blocking version of GetMessage */
        /* But it returns 0 for no messages rather than for quit */
        if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
          /* need explicit check for quit */
          if (msg.message == WM_QUIT) {
            nowQuit = 1;
            break; /* from the while(XPending... )*/
          }
          /* XXX mdh - see above */
          TranslateMessage (&msg);
          DispatchMessage (&msg);
        }
      }
    }
  } else
    /* Again, the else qualifies the while below */
#endif /* WIN_2_X */
  while (True) { /* FOREVER */
    if ((fromPending = XPending(fromDpy)))
      if (ProcessEvent(fromDpy, &dpyInfo)) /* done! */
        break;

    if (XPending(toDpy)) {
      if (ProcessEvent(toDpy, &dpyInfo)) /* done! */
        break;
    } else if (!fromPending) {
      FD_ZERO(&fdset);
      FD_SET(fromConn, &fdset);
      FD_SET(toConn, &fdset);
      select(nfds, &fdset, NULL, NULL, NULL);
    }

  } /* END FOREVER */

} /* END DoX2X() */

static void InitDpyInfo(pDpyInfo)
PDPYINFO pDpyInfo;
{
  Display   *fromDpy, *toDpy;
  Screen    *fromScreen;
  long      black, white;
  int       fromHeight, fromWidth, toHeight, toWidth;
  Pixmap    nullPixmap;
  XColor    dummyColor;
  Window    root, trigger, big, rret, toRoot, propWin;
  short     *xTable, *yTable; /* short: what about dimensions > 2^15? */
  int       *heights, *widths;
  int       counter;
  int       nScreens, screenNum;
  int       twidth, theight, tascent; /* text dimensions */
  int       xoff, yoff; /* window offsets */
  unsigned int width, height; /* window width, height */
  int       geomMask;                /* mask returned by parse */
  int       gravMask;
  int       gravity;
  int       xret, yret, wret, hret, bret, dret;
  XSetWindowAttributes xswa;
  XSizeHints *xsh;
  int       eventMask;
  GC        textGC;
  char      *windowName;
  Font      fid;
  PSHADOW   pShadow;
  int       triggerLoc;
  Bool      vertical;

  /* cache commonly used variables */
  fromDpy = pDpyInfo->fromDpy;
  toDpy   = pDpyInfo->toDpy;
  pDpyInfo->toDpyXtra.propWin = (Window) 0;

#ifdef WIN_2_X
  gravity = NorthWestGravity;   /* keep compliler happy */
  pDpyInfo->unreasonableCount = 0;

  if (fromDpy == fromWin) {

    fromWidth=GetSystemMetrics(SM_CXSCREEN);
    fromHeight=GetSystemMetrics(SM_CYSCREEN);

    POINT pt;

    pDpyInfo->screenRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    pDpyInfo->screenRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    pDpyInfo->screenRect.right = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    pDpyInfo->screenRect.bottom = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    pDpyInfo->screenHeight = pDpyInfo->screenRect.bottom - pDpyInfo->screenRect.top;
    pDpyInfo->screenWidth = pDpyInfo->screenRect.right - pDpyInfo->screenRect.left;


    // Guess a a point at or near the monitor we want.
    if (doEdge == EDGE_NORTH || doEdge == EDGE_SOUTH)
    {
      pt.x = (pDpyInfo->screenRect.right - pDpyInfo->screenRect.left) / 2;
      pt.y = (doEdge == EDGE_SOUTH) ? pDpyInfo->screenRect.bottom - 1 : pDpyInfo->screenRect.top;;
    }
    else
    {
      pt.x = (doEdge == EDGE_EAST) ? pDpyInfo->screenRect.right - 1 : pDpyInfo->screenRect.left;
      pt.y = (pDpyInfo->screenRect.bottom -  pDpyInfo->screenRect.top) / 2;
    }

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    MONITORINFO monInfo;
    monInfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(mon, &monInfo);

    pDpyInfo->monitorRect = monInfo.rcMonitor;

    /* these should not be used, but keep compiler happy */
    fromScreen = (Screen *) 0;
    black = white = 0;
    root = pDpyInfo->root = None;
  } else {
    fromScreen = XDefaultScreenOfDisplay(fromDpy);
    black      = XBlackPixelOfScreen(fromScreen);
    white      = XWhitePixelOfScreen(fromScreen);
    fromHeight = XHeightOfScreen(fromScreen);
    fromWidth  = XWidthOfScreen(fromScreen);
    root       = pDpyInfo->root      = XDefaultRootWindow(fromDpy);
  }
  toRoot     = XDefaultRootWindow(toDpy);
  nScreens   = pDpyInfo->nScreens  = XScreenCount(toDpy);
  vertical   = pDpyInfo->vertical = (doEdge == EDGE_NORTH
                                      || doEdge == EDGE_SOUTH);
#else
  fromScreen = XDefaultScreenOfDisplay(fromDpy);
  black      = XBlackPixelOfScreen(fromScreen);
  white      = XWhitePixelOfScreen(fromScreen);
  fromHeight = XHeightOfScreen(fromScreen);
  fromWidth  = XWidthOfScreen(fromScreen);
  toRoot     = XDefaultRootWindow(toDpy);

  /* values also in dpyinfo */
  root       = pDpyInfo->root      = XDefaultRootWindow(fromDpy);
  nScreens   = pDpyInfo->nScreens  = XScreenCount(toDpy);
  vertical   = pDpyInfo->vertical = (doEdge == EDGE_NORTH
                                      || doEdge == EDGE_SOUTH);
#endif

#ifdef WIN_2_X
  if (fromDpy != fromWin) {
#endif
    pDpyInfo->fromDpyUtf8String = XInternAtom(fromDpy, UTF8_STRING, False);
#ifdef WIN_2_X
  }
#endif
  pDpyInfo->toDpyUtf8String = XInternAtom(toDpy, UTF8_STRING, False);

  /* other dpyinfo values */
  pDpyInfo->mode        = X2X_DISCONNECTED;
  pDpyInfo->unreasonableDelta = (vertical ? fromHeight : fromWidth) / 2;
  pDpyInfo->pFakeThings = NULL;

  /* window init structures */
  xswa.override_redirect = True;
  xsh = XAllocSizeHints();
  eventMask = KeyPressMask | KeyReleaseMask;

  /* cursor locations for moving between screens */
  pDpyInfo->fromIncrCoord = triggerw;
  pDpyInfo->fromDecrCoord = (vertical ? fromHeight : fromWidth) - triggerw - 1;
  if (doEdge) { /* edge triggers x2x */
#ifdef WIN_2_X
    if (fromDpy == fromWin) {
      /* keep compiler happy */
      nullPixmap = 0;
      pDpyInfo->grabCursor = 0;
    } else {
#endif
    nullPixmap = XCreatePixmap(fromDpy, root, 1, 1, 1);
    eventMask |= EnterWindowMask;
    pDpyInfo->grabCursor =
      XCreatePixmapCursor(fromDpy, nullPixmap, nullPixmap,
                          &dummyColor, &dummyColor, 0, 0);
#ifdef WIN_2_X
    }
#endif

    /* trigger window location */
    if (doEdge == EDGE_NORTH) {
      triggerLoc = 0;
      pDpyInfo->fromConnCoord = fromHeight - triggerw - 1;
      pDpyInfo->fromDiscCoord = triggerw;
    } else if (doEdge == EDGE_SOUTH) {
      triggerLoc = fromHeight - triggerw;
      pDpyInfo->fromConnCoord = 1;
      pDpyInfo->fromDiscCoord = triggerLoc - 1;
    } else if (doEdge == EDGE_EAST) {
      triggerLoc = fromWidth - triggerw;
      pDpyInfo->fromConnCoord = 1;
      pDpyInfo->fromDiscCoord = triggerLoc - 1;
    } else /* doEdge == EDGE_WEST */ {
      triggerLoc = 0;
      pDpyInfo->fromConnCoord = fromWidth - triggerw - 1;
      pDpyInfo->fromDiscCoord = triggerw;
    } /* END if doEdge == ... */

    xswa.background_pixel = black;

#ifdef WIN_2_X
    if (fromDpy == fromWin) {

      /* Create both trigger and big windows here */
      /* This code is based on Win2VBC/vncviewer ClientConnection.cpp */
      WNDCLASS wndclass;
      const DWORD winstyle = 0;

      wndclass.style                = 0;
      wndclass.lpfnWndProc        = WinProcessMessage;
      wndclass.cbClsExtra        = 0;
      wndclass.cbWndExtra        = 0;
      wndclass.hInstance        = m_instance;
      /* XXX mdh - For now just use system provided resources */
      wndclass.hIcon                = LoadIcon(NULL, IDI_APPLICATION);
      // wndclass.hCursor        = LoadCursor(NULL, IDC_NO);
      wndclass.hCursor                = LoadCursor(m_instance, MAKEINTRESOURCE(IDC_NOCURSOR));
      wndclass.hbrBackground        = (HBRUSH) GetStockObject(BLACK_BRUSH);
      wndclass.lpszMenuName        = (const TCHAR *) NULL;
      wndclass.lpszClassName        = fromWinName;

      RegisterClass(&wndclass);


     pDpyInfo->bigwindow = CreateWindowEx(
         WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
         fromWinName,
         "x2x_big",
         winstyle,
         0,
         0,
         fromWidth,
         fromHeight,
         NULL,                // Parent handle
         NULL,                // Menu handle
         m_instance,
         NULL);

      pDpyInfo->edgewindow = CreateWindowEx(
         WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
         fromWinName,
         "x2x_edge",
         winstyle,
         /* Next 4 are x, y, width, height */
         (doEdge == EDGE_EAST) ? pDpyInfo->monitorRect.right -1: pDpyInfo->monitorRect.left,
         (doEdge == EDGE_SOUTH) ? pDpyInfo->monitorRect.bottom - 1 : pDpyInfo->monitorRect.top,
         (pDpyInfo->vertical) ? pDpyInfo->monitorRect.right - pDpyInfo->monitorRect.left : 1,
         (pDpyInfo->vertical) ? 1 : pDpyInfo->monitorRect.bottom - pDpyInfo->monitorRect.top,
         pDpyInfo->bigwindow, // Parent handle
         NULL,                // Menu handle
         m_instance,
         NULL);


      ShowWindow(pDpyInfo->bigwindow, SW_HIDE);
      ShowWindow(pDpyInfo->edgewindow, SW_HIDE);
      pDpyInfo->fromWidth = fromWidth;
      pDpyInfo->fromHeight = fromHeight;
      pDpyInfo->wdelta = 0;

      // record which client created this window
      SetWindowLong(pDpyInfo->bigwindow, GWL_USERDATA, (LONG) pDpyInfo);
      SetWindowLong(pDpyInfo->edgewindow, GWL_USERDATA, (LONG) pDpyInfo);

      // Set up clipboard watching
      // We want to know when the clipboard changes, so
      // insert ourselves in the viewer chain. But doing
      // this will cause us to be notified immediately of
      // the current state.
      // We don't want to send that.
      pDpyInfo->expectOwnClip = 0;
      if (doSel) {
        // pDpyInfo->initialClipboardSeen = False;
        pDpyInfo->winSelText = NULL;
        pDpyInfo->hwndNextViewer = SetClipboardViewer(pDpyInfo->edgewindow);
      }

      pDpyInfo->onedge = 0;
      MoveWindowToEdge(pDpyInfo);
      /* Keep compile happy */
      trigger = 0;
    } else {
#endif
    /* fromWidth - 1 doesn't seem to work for some reason */
    /* Use triggerw offsets so that if an x2x is running
       along the left edge and along the north edge, both with
       -resurface, we don't get a feedback loop of them each
       fighting to be on top.
        --09/27/99 Greg J. Badros <gjb@cs.washington.edu> */
    /* also, make it an InputOnly window so I don't lose
       screen real estate --09/29/99 gjb */
    trigger = pDpyInfo->trigger =
      XCreateWindow(fromDpy, root,
                    vertical ? triggerw : triggerLoc,
                    vertical ? triggerLoc : triggerw,
                    vertical ? fromWidth - (2*triggerw) : triggerw,
                    vertical ? triggerw : fromHeight - (2*triggerw),
                    0, 0, InputOnly, 0,
                    CWOverrideRedirect, &xswa);
#ifdef WIN_2_X
    }
#endif
    fid = 0;
  } else { /* normal window for text: do size grovelling */
    pDpyInfo->grabCursor = XCreateFontCursor(fromDpy, XC_exchange);
    eventMask |= StructureNotifyMask | ExposureMask;
    if (doMouse) eventMask |= ButtonPressMask | ButtonReleaseMask;

    if (label == NULL)
      label = toDpyName;
    /* determine size of text */
    if (((fid = XLoadFont(fromDpy, fontName)) != 0) ||
        ((fid = XLoadFont(fromDpy, defaultFN)) != 0) ||
        ((fid = XLoadFont(fromDpy, "fixed")) != 0)) {
      /* have a font */
      int ascent, descent, direction;
      XCharStruct overall;

      XQueryTextExtents(fromDpy, fid, label, strlen(label),
                        &direction, &ascent, &descent, &overall);
      twidth = -overall.lbearing + overall.rbearing;
      theight = ascent + descent;
      tascent = ascent;

      textGC = pDpyInfo->textGC = XCreateGC(fromDpy, root, 0, NULL);
      XSetState(fromDpy, textGC, black, white, GXcopy, AllPlanes);
      XSetFont(fromDpy, textGC, fid);

    } else { /* should not have to execute this clause: */
      twidth = theight = 100; /* default window size */
    } /* END if have a font ... else ... */

    /* determine size of window */
    xoff = yoff = 0;
    width = twidth + 4; /* XXX gap around text -- should be configurable */
    height = theight + 4;
    geomMask = XParseGeometry(geomStr, &xoff, &yoff, &width, &height);
    switch (gravMask = (geomMask & (XNegative | YNegative))) {
    case (XNegative | YNegative): gravity = SouthEastGravity; break;
    case XNegative:               gravity = NorthEastGravity; break;
    case YNegative:               gravity = SouthWestGravity; break;
    default:                      gravity = NorthWestGravity; break;
    }
    if (gravMask) {
      XGetGeometry(fromDpy, root,
                   &rret, &xret, &yret, &wret, &hret, &bret, &dret);
      if ((geomMask & (XValue | XNegative)) == (XValue | XNegative)){
        xoff = wret - width + xoff;
      }
      if ((geomMask & (YValue | YNegative)) == (YValue | YNegative)) {
        yoff = hret - height + yoff;
      }
    } /* END if geomMask */

    trigger = pDpyInfo->trigger =
      XCreateSimpleWindow(fromDpy, root, xoff, yoff, width, height,
                          0, black, white);
  } /* END if doEdge ... else ...*/

#ifdef WIN_2_X
  if (fromDpy != fromWin) {
#endif
  /* size hints stuff: */
  xsh->x           = xoff;
  xsh->y           = yoff;
  xsh->base_width  = width;
  xsh->base_height = height;
  xsh->win_gravity = gravity;
  xsh->flags       = (PPosition|PBaseSize|PWinGravity);
  XSetWMNormalHints(fromDpy, trigger, xsh);

  if (title) {
    windowName = title;
  } else {
    windowName = (char *)malloc(strlen(programStr) + strlen(toDpyName) + 2);
    sprintf(windowName, "%s %s", programStr, toDpyName);
  }

  XStoreName(fromDpy, trigger, windowName);
  XSetIconName(fromDpy, trigger, windowName);

  /* register for WM_DELETE_WINDOW protocol */
  pDpyInfo->wmpAtom = XInternAtom(fromDpy, "WM_PROTOCOLS", True);
  pDpyInfo->wmdwAtom = XInternAtom(fromDpy, "WM_DELETE_WINDOW", True);
  XSetWMProtocols(fromDpy, trigger, &(pDpyInfo->wmdwAtom), 1);

  /* mdh - Put in Chaiken's change to make this InputOnly */
  if (doBig) {
    big = pDpyInfo->big =
      XCreateWindow(fromDpy, root, 0, 0, fromWidth, fromHeight, 0,
                    0, InputOnly, 0, CWOverrideRedirect, &xswa);
    /* size hints stuff: */
    xsh->x           = 0;
    xsh->y           = 0;
    xsh->base_width  = fromWidth;
    xsh->base_height = fromHeight;
    xsh->min_width   = fromWidth;
    xsh->min_height  = fromHeight;
    xsh->flags       = (PMinSize|PPosition|PBaseSize);

    XSetWMNormalHints(fromDpy, big, xsh);
    XStoreName(fromDpy, big, windowName);
    XSetIconName(fromDpy, big, windowName);
  } else {
    pDpyInfo->big = None;
  }

  XFree((char *) xsh);

  if (!title)
    free(windowName);
#ifdef WIN_2_X
  }
#endif

  /* conversion stuff */
  pDpyInfo->toScreen = (doEdge == EDGE_WEST || doEdge == EDGE_NORTH)
                        ? (nScreens - 1) : 0;

  /* construct table lookup for screen coordinate conversion */
  pDpyInfo->xTables = (short **)malloc(sizeof(short *) * nScreens);
  pDpyInfo->yTables = (short **)malloc(sizeof(short *) * nScreens);
  heights = (int *)malloc(sizeof(int *) * nScreens);
  widths  = (int *)malloc(sizeof(int *) * nScreens);

  for (screenNum = 0; screenNum < nScreens; ++screenNum) {
    widths[screenNum] = toWidth  =
      XWidthOfScreen(XScreenOfDisplay(toDpy, screenNum));
    heights[screenNum] = toHeight =
      XHeightOfScreen(XScreenOfDisplay(toDpy, screenNum));

    pDpyInfo->xTables[screenNum] = xTable =
      (short *)malloc(sizeof(short) * fromWidth);
    pDpyInfo->yTables[screenNum] = yTable =
      (short *)malloc(sizeof(short) * fromHeight);

    /* vertical conversion table */
    for (counter = 0; counter < fromHeight; ++counter)
      yTable[counter] = (counter * toHeight) / fromHeight;

    /* horizontal conversion table entries */
    for (counter = 0; counter < fromWidth; ++counter)
      xTable[counter] = (counter * toWidth) / fromWidth;

    /* adjustment for boundaries */
    if (vertical) {
      if ((screenNum != 0) || (doEdge == EDGE_SOUTH))
        yTable[0] = COORD_DECR;
      if (((screenNum + 1) < nScreens) || (doEdge == EDGE_NORTH)) {
        yTable[fromHeight - 1] = COORD_INCR;
        /* work-around for bug: on at least one tested screen, cursor
           never moved past fromWidth - 2  (I'll assume this might apply
           in the vertical case, too. --cpbs) */
        yTable[fromHeight - 2] = COORD_INCR;
      }
    } else {
      if ((screenNum != 0) || (doEdge == EDGE_EAST))
        xTable[0] = COORD_DECR;
      if (((screenNum + 1) < nScreens) || (doEdge == EDGE_WEST)) {
        xTable[fromWidth - 1] = COORD_INCR;
        /* work-around for bug: on at least one tested screen, cursor
           never moved past fromWidth - 2 */
        xTable[fromWidth - 2] = COORD_INCR;
      }
    }

  } /* END for screenNum */

  free(heights);
  free(widths);

  /* always create propWin for events from toDpy */
  propWin = XCreateWindow(toDpy, toRoot, 0, 0, 1, 1, 0, 0, InputOutput,
                          CopyFromParent, 0, NULL);
  pDpyInfo->toDpyXtra.propWin = propWin;
  debug("Create window %x on todpy\n", (unsigned int)propWin);
  /* initialize pointer mapping */
  RefreshPointerMapping(toDpy, pDpyInfo);

  if (doSel) {
    pDpyInfo->sDpy = NULL;
    pDpyInfo->sTime = 0;

    pDpyInfo->fromDpyXtra.otherDpy   = toDpy;
    pDpyInfo->fromDpyXtra.sState     = SELSTATE_OFF;
#ifdef WIN_2_X
  if (fromDpy != fromWin) {
#endif
    pDpyInfo->fromDpyXtra.pingAtom   = XInternAtom(fromDpy, pingStr, False);
#ifdef WIN_2_X
  }
#endif
    pDpyInfo->fromDpyXtra.pingInProg = False;
    pDpyInfo->fromDpyXtra.propWin    = trigger;
    eventMask |= PropertyChangeMask;

    pDpyInfo->toDpyXtra.otherDpy     = fromDpy;
    pDpyInfo->toDpyXtra.sState       = SELSTATE_OFF;
    pDpyInfo->toDpyXtra.pingAtom     = XInternAtom(toDpy, pingStr, False);
    pDpyInfo->toDpyXtra.pingInProg   = False;
#ifdef WIN_2_X
    if (fromDpy != fromWin)
#endif
      XSelectInput(toDpy, propWin, PropertyChangeMask);
    XSetSelectionOwner(toDpy, XA_PRIMARY, propWin, CurrentTime);
#ifdef WIN_2_X
    debug("SelectionOwner to propWin %x\n", (unsigned int)propWin);
    pDpyInfo->owntoXsel = 1;
    pDpyInfo->expectSelNotify = 0;
    pDpyInfo->expectOwnClip = 0;
    pDpyInfo->winSSave = 0;
#endif
  } /* END if doSel */

  if (doResurface) /* get visibility events */
    eventMask |= VisibilityChangeMask;

#ifdef WIN_2_X
  if (fromDpy != fromWin) {
#endif
  XSelectInput(fromDpy, trigger, eventMask);
  pDpyInfo->eventMask = eventMask; /* save for future munging */
  if (doSel) XSetSelectionOwner(fromDpy, XA_PRIMARY, trigger, CurrentTime);
  XMapRaised(fromDpy, trigger);
  if ((pDpyInfo->fid = fid)) { /* paint text */
    /* position text */
    pDpyInfo->twidth = twidth;
    pDpyInfo->theight = theight;
    pDpyInfo->tascent = tascent;
    pDpyInfo->width = width;
    pDpyInfo->height = height;

    XDrawImageString(fromDpy, trigger, textGC,
                     MAX(0, ((width - twidth) / 2)),
                     MAX(0, ((height - theight) / 2)) + tascent,
                     label, strlen(label));
  } /* END if font */
#ifdef WIN_2_X
  }
#endif

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
    XTestGrabControl(pShadow->dpy, True); /* impervious to grabs! */

} /* END InitDpyInfo */

static void DoConnect(pDpyInfo)
PDPYINFO pDpyInfo;
{
  Display *fromDpy = pDpyInfo->fromDpy;
  Window  trigger = pDpyInfo->trigger;

  PSHADOW   pShadow;

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
    DPMSForceLevel(pShadow->dpy, DPMSModeOn);
    XFlush(pShadow->dpy);
  }

  debug("connecting\n");
  pDpyInfo->mode = X2X_CONNECTED;

#ifdef WIN_2_X
  assert (fromDpy != fromWin);
#endif
  if (pDpyInfo->big != None) XMapRaised(fromDpy, pDpyInfo->big);
  XGrabPointer(fromDpy, trigger, True,
               PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
               GrabModeAsync, GrabModeAsync,
               None, pDpyInfo->grabCursor, CurrentTime);
  XGrabKeyboard(fromDpy, trigger, True,
                GrabModeAsync, GrabModeAsync,
                CurrentTime);
  XSelectInput(fromDpy, trigger, pDpyInfo->eventMask | PointerMotionMask);
  XFlush(fromDpy);
} /* END DoConnect */

static void DoDisconnect(pDpyInfo)
PDPYINFO pDpyInfo;
{
  Display *fromDpy = pDpyInfo->fromDpy;
  PDPYXTRA pDpyXtra;

  debug("disconnecting\n");
  pDpyInfo->mode = X2X_DISCONNECTED;
#ifdef WIN_2_X
  assert (fromDpy != fromWin);
#endif
  if (pDpyInfo->big != None) XUnmapWindow(fromDpy, pDpyInfo->big);
  XUngrabKeyboard(fromDpy, CurrentTime);
  XUngrabPointer(fromDpy, CurrentTime);
  XSelectInput(fromDpy, pDpyInfo->trigger, pDpyInfo->eventMask);

  if (doSel) {
    pDpyXtra = GETDPYXTRA(fromDpy, pDpyInfo);
    if (pDpyXtra->sState == SELSTATE_ON) {
      XSetSelectionOwner(fromDpy, XA_PRIMARY, pDpyXtra->propWin, CurrentTime);
    }
  } /* END if */

  XFlush(fromDpy);

  /* force normal state on to display: */
  if (doAutoUp)
    FakeThingsUp(pDpyInfo);
} /* END DoDisconnect */

static void RegisterEventHandlers(pDpyInfo)
PDPYINFO pDpyInfo;
{
  Display *fromDpy = pDpyInfo->fromDpy;
  Window  trigger = pDpyInfo->trigger;
  Display *toDpy;
  Window  propWin;

#define XSAVECONTEXT(A, B, C, D) XSaveContext(A, B, C, (XPointer)(D))

#ifdef WIN_2_X
  if (fromDpy != fromWin) {
#endif
  XSAVECONTEXT(fromDpy, trigger, MotionNotify,    ProcessMotionNotify);
  XSAVECONTEXT(fromDpy, trigger, Expose,          ProcessExpose);
  XSAVECONTEXT(fromDpy, trigger, EnterNotify,     ProcessEnterNotify);
  XSAVECONTEXT(fromDpy, trigger, ButtonPress,     ProcessButtonPress);
  XSAVECONTEXT(fromDpy, trigger, ButtonRelease,   ProcessButtonRelease);
  XSAVECONTEXT(fromDpy, trigger, KeyPress,        ProcessKeyEvent);
  XSAVECONTEXT(fromDpy, trigger, KeyRelease,      ProcessKeyEvent);
  XSAVECONTEXT(fromDpy, trigger, ConfigureNotify, ProcessConfigureNotify);
  XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
  XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
  XSAVECONTEXT(fromDpy, trigger, ClientMessage,   ProcessClientMessage);
  XSAVECONTEXT(fromDpy, None,    MappingNotify,   ProcessMapping);


  if (doResurface)
    XSAVECONTEXT(fromDpy, trigger, VisibilityNotify, ProcessVisibility);
#ifdef WIN_2_X
  }
#endif

  toDpy = pDpyInfo->toDpy;
  propWin = pDpyInfo->toDpyXtra.propWin;
  XSAVECONTEXT(toDpy, None, MappingNotify, ProcessMapping);

  if (doSel) {
#ifdef WIN_2_X
    if (fromDpy != fromWin) {
#endif
    XSAVECONTEXT(fromDpy, trigger, SelectionRequest, ProcessSelectionRequest);
    XSAVECONTEXT(fromDpy, trigger, PropertyNotify,   ProcessPropertyNotify);
    XSAVECONTEXT(fromDpy, trigger, SelectionNotify,  ProcessSelectionNotify);
    XSAVECONTEXT(fromDpy, trigger, SelectionClear,   ProcessSelectionClear);
    XSAVECONTEXT(toDpy,   propWin, SelectionRequest, ProcessSelectionRequest);
    XSAVECONTEXT(toDpy,   propWin, PropertyNotify,   ProcessPropertyNotify);
    XSAVECONTEXT(toDpy,   propWin, SelectionNotify,  ProcessSelectionNotify);
    XSAVECONTEXT(toDpy,   propWin, SelectionClear,   ProcessSelectionClear);
#ifdef WIN_2_X
    } else {
      XSAVECONTEXT(toDpy, propWin, SelectionRequest, ProcessSelectionRequestW);
      // XSAVECONTEXT(toDpy, propWin, PropertyNotify,  ProcessPropertyNotifyW);
      XSAVECONTEXT(toDpy, propWin, SelectionNotify,ProcessSelectionNotifyW);
      XSAVECONTEXT(toDpy, propWin, SelectionClear,   ProcessSelectionClearW);
    }
#endif
  } /* END if doSel */

} /* END RegisterEventHandlers */

static Bool ProcessEvent(dpy, pDpyInfo)
Display  *dpy;
PDPYINFO pDpyInfo;
{
  XEvent    ev;
  XAnyEvent *pEv = (XAnyEvent *)&ev;
  HANDLER   handler;

#define XFINDCONTEXT(A, B, C, D) XFindContext(A, B, C, (XPointer *)(D))

  XNextEvent(dpy, &ev);
  handler = 0;
  if ((!XFINDCONTEXT(dpy, pEv->window, pEv->type, &handler)) ||
      (!XFINDCONTEXT(dpy, None, pEv->type, &handler))) {
    /* have handler */
    return ((*handler)(dpy, pDpyInfo, &ev));
  } else {
    debug("no handler for window 0x%x, event type %d\n",
           (unsigned int)pEv->window, pEv->type);
  } /* END if/else */

  return False;

} /* END ProcessEvent */

static Bool ProcessMotionNotify(unused, pDpyInfo, pEv)
Display  *unused;
PDPYINFO pDpyInfo;
XMotionEvent *pEv; /* caution: might be pseudo-event!!! */
{
  /* Note: ProcessMotionNotify is sometimes called from inside x2x to
   *       simulate a motion event.  Any new references to pEv fields
   *       must be checked carefully!
   */

  int       toScreenNum;
  PSHADOW   pShadow;
  int       toCoord, fromCoord, delta;
  Display   *fromDpy;
  Bool      bAbortedDisconnect;
  Bool      vert;

  vert = pDpyInfo->vertical;

  /* find the screen */
  toScreenNum = pDpyInfo->toScreen;
  fromCoord = vert ? pEv->y_root : pEv->x_root;

  /* check to make sure the cursor is still on the from screen */
  if (!(pEv->same_screen)) {
    toCoord = (pDpyInfo->lastFromCoord < fromCoord) ? COORD_DECR : COORD_INCR;
  } else {
    toCoord = (vert?pDpyInfo->yTables:pDpyInfo->xTables)[toScreenNum][fromCoord];

    /* sanity check motion: necessary for nondeterminism surrounding warps */
    delta = pDpyInfo->lastFromCoord - fromCoord;
    if (delta < 0) delta = -delta;
    if (delta > pDpyInfo->unreasonableDelta) return False;
  }

  if (SPECIAL_COORD(toCoord) != 0) { /* special coordinate */
    bAbortedDisconnect = False;
    if (toCoord == COORD_INCR) {
      if (toScreenNum != (pDpyInfo->nScreens - 1)) { /* next screen */
        toScreenNum = ++(pDpyInfo->toScreen);
        fromCoord = pDpyInfo->fromIncrCoord;
        toCoord = (vert?pDpyInfo->yTables:pDpyInfo->xTables)[toScreenNum][fromCoord];
      } else { /* disconnect! */
        if (doBtnBlock &&
            (pEv->state & (Button1Mask | Button2Mask | Button3Mask |
                           Button4Mask | Button5Mask)))
          bAbortedDisconnect = True;
        else {
          DoDisconnect(pDpyInfo);
          fromCoord = pDpyInfo->fromDiscCoord;
        }
        toCoord = (vert?pDpyInfo->yTables:pDpyInfo->xTables)[toScreenNum][pDpyInfo->fromConnCoord];
      }
    } else { /* DECR */
      if (toScreenNum != 0) { /* previous screen */
        toScreenNum = --(pDpyInfo->toScreen);
        fromCoord = pDpyInfo->fromDecrCoord;
        toCoord = (vert?pDpyInfo->yTables:pDpyInfo->xTables)[toScreenNum][fromCoord];
      } else { /* disconnect! */
        if (doBtnBlock &&
            (pEv->state & (Button1Mask | Button2Mask | Button3Mask |
                           Button4Mask | Button5Mask)))
          bAbortedDisconnect = True;
        else {
          DoDisconnect(pDpyInfo);
          fromCoord = pDpyInfo->fromDiscCoord;
        }
        toCoord = (vert?pDpyInfo->yTables:pDpyInfo->xTables)[toScreenNum][pDpyInfo->fromConnCoord];
      }
    } /* END if toCoord */
    if (!bAbortedDisconnect) {
      fromDpy = pDpyInfo->fromDpy;
      XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                   vert ? pEv->x_root : fromCoord,
                   vert ? fromCoord : pEv->y_root);
      XFlush(fromDpy);
    }
  } /* END if SPECIAL_COORD */
  pDpyInfo->lastFromCoord = fromCoord;

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
    if (doDpmsMouse)
    {
      DPMSForceLevel(pShadow->dpy, DPMSModeOn);
    }
      
    XTestFakeMotionEvent(pShadow->dpy, toScreenNum,
                      vert?pDpyInfo->xTables[toScreenNum][pEv->x_root]:toCoord,
                      vert?toCoord:pDpyInfo->yTables[toScreenNum][pEv->y_root],
                      0);
    XFlush(pShadow->dpy);
  } /* END for */

  return False;

} /* END ProcessMotionNotify */

static Bool ProcessExpose(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XExposeEvent *pEv;
{
  XClearWindow(pDpyInfo->fromDpy, pDpyInfo->trigger);
  if (pDpyInfo->fid)
    XDrawImageString(pDpyInfo->fromDpy, pDpyInfo->trigger, pDpyInfo->textGC,
                     MAX(0,((pDpyInfo->width - pDpyInfo->twidth) / 2)),
                     MAX(0,((pDpyInfo->height - pDpyInfo->theight) / 2)) +
                     pDpyInfo->tascent, label, strlen(label));

  return False;

} /* END ProcessExpose */

static Bool ProcessEnterNotify(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XCrossingEvent *pEv;
{
  Display *fromDpy = pDpyInfo->fromDpy;
  XMotionEvent xmev;

  if ((pEv->mode == NotifyNormal) &&
      (pDpyInfo->mode == X2X_DISCONNECTED) && (dpy == pDpyInfo->fromDpy)) {
    DoConnect(pDpyInfo);
    if (pDpyInfo->vertical) {
      XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                   pEv->x_root, pDpyInfo->fromConnCoord);
      xmev.x_root = pEv->x_root;
      xmev.y_root = pDpyInfo->lastFromCoord = pDpyInfo->fromConnCoord;
    } else {
      XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
                   pDpyInfo->fromConnCoord, pEv->y_root);
      xmev.x_root = pDpyInfo->lastFromCoord = pDpyInfo->fromConnCoord;
      xmev.y_root = pEv->y_root;
    }
    xmev.same_screen = True;
    ProcessMotionNotify(NULL, pDpyInfo, &xmev);
  }  /* END if NotifyNormal... */
  return False;

} /* END ProcessEnterNotify */

static Bool ProcessButtonPress(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XButtonEvent *pEv;
{
  int state;
  PSHADOW   pShadow;
  unsigned int toButton;

  KeySym  keysym;
  KeyCode keycode;
  int     eventno;

  switch (pDpyInfo->mode) {
  case X2X_DISCONNECTED:
    pDpyInfo->mode = X2X_AWAIT_RELEASE;
    debug("awaiting button release before connecting\n");
    break;
  case X2X_CONNECTED:
    debug("Got button %d, max is %d (%d)\n", pEv->button, N_BUTTONS, nButtons);
    if ((pEv->button <= N_BUTTONS) &&
        (buttonmap[pEv->button][0] != NoSymbol))
    {
      debug("Mapped!\n");
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
      {
        debug("Button %d is mapped, sending keys: ", pEv->button);
        for (eventno = 0;
             (keysym = buttonmap[pEv->button][eventno]) != NoSymbol;
             eventno++)
        {
          if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
            XTestFakeKeyEvent(pShadow->dpy, keycode, True, 0);
            XTestFakeKeyEvent(pShadow->dpy, keycode, False, 0);
            XFlush(pShadow->dpy);
            debug(" (0x%04X)", keycode);
          }
          else
            debug(" (no code)");
        }
        debug("\n");
      }
    } else if (pEv->button <= nButtons) {
      toButton = pDpyInfo->inverseMap[pEv->button];
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        XTestFakeButtonEvent(pShadow->dpy, toButton, True, 0);
        debug("from button %d down, to button %d down\n", pEv->button,toButton);
        XFlush(pShadow->dpy);
      } /* END for */
      if (doAutoUp)
        FakeAction(pDpyInfo, FAKE_BUTTON, toButton, True);
    }
    if (doEdge) break;

    /* check if more than one button pressed */
    state = pEv->state;
    switch (pEv->button) {
    case Button1: state &= ~Button1Mask; break;
    case Button2: state &= ~Button2Mask; break;
    case Button3: state &= ~Button3Mask; break;
    case Button4: state &= ~Button4Mask; break;
    case Button5: state &= ~Button5Mask; break;
    default:
      debug("unknown button %d\n", pEv->button);
      break;
    } /* END switch button */
    if (state) { /* then more than one button pressed */
      debug("awaiting button release before disconnecting\n");
      pDpyInfo->mode = X2X_CONN_RELEASE;
    }
    break;
  } /* END switch mode */
  return False;
} /* END ProcessButtonPress */

static Bool ProcessButtonRelease(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XButtonEvent *pEv;
{
  int state;
  PSHADOW   pShadow;
  XMotionEvent xmev;
  unsigned int toButton;

  if ((pDpyInfo->mode == X2X_CONNECTED) ||
      (pDpyInfo->mode == X2X_CONN_RELEASE)) {
    if ((pEv->button <= nButtons) &&
        (buttonmap[pEv->button][0] == NoSymbol))
      // Do not process button release if it was mapped to keys
    {
      toButton = pDpyInfo->inverseMap[pEv->button];
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        XTestFakeButtonEvent(pShadow->dpy, toButton, False, 0);
        debug("from button %d up, to button %d up\n", pEv->button, toButton);
        XFlush(pShadow->dpy);
      } /* END for */
      if (doAutoUp)
        FakeAction(pDpyInfo, FAKE_BUTTON, toButton, False);
    }
  } /* END if */

  if (doEdge) return False;
  if ((pDpyInfo->mode == X2X_AWAIT_RELEASE) ||
      (pDpyInfo->mode == X2X_CONN_RELEASE)) {
    /* make sure that all buttons are released */
    state = pEv->state;
    switch (pEv->button) {
    case Button1: state &= ~Button1Mask; break;
    case Button2: state &= ~Button2Mask; break;
    case Button3: state &= ~Button3Mask; break;
    case Button4: state &= ~Button4Mask; break;
    case Button5: state &= ~Button5Mask; break;
    default:
      debug("unknown button %d\n", pEv->button);
      break;
    } /* END switch button */
    if (!state) { /* all buttons up: time to (dis)connect */
      if (pDpyInfo->mode == X2X_AWAIT_RELEASE) { /* connect */
        DoConnect(pDpyInfo);
        if (pDpyInfo->vertical) {
          xmev.x_root = pEv->x_root;
          xmev.y_root = pDpyInfo->lastFromCoord = pEv->y_root;
        } else {
          xmev.x_root = pDpyInfo->lastFromCoord = pEv->x_root;
          xmev.y_root = pEv->y_root;
        }
        xmev.same_screen = True;
        ProcessMotionNotify(NULL, pDpyInfo, &xmev);
      } else { /* disconnect */
        DoDisconnect(pDpyInfo);
      } /* END if mode */
    } /* END if !state */
  } /* END if mode */
  return False;

} /* END ProcessButtonRelease */

static Bool ProcessKeyEvent(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XKeyEvent *pEv;
{
  KeyCode   keycode;
  KeySym    keysym;
  PSHADOW   pShadow;
  Bool      bPress;
  PSTICKY   pSticky;
  Bool      DoFakeShift = False;
  KeyCode   toShiftCode;

  keysym = XKeycodeToKeysym(pDpyInfo->fromDpy, pEv->keycode, 0);
  bPress = (pEv->type == KeyPress);

  /* If CapsLock is on, we need to do some funny business to make sure the */
  /* "to" display does the right thing */
  if(doCapsLkHack && (pEv->state & 0x2))
  {
    /* Throw away any explicit shift events (they're faked as neccessary) */
    if((keysym == XK_Shift_L) || (keysym == XK_Shift_R)) return False;

    /* If the shift key is pressed, do the shift, unless the keysym */
    /* is an alpha key, in which case we invert the shift logic */
    DoFakeShift = (pEv->state & 0x1);
    if(((keysym >= XK_A) && (keysym <= XK_Z)) ||
       ((keysym >= XK_a) && (keysym <= XK_z)))
      DoFakeShift = !DoFakeShift;
  }

  for (pSticky = stickies; pSticky; pSticky = pSticky->pNext)
    if (keysym == pSticky->keysym)
      break;

  if (pSticky) {
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
      toShiftCode = XKeysymToKeycode(pShadow->dpy, XK_Shift_L);
      if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
        if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, True, 0);
        XTestFakeKeyEvent(pShadow->dpy, keycode, True, 0);
        XTestFakeKeyEvent(pShadow->dpy, keycode, False, 0);
        if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, False, 0);
        XFlush(pShadow->dpy);
      } /* END if */
    } /* END for */
  } else {
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
      toShiftCode = XKeysymToKeycode(pShadow->dpy, XK_Shift_L);
      if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
        if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, True, 0);
        XTestFakeKeyEvent(pShadow->dpy, keycode, bPress, 0);
        if(DoFakeShift) XTestFakeKeyEvent(pShadow->dpy, toShiftCode, False, 0);
        XFlush(pShadow->dpy);
      } /* END if */
    } /* END for */
    if (doAutoUp)
      FakeAction(pDpyInfo, FAKE_KEY, keysym, bPress);
  }

  return False;

} /* END ProcessKeyEvent */

static Bool ProcessConfigureNotify(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XConfigureEvent *pEv;
{
  if (pDpyInfo->fid) {
    /* reposition text */
    pDpyInfo->width = pEv->width;
    pDpyInfo->height = pEv->height;
  } /* END if font */
  return False;

} /* END ProcessConfigureNotify */

static Bool ProcessClientMessage(dpy, pDpyInfo, pEv)
Display  *dpy;
PDPYINFO pDpyInfo;
XClientMessageEvent *pEv;
{
  /* terminate if atoms match! */
  return ((pEv->message_type == pDpyInfo->wmpAtom) &&
          (pEv->data.l[0]    == pDpyInfo->wmdwAtom));

} /* END ProcessClientMessage */

static Bool ProcessSelectionRequest(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionRequestEvent *pEv;
{
  PDPYXTRA pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);
  Display *otherDpy;
  Atom utf8string;

  if (dpy == pDpyInfo->fromDpy) {
    utf8string = pDpyInfo->fromDpyUtf8String;
  } else {
    utf8string = pDpyInfo->toDpyUtf8String;
  }

    debug("selection request\n");

  /* bribe me to support more general selection requests,
     or send me the code to do it. */
  if ((pDpyXtra->sState != SELSTATE_ON) ||
      (pEv->selection != XA_PRIMARY) ||
      (pEv->target > XA_LAST_PREDEFINED && pEv->target != utf8string)) { /* bad request, punt request */
    pEv->property = None;
    SendSelectionNotify(pEv); /* blam! */
  } else {
    otherDpy = pDpyXtra->otherDpy;
    SendPing(otherDpy, GETDPYXTRA(otherDpy, pDpyInfo)); /* get started */
    if (pDpyInfo->sDpy) {
      /* nuke the old one */
      pDpyInfo->sEv.property = None;
      SendSelectionNotify(&(pDpyInfo->sEv)); /* blam! */
    } /* END if InProg */
    pDpyInfo->sDpy  = otherDpy;
    pDpyInfo->sEv = *pEv;
  } /* END if relaySel */
  return False;

} /* END ProcessSelectionRequest */

static void SendPing(dpy, pDpyXtra)
Display *dpy;
PDPYXTRA pDpyXtra;
{
  if (!(pDpyXtra->pingInProg)) {
    XChangeProperty(dpy, pDpyXtra->propWin, pDpyXtra->pingAtom, XA_PRIMARY,
                    8, PropModeAppend, NULL, 0);
    pDpyXtra->pingInProg = True;
  } /* END if */
} /* END SendPing */

static Bool ProcessPropertyNotify(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XPropertyEvent *pEv;
{
  PDPYXTRA pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);

  debug("property notify\n");

  if (pEv->atom == pDpyXtra->pingAtom) { /* acking a ping */
    pDpyXtra->pingInProg = False;
    if (pDpyXtra->sState == SELSTATE_WAIT) {
      pDpyXtra->sState = SELSTATE_ON;
      XSetSelectionOwner(dpy, XA_PRIMARY, pDpyXtra->propWin, pEv->time);
    } else if (dpy == pDpyInfo->sDpy) {
      if (pDpyInfo->sTime == pEv->time) {
        /* oops, need to ensure uniqueness */
        SendPing(dpy, pDpyXtra); /* try for another time stamp */
      } else {
        pDpyInfo->sTime = pEv->time;
        XConvertSelection(dpy, pDpyInfo->sEv.selection, pDpyInfo->sEv.target,
                          XA_PRIMARY, pDpyXtra->propWin, pEv->time);
      } /* END if ... ensure uniqueness */
    } /* END if sState... */
  } /* END if ping */
  return False;

} /* END ProcessPropertyNotify */

static Bool ProcessSelectionNotify(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionEvent *pEv;
{
  Atom type;
  int  format;
  unsigned long nitems, after;
  unsigned char *prop;
  Bool success;
  XSelectionRequestEvent *pSelReq;
  Atom utf8string;

#define DEFAULT_PROP_SIZE 1024L

  debug("selection notify\n");

  if (dpy == pDpyInfo->fromDpy) {
    utf8string = pDpyInfo->fromDpyUtf8String;
  } else {
    utf8string = pDpyInfo->toDpyUtf8String;
  }

  if ((dpy == pDpyInfo->sDpy) && (pDpyInfo->sTime == pEv->time)) {
    success = False;
    /* corresponding select */
    if (XGetWindowProperty(dpy, pEv->requestor, XA_PRIMARY, 0L,
                           DEFAULT_PROP_SIZE, True, AnyPropertyType,
                           &type, &format, &nitems, &after, &prop)
        == Success) { /* got property */
      if ((type != None)
          && (format != None)
          && (nitems != 0)
          && (prop != None)
          /* known type */
          && (type <= XA_LAST_PREDEFINED || type == utf8string)) {
        if (after == 0L) { /* got everything */
          success = True;
        } else { /* try to get everything */
          XFree(prop);
          success =
            ((XGetWindowProperty(dpy, pEv->requestor, XA_PRIMARY, 0L,
                                 DEFAULT_PROP_SIZE + after + 1,
                                 True, AnyPropertyType,
                                 &type, &format, &nitems, &after, &prop)
              == Success) &&
             (type != None) && (format != None) && (nitems != 0) &&
             (after == 0L) && (prop != None));
        } /* END if got everything ... else ...*/
      } /* END if known type */
    } /* END if got property */

    pSelReq = &(pDpyInfo->sEv);
    if (success) { /* send bits to the requesting dpy/window */
      XChangeProperty(pSelReq->display, pSelReq->requestor,
                      pSelReq->property, type, format, PropModeReplace,
                      prop, nitems);
      XFree(prop);
      SendSelectionNotify(pSelReq);
    } else {
      pSelReq->property = None;
      SendSelectionNotify(pSelReq);
    } /* END if success */
    pDpyInfo->sDpy = NULL;
  } /* END if corresponding select */
  return False;

} /* END ProcessSelectionNotify */

static void SendSelectionNotify(pSelReq)
XSelectionRequestEvent *pSelReq;
{
  XSelectionEvent sendEv;

  sendEv.type      = SelectionNotify;
  sendEv.display   = pSelReq->display;
  sendEv.requestor = pSelReq->requestor;
  sendEv.selection = pSelReq->selection;
  sendEv.target    = pSelReq->target;
  sendEv.property  = pSelReq->property;
  sendEv.time      = pSelReq->time;
  XSendEvent(pSelReq->display, pSelReq->requestor, False, 0,
             (XEvent *)&sendEv);

} /* END SendSelectionNotify */

static Bool ProcessSelectionClear(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionClearEvent *pEv;
{
  Display  *otherDpy;
  PDPYXTRA pDpyXtra, pOtherXtra;

  debug("selection clear\n");

  if (pEv->selection == XA_PRIMARY) {
    /* track primary selection */
    pDpyXtra = GETDPYXTRA(dpy, pDpyInfo);
    pDpyXtra->sState = SELSTATE_OFF;
    otherDpy = pDpyXtra->otherDpy;
    pOtherXtra = GETDPYXTRA(otherDpy, pDpyInfo);
    pOtherXtra->sState = SELSTATE_WAIT;
    SendPing(otherDpy, pOtherXtra);
    if (pDpyInfo->sDpy) { /* nuke the selection in progress */
      pDpyInfo->sEv.property = None;
      SendSelectionNotify(&(pDpyInfo->sEv)); /* blam! */
      pDpyInfo->sDpy = NULL;
    } /* END if nuke */
  } /* END if primary */
  return False;

} /* END ProcessSelectionClear */

/**********
 * process a visibility event
 **********/
static Bool ProcessVisibility(dpy, pDpyInfo, pEv)
Display          *dpy;
PDPYINFO         pDpyInfo;
XVisibilityEvent *pEv;
{
  /* might want to qualify, based on other messages.  otherwise,
     this code might cause a loop if two windows decide to fight
     it out for the top of the stack */
  if (pEv->state != VisibilityUnobscured)
    XRaiseWindow(dpy, pEv->window);

  return False;

} /* END ProcessVisibility */

/**********
 * process a keyboard mapping event
 **********/
static Bool ProcessMapping(dpy, pDpyInfo, pEv)
Display             *dpy;
PDPYINFO            pDpyInfo;
XMappingEvent       *pEv;
{
  debug("mapping\n");

  switch (pEv->request) {
  case MappingModifier:
  case MappingKeyboard:
    XRefreshKeyboardMapping(pEv);
    break;
  case MappingPointer:
    RefreshPointerMapping(dpy, pDpyInfo);
    break;
  } /* END switch */

  return False;

} /* END ProcessMapping */

static void FakeAction(pDpyInfo, type, thing, bDown)
PDPYINFO pDpyInfo;
unsigned int thing;
Bool bDown;
{
  PFAKE *ppFake;
  PFAKE pFake;

  /* find the associated button, or the last record, whichever comes first */
  for (ppFake = &(pDpyInfo->pFakeThings);
       (*ppFake &&
        (((*ppFake)->type != type) || ((*ppFake)->thing != thing)));
       ppFake = &((*ppFake)->pNext));

  if (bDown) { /* key down */
    if (*ppFake == NULL) { /* need a new record */
      pFake = (PFAKE)malloc(sizeof(FAKE));
      pFake->pNext = NULL; /* always at the end of the list */
      pFake->type = type;
      pFake->thing = thing;
      *ppFake = pFake;
    } /* END if */
  } else { /* key up */
    if (*ppFake != NULL) { /* get rid of the record */
      /* splice out of the list */
      pFake = *ppFake;
      *ppFake = pFake->pNext;
      free(pFake); /* blam! */
    } /* END if */
  } /* END if */

} /* END FakeAction */

static void FakeThingsUp(pDpyInfo)
PDPYINFO pDpyInfo;
{
  PFAKE pFake, pNext;
  PSHADOW pShadow;
  unsigned int type;
  KeyCode keycode;

  if (pDpyInfo->pFakeThings) { /* everything goes up! */
    for (pFake = pDpyInfo->pFakeThings; pFake; pFake = pNext) {
      type = pFake->type;
      /* send up to all shadows */
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        if (type == FAKE_KEY) { /* key goes up */
          if ((keycode = XKeysymToKeycode(pShadow->dpy, pFake->thing))) {
            XTestFakeKeyEvent(pShadow->dpy, keycode, False, 0);
            debug("key 0x%x up\n", pFake->thing);
          } /* END if */
        } else { /* button goes up */
          XTestFakeButtonEvent(pShadow->dpy, pFake->thing, False, 0);
          debug("button %d up\n", pFake->thing);
        } /* END if/else */
      } /* END for */

      /* flush everything at once */
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext)
        XFlush(pShadow->dpy);

      /* get next and free current */
      pNext = pFake->pNext;
      free(pFake);
    } /* END for */

    pDpyInfo->pFakeThings = NULL;
  } /* END if */

} /* END FakeThingsUp */

static void RefreshPointerMapping(dpy, pDpyInfo)
Display             *dpy;
PDPYINFO            pDpyInfo;
{
  unsigned int buttCtr;
  unsigned char buttonMap[N_BUTTONS];

  if (dpy == pDpyInfo->toDpy) { /* only care about toDpy */
    /* straightforward mapping */
    for (buttCtr = 1; buttCtr <= N_BUTTONS; ++buttCtr) {
      pDpyInfo->inverseMap[buttCtr] = buttCtr;
    } /* END for */

    nButtons = MIN(N_BUTTONS, XGetPointerMapping(dpy, buttonMap, N_BUTTONS));
        debug("got button mapping: %d items\n", nButtons);
#ifdef WIN_2_X
    pDpyInfo->nXbuttons = nButtons;
#endif
    if (doPointerMap) {
      for (buttCtr = 0; buttCtr < nButtons; ++buttCtr) {
        debug("button %d -> %d\n", buttCtr + 1, buttonMap[buttCtr]);
        if (buttonMap[buttCtr] <= N_BUTTONS)
          pDpyInfo->inverseMap[buttonMap[buttCtr]] = buttCtr + 1;
      } /* END for */
    } /* END if */
  } /* END if toDpy */

} /* END RefreshPointerMapping */

#ifdef WIN_2_X

/* These are the Windows specific routines */

/* Want the Edge window and not the Big one */
void MoveWindowToEdge(PDPYINFO pDpyInfo) {
  if (pDpyInfo->onedge) return;
  debug("MoveWindowToEdge\n");

  SetWindowPos(pDpyInfo->bigwindow, HWND_BOTTOM,
               pDpyInfo->screenRect.left, pDpyInfo->screenRect.top,
               pDpyInfo->screenWidth, pDpyInfo->screenHeight,
               SWP_HIDEWINDOW /* | SWP_NOREDRAW */);

  SetWindowPos(pDpyInfo->edgewindow, HWND_TOPMOST,
               (doEdge == EDGE_EAST) ? pDpyInfo->monitorRect.right -1: pDpyInfo->monitorRect.left,
         (doEdge == EDGE_SOUTH) ? pDpyInfo->monitorRect.bottom - 1 : pDpyInfo->monitorRect.top,
         (pDpyInfo->vertical) ? pDpyInfo->monitorRect.right - pDpyInfo->monitorRect.left : 1,
         (pDpyInfo->vertical) ? 1 : pDpyInfo->monitorRect.bottom - pDpyInfo->monitorRect.top,
               SWP_SHOWWINDOW | SWP_NOREDRAW);
  pDpyInfo->onedge=1;

  SetForegroundWindow(hWndSave);
}

int MoveWindowToScreen(PDPYINFO pDpyInfo)
{
  int notfg;
  LPINPUT pInputs;

  if(!pDpyInfo->onedge) return 1;
  debug("MoveWindowToScreen\n");

  hWndSave = GetForegroundWindow();

  if ((notfg = (SetForegroundWindow(pDpyInfo->bigwindow) == 0))) {
    debug("Did not become foreground\n");

    /* This code thanks to Thomas Chadwick */
    /* Fakes that the user clicked the mouse to move the focus */
    /* This is to deal with the XP and 2000 behaviour that attempts to */
    /* prevent an application from stealing the focus */
    debug("Using SendInput to synthesize a mouse click\n");

    pInputs = (LPINPUT) malloc (2*sizeof(INPUT));

    pInputs[0].type           = INPUT_MOUSE;
    pInputs[0].mi.dx          = 0;
    pInputs[0].mi.dy          = 0;
    pInputs[0].mi.mouseData   = 0;
    pInputs[0].mi.dwFlags     = MOUSEEVENTF_LEFTDOWN;
    pInputs[0].mi.time        = 0;
    pInputs[0].mi.dwExtraInfo = 0;

    pInputs[1].type           = INPUT_MOUSE;
    pInputs[1].mi.dx          = 0;
    pInputs[1].mi.dy          = 0;
    pInputs[1].mi.mouseData   = 0;
    pInputs[1].mi.dwFlags     = MOUSEEVENTF_LEFTUP;
    pInputs[1].mi.time        = 0;
    pInputs[1].mi.dwExtraInfo = 0;

    if (SendInput(2, pInputs, sizeof(INPUT)) == 0) {
      debug("SendInput failed\n");
      free(pInputs);
      return 0;
    }
    free(pInputs);
    return 1;
  }

  SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
               pDpyInfo->screenRect.left, pDpyInfo->screenRect.top,
               pDpyInfo->screenWidth, pDpyInfo->screenHeight,
               SWP_SHOWWINDOW | SWP_NOREDRAW);

  pDpyInfo->onedge=0;
  return 1;
}

static void DoWinConnect(pDpyInfo, x, y)
PDPYINFO pDpyInfo;
int x,y;
{
  PSHADOW   pShadow;

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
    DPMSForceLevel(pShadow->dpy, DPMSModeOn);
    XFlush(pShadow->dpy);
  }

  debug("connecting (Win2x)\n");
  pDpyInfo->mode = X2X_CONNECTED;

  if (!pDpyInfo->onedge) return;

  if (MoveWindowToScreen(pDpyInfo)) {
    debug("Warp Cursor: %d,%d -> %d, %d\n",
           x,y,
           (doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3,
           y);

    if (pDpyInfo->vertical)
    {
      pDpyInfo->lastFromCoord = (doEdge == EDGE_SOUTH) ? 0 : pDpyInfo->fromHeight - 2;
      pDpyInfo->lastFromX = x - pDpyInfo->monitorRect.left - logicalOffset;
      pDpyInfo->lastFromY = pDpyInfo->lastFromCoord;
    }
    else
    {

      pDpyInfo->lastFromCoord = (doEdge == EDGE_EAST) ? 0 : pDpyInfo->fromWidth - 2;
      pDpyInfo->lastFromX = pDpyInfo->lastFromCoord;
      pDpyInfo->lastFromY = y - pDpyInfo->monitorRect.top - logicalOffset;
    }

    SetCursorPos(pDpyInfo->lastFromX, pDpyInfo->lastFromY);
  }
}

static void DoWinDisconnect(pDpyInfo, x, y)
PDPYINFO pDpyInfo;
int x,y;
{

  debug("disconnecting\n");
  pDpyInfo->mode = X2X_DISCONNECTED;

  /* If we own the X selection, then windows has it! */
  /* otherwise transfer the info by asking the owning X window to */
  /* tell us (via ProcessSelectNotifyW) */
  if (!pDpyInfo->owntoXsel) {
    debug("Ask X for -to selection\n");
    XConvertSelection(pDpyInfo->toDpy, XA_PRIMARY, XA_STRING,
                      XA_PRIMARY, pDpyInfo->toDpyXtra.propWin, CurrentTime);
    XFlush(pDpyInfo->toDpy);
    pDpyInfo->expectSelNotify = 1;
  }

  if (x >= 0) {
    debug("Warp Cursor: %d,%d -> %d, %d\n",
           x,y,
           (doEdge == EDGE_EAST) ? pDpyInfo->fromWidth - 2 : 2,
           y);
    if (pDpyInfo->vertical)
    {
      SetCursorPos(x + pDpyInfo->monitorRect.left + logicalOffset, (doEdge == EDGE_SOUTH) ? pDpyInfo->monitorRect.bottom - 2 : pDpyInfo->monitorRect.top);
    }
    else
    {
      SetCursorPos((doEdge == EDGE_EAST) ? pDpyInfo->monitorRect.right - 2 : pDpyInfo->monitorRect.left, y + pDpyInfo->monitorRect.top + logicalOffset);
    }
  }

  MoveWindowToEdge(pDpyInfo);
  /* force normal state on to display: */
  if (doAutoUp)
    FakeThingsUp(pDpyInfo);
}

/* Windows Event Management */
LRESULT CALLBACK
WinProcessMessage (HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
  PDPYINFO pDpyInfo = (PDPYINFO) GetWindowLong(hwnd, GWL_USERDATA);

#ifdef DEBUGCHATTY
  if (iMsg != WM_PAINT) printf("Got msg %d (0x%04x) %s %s\n", iMsg, iMsg,
                               msgtotext(iMsg),
                               (hwnd == NULL) ? "null" :
                               (pDpyInfo == NULL) ? "null dpy" :
                               (hwnd == pDpyInfo->bigwindow) ? "big" :
                               ((hwnd == pDpyInfo->edgewindow) ? "edge" : "XX"));
#endif
  switch (iMsg)
  {
    case WM_CREATE:
      return 0;

    case WM_TIMER:
      debug("#");
#if 0
      if (wParam == m_emulate3ButtonsTimer)
      {
        SubProcessPointerEvent(m_emulateButtonPressedX,
                               m_emulateButtonPressedY,
                               m_emulateKeyFlags);
        KillTimer(m_hwnd, m_emulate3ButtonsTimer);
        m_waitingOnEmulateTimer = false;
      }
#endif
      return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    {

      POINT pt;
      int x,y;

      if (GetFocus() != hwnd) {
        if (pDpyInfo == NULL) {
          debug("No focus and pDpyInfo NULL\n");
          return 0;
        }
        if (pDpyInfo->onedge) {
          debug("No focus and currently on edge\n");
          return 0;
        }
        if (hwnd == pDpyInfo->bigwindow) {
          /* Ok, event for the bigwindow, but no focus -> take it */
          debug("No focus on bigwindow mouse event, grab it\n");
          SetForegroundWindow(pDpyInfo->bigwindow);
          SetFocus(pDpyInfo->bigwindow);
        }
        else
          debug("No focus, not on edge, not bigwindow\n");
      }

      pt.x = GET_X_LPARAM(lParam);
      pt.y = GET_Y_LPARAM(lParam);

      ClientToScreen(hwnd, &pt);
      x = pt.x;
      y = pt.y;

      if(x<-32768 || x>32768) x=0;
      if(y<-32768 || y>32768) y=0;

      if(x>=pDpyInfo->fromWidth)
        x = pDpyInfo->fromWidth - 1;
      if(y>=pDpyInfo->fromHeight)
        y = pDpyInfo->fromHeight - 1;
      if(y<0)
        y = 0;
      if(x<0)
        x = 0;

      if (pt.x != x || pt.y != y)
      {
        SetCursorPos(pDpyInfo->lastFromX, pDpyInfo->lastFromY);
        return;
      }


      if(pDpyInfo->onedge)
      {
        if(hwnd == pDpyInfo->edgewindow)
        {
          debug("onedge mouse connect\n");
          DoWinConnect(pDpyInfo, x,y);
        }
        else {
          debug("onedge mouse move to non edge window ");
          DoWinConnect(pDpyInfo, x, y);
        }
        return 0;
      }

      if(hwnd == pDpyInfo->bigwindow)
      {
        WinPointerEvent(pDpyInfo, x,y, wParam, iMsg);

      } /* END == bigwindow */
      if(hwnd == pDpyInfo->edgewindow)
      {
        int delta;
#ifdef DEBUGMOUSE
        printf("e(%d,%d) ", x, y);
#endif
        delta = pDpyInfo->lastFromCoord - ((pDpyInfo->vertical) ? y : x);
        if (delta < 0) delta = -delta;
        if (delta > pDpyInfo->unreasonableDelta) {
          /* Guess that the warp failed and try it again... */
          debug("Retry warp to (%d, %d)\n",
                (doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3,
                 y);
          if (pDpyInfo->vertical)
          {
            SetCursorPos(x, (doEdge == EDGE_SOUTH) ? 1 : pDpyInfo->fromHeight - 3);
          }
          else
          {
            SetCursorPos((doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3, y);
          }
        }
      } /* END == edgewindow */

      return 0;
    }

    case WM_MOUSEWHEEL:
    {
      /* Partial deltas can be sent, but should only take notice */
      /* in units of WHEEL_DELTA */
      int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
      int wdelta;

      wdelta = pDpyInfo->wdelta + zDelta;

      debug("Wheel moved to new delta %d (%s window)\n", wdelta,
             (hwnd == pDpyInfo->edgewindow) ? "edge" :
             (hwnd == pDpyInfo->bigwindow) ? "big" : "other");

      while (wdelta >= WHEEL_DELTA) {
        /* Scroll up by sending Button 4 */
        SendButtonClick(pDpyInfo, 4);
        wdelta -= WHEEL_DELTA;
      }

      while (wdelta <= -WHEEL_DELTA) {
        /* Scroll down by sending Button 5 */
        SendButtonClick(pDpyInfo, 5);
        wdelta += WHEEL_DELTA;
      }
      pDpyInfo->wdelta = wdelta;

      return 0;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    {
#if 0
      if (pDpyInfo->winSSave) {
        debug("Attempt to restore from screen saver (key)!\n");
        if (!pDpyInfo->onedge) {
          SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
                       pDpyInfo->screenRect.left, pDpyInfo->screenRect.top,
                       pDpyInfo->screenWidth, pDpyInfo->screenHeight,
                       SWP_HIDEWINDOW);
          SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
                       pDpyInfo->screenRect.left, pDpyInfo->screenRect.top,
                       pDpyInfo->screenWidth, pDpyInfo->screenHeight,
                       SWP_SHOWWINDOW  | SWP_NOREDRAW);
        }
        pDpyInfo->winSSave = 0;
      }
#endif /* 0 */
      WinKeyEvent(pDpyInfo, (int) wParam, (DWORD) lParam);
      return 0;
    }

    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      return 0;

      // Cacnel modifiers when we lose focus
    case WM_KILLFOCUS:
    {
#if 0
      /* XXX mdh - need to think about this */
      if (!m_running) return 0;
      log.Print(6, _T("Losing focus - cancelling modifiers\n"));
      SendKeyEvent(XK_Alt_L,     false);
      SendKeyEvent(XK_Control_L, false);
      SendKeyEvent(XK_Shift_L,   false);
      SendKeyEvent(XK_Alt_R,     false);
      SendKeyEvent(XK_Control_R, false);
      SendKeyEvent(XK_Shift_R,   false);
#endif /* 0 */
      return 0;
    }

    case WM_CLOSE:
    {
      DestroyWindow(hwnd);
      return 0;
    }

    case WM_DESTROY:
    {
      // Remove us from the clipboard viewer chain
      if (doSel) {
        /*int res =*/ ChangeClipboardChain(pDpyInfo->edgewindow,
                                       pDpyInfo->hwndNextViewer);
      }

      SetWindowLong(pDpyInfo->bigwindow, GWL_USERDATA, (LONG) 0);
      SetWindowLong(pDpyInfo->edgewindow, GWL_USERDATA, (LONG) 0);

      if(hwnd == pDpyInfo->bigwindow) pDpyInfo->bigwindow = 0;
      if(hwnd == pDpyInfo->edgewindow) pDpyInfo->edgewindow = 0;
      /* XXX mdh - is a PostQuitMessage(0) needed here? */
      return 0;
    }

    case WM_SETCURSOR:
    {
      POINT pt;
      if(doEdge && (hwnd == pDpyInfo->edgewindow))
      {
        GetCursorPos(&pt);
        debug("Activated by action: WM_SETCURSOR\n");
        DoWinConnect(pDpyInfo, pt.x, pt.y);
      }
      return DefWindowProc(hwnd, iMsg, wParam, lParam);
    }

    case WM_DRAWCLIPBOARD:
      if (doSel) {
        // The clipboard contents changed
        /* For now can only process text */
        if (IsClipboardFormatAvailable(CF_TEXT) &&
            OpenClipboard(pDpyInfo->edgewindow)) {
          HGLOBAL hglb = GetClipboardData(CF_TEXT);
          LPTSTR lptstr = GlobalLock(hglb);
          int len;
          len = strlen(lptstr);
          /* If the length is bad just ignore */
          if (len > 0) {
            if (pDpyInfo->winSelText != NULL) free(pDpyInfo->winSelText);
            pDpyInfo->winSelText = malloc(len + 10);
            if (pDpyInfo->winSelText != NULL)
              strcpy(pDpyInfo->winSelText, lptstr);
            /* XXX mdh - hope this does (null) as expected! */
            debug("Windows clipboard changed to %s\n", pDpyInfo->winSelText);
          }
          GlobalUnlock(hglb);
          CloseClipboard();
          /* Prevent grabbing X selection in response to us copying  */
          /* it to windows (mostly cosmetic in that the real source  */
          /* X app will unhighlight or whatever on loosing selection)*/
          /* XXX mdh - the strlen+1 isn't guarenteed to be unique */
          /* but I think it is good enough, since if it goes wrong */
          /* the X app looses its selection but the actual text is preserved */
          if (pDpyInfo->expectOwnClip != 0) {
            if (pDpyInfo->expectOwnClip == (len+1)) {
              /* Its ours stop looking */
              pDpyInfo->expectOwnClip = 0;
              debug("Saw own addition to clipboard\n");
            } else {
                debug("Oops. expectOwrClip %d with len %d\n",
                       pDpyInfo->expectOwnClip, len);
            }
          } else {
            /* This can race during creation */
            /* but we will claim selection in the init routine */
            if (pDpyInfo->toDpyXtra.propWin != 0) {
              debug("Selection Owner to %x\n",
                     (unsigned int)pDpyInfo->toDpyXtra.propWin);
              XSetSelectionOwner(pDpyInfo->toDpy, XA_PRIMARY,
                                 pDpyInfo->toDpyXtra.propWin, CurrentTime);
              pDpyInfo->owntoXsel = 1;
            }
          }
        }
        // We have to tell the next clipbard viewer
        SendMessage(pDpyInfo->hwndNextViewer, iMsg, wParam, lParam);
      } else {
        printf("Unxepected WM_DRAWCLIPBOARD when not doing selection\n");
      }
      // ProcessLocalClipboardChange();

      return 0;

    case WM_CHANGECBCHAIN:
    {
      if (doSel) {
        // The clipboard chain is changing
        HWND hWndRemove = (HWND) wParam;     // handle of window being removed
        HWND hWndNext = (HWND) lParam;       // handle of next window in chain
        // If next window is closing, update our pointer.
        if (hWndRemove == pDpyInfo->hwndNextViewer)
          pDpyInfo->hwndNextViewer = hWndNext;
        // Otherwise, pass the message to the next link.
        else if (pDpyInfo->hwndNextViewer != NULL)
          SendMessage(pDpyInfo->hwndNextViewer, WM_CHANGECBCHAIN,
                      (WPARAM) hWndRemove,  (LPARAM) hWndNext );
      } else {
        printf("Unxepected WM_CHANGEKBCHAIN when not doing selection\n");
      }
      return 0;
    }

    case WM_SYSCOMMAND:
      debug("WM_SYSCOMMAND with wParam %d (0x%x)\n", wParam, wParam);
      if (wParam == SC_SCREENSAVE) {
        PSHADOW   pShadow;
        pDpyInfo->winSSave = 1;
        for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
          XActivateScreenSaver(pShadow->dpy);
          XFlush(pShadow->dpy);
        } /* END for shadow */
      }
      /* Fall through */
    case WM_SYNCPAINT:
    case WM_PAINT:
      return DefWindowProc(hwnd, iMsg, wParam, lParam);

    case WM_NCPAINT:
      return 0;

    case WM_SETFOCUS:
    case WM_ACTIVATE:
    case WM_NCHITTEST:
    case WM_NCACTIVATE:
    case WM_WINDOWPOSCHANGED:
    case WM_WINDOWPOSCHANGING:
    case WM_SIZE:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_GETMINMAXINFO:
    case WM_QUERYNEWPALETTE:
    case WM_PALETTECHANGED:
      return 1;

      /* keep transparent (I hope) */
    case WM_ERASEBKGND:
      return 1;

    case X2X_MSG_HOTKEY:
      if (pDpyInfo->mode == X2X_CONNECTED)
      {
        DoWinDisconnect(pDpyInfo, 30, 30);
      }
      else
      {
        DoWinConnect(pDpyInfo, 30, 30);
      }

      return 1;
  }

  debug("Unused message: %d (0x%04x)\n", iMsg, iMsg);
  return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

void WinPointerEvent(PDPYINFO pDpyInfo,
                     int x, int y, DWORD keyflags, UINT msg)
{
  int down = 0;
  unsigned int button, toButton;
  int       toScreenNum;
  PSHADOW   pShadow;
  int       toCoord, fromCoord, fromX, fromY, delta;
  short **coordTables;

  button = 0;
  switch (msg) {
  case WM_MOUSEMOVE:

    /* seems that we get repeats, ignore them */
    if ((x == pDpyInfo->lastFromX) && (y == pDpyInfo->lastFromY)) {
      return;
    }

    /* find the screen */
    toScreenNum = pDpyInfo->toScreen;
    fromX = x;
    fromY = y;

    fromCoord = (pDpyInfo->vertical) ? fromY : fromX;

    coordTables = (pDpyInfo->vertical) ? pDpyInfo->yTables : pDpyInfo->xTables;

    toCoord = coordTables[toScreenNum][fromCoord];

    /* sanity check motion: necessary for nondeterminism surrounding warps */
    delta = pDpyInfo->lastFromCoord - fromCoord;
    if (delta < 0) delta = -delta;
    if (delta > pDpyInfo->unreasonableDelta) {
      if (pDpyInfo->unreasonableCount++ < MAX_UNREASONABLES) {
        return;
      }
      pDpyInfo->unreasonableCount = 0;
    }

    if (SPECIAL_COORD(toCoord) != 0) { /* special coordinate */
      if (toCoord == COORD_INCR) {
        if (toScreenNum != (pDpyInfo->nScreens - 1)) { /* next screen */
          toScreenNum = ++(pDpyInfo->toScreen);
          fromCoord = pDpyInfo->fromIncrCoord;
          toCoord = coordTables[toScreenNum][fromCoord];
        } else { /* disconnect! */
          if (doBtnBlock &&
            (keyflags & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))) {
          } else {
            DoWinDisconnect(pDpyInfo, x, y);
            fromCoord = pDpyInfo->fromDiscCoord;
          }
          toCoord = coordTables[toScreenNum][pDpyInfo->fromConnCoord];
        }
      } else { /* DECR */
        if (toScreenNum != 0) { /* previous screen */
          toScreenNum = --(pDpyInfo->toScreen);
          fromCoord = pDpyInfo->fromDecrCoord;
          toCoord = coordTables[toScreenNum][fromCoord];
        } else { /* disconnect! */
          if (doBtnBlock &&
            (keyflags & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))) {
          } else {
            DoWinDisconnect(pDpyInfo, x, y);
            fromCoord = pDpyInfo->fromDiscCoord;
          }
          toCoord = coordTables[toScreenNum][pDpyInfo->fromConnCoord];
        }
      } /* END if toX */
    } /* END if SPECIAL_COORD */
    pDpyInfo->lastFromCoord = fromCoord;
    pDpyInfo->lastFromX = fromX;
    pDpyInfo->lastFromY = fromY;

    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
      if (doDpmsMouse)
      {
        DPMSForceLevel(pShadow->dpy, DPMSModeOn);
      }
      XTestFakeMotionEvent(pShadow->dpy, toScreenNum, pDpyInfo->xTables[toScreenNum][x],
        pDpyInfo->yTables[toScreenNum][y], 0);
      XFlush(pShadow->dpy);
    } /* END for */
    return;

  case WM_LBUTTONDOWN: down++;
  case WM_LBUTTONUP: button = Button1;
    break;
  case WM_MBUTTONDOWN: down++;
  case WM_MBUTTONUP: button = Button2;
    break;
  case WM_RBUTTONDOWN: down++;
  case WM_RBUTTONUP: button = Button3;
    break;
  }
  /* down and button tell us what to do */
  if (button <= N_BUTTONS) {
    toButton = pDpyInfo->inverseMap[button];
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
      XTestFakeButtonEvent(pShadow->dpy, toButton, down, 0);
      debug("from button %d %s, to button %d %s\n",
            button, down ? "down":"up", toButton, down ? "down":"up");
      XFlush(pShadow->dpy);
    } /* END for */
    if (doAutoUp)
      FakeAction(pDpyInfo, FAKE_BUTTON, toButton, down);
  }
}

void SendButtonClick(pDpyInfo, button)
     PDPYINFO pDpyInfo;
     int button;
{
  int toButton;
  int       toScreenNum;
  PSHADOW   pShadow;

  toScreenNum = pDpyInfo->toScreen;

  if (button <= N_BUTTONS) {
    toButton = pDpyInfo->inverseMap[button];
    if (toButton <= pDpyInfo->nXbuttons)
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
        XTestFakeButtonEvent(pShadow->dpy, toButton, True, 0);
        XTestFakeButtonEvent(pShadow->dpy, toButton, False, 0);
        debug("Click from button %d, to button %d\n",
               button, toButton);
        XFlush(pShadow->dpy);
      } /* END for */
    else
      debug("to only has %d buttons, cant clikc from %d -> to %d\n",
             pDpyInfo->nXbuttons, button, toButton);
  }
}

//
// ProcessKeyEvent
//
// Normally a single Windows key event will map onto a single RFB
// key message, but this is not always the case.  Much of the stuff
// here is to handle AltGr (=Ctrl-Alt) on international keyboards.
// Example cases:
//
//    We want Ctrl-F to be sent as:
//      Ctrl-Down, F-Down, F-Up, Ctrl-Up.
//    because there is no keysym for ctrl-f, and because the ctrl
//    will already have been sent by the time we get the F.
//
//    On German keyboards, @ is produced using AltGr-Q, which is
//    Ctrl-Alt-Q.  But @ is a valid keysym in its own right, and when
//    a German user types this combination, he doesn't mean Ctrl-@.
//    So for this we will send, in total:
//
//      Ctrl-Down, Alt-Down,
//                 (when we get the AltGr pressed)
//
//      Alt-Up, Ctrl-Up, @-Down, Ctrl-Down, Alt-Down
//                 (when we discover that this is @ being pressed)
//
//      Alt-Up, Ctrl-Up, @-Up, Ctrl-Down, Alt-Down
//                 (when we discover that this is @ being released)
//
//      Alt-Up, Ctrl-Up
//                 (when the AltGr is released)

void WinKeyEvent(pDpyInfo, virtkey, keyData)
PDPYINFO pDpyInfo;
int virtkey;
DWORD keyData;
{
#ifdef DEBUG
  char keyname[32];
#endif
  KeyActionSpec kas;
  int down = ((keyData & 0x80000000l) == 0);
  int i;
  int winShift = 0;

  /* Attempt to discard spurious keys and 'fake' loss of focus */
  if (pDpyInfo->onedge && down) {
    debug("Ignore key while onedge\n");
    return;
  }
  // if virtkey found in mapping table, send X equivalent
  // else
  //   try to convert directly to ascii
  //   if result is in range supported by X keysyms,
  //      raise any modifiers, send it, then restore mods
  //   else
  //      calculate what the ascii would be without mods
  //      send that

#ifdef DEBUG
  if (GetKeyNameText(  keyData,keyname, 31)) {
    debug("Process key: %s (vk %d keyData %04x): ", keyname, virtkey, (unsigned int)keyData);
  };
#endif

  if (doCapsLkHack && (virtkey == VK_CAPITAL)) {
    /* We rely on Windows to process Caps Lock so don't send to X */
    debug(" Ignore Caps Lock\n");
    return;
  }

  /* Special cases */
  if ((virtkey == VK_HOME) || (virtkey == VK_END)) {
    if (GetKeyState(VK_RMENU) & 0x8000) { // right ALT down
      debug("Magic key \n");
      SendKeyEvent(pDpyInfo, XK_Alt_R, False, False, 0);
      if (virtkey == VK_END)
        exit(0); // Is there a proper way?

      DoWinDisconnect(pDpyInfo, -1, -1);
      return;
    }
  }

  kas = PCtoX(virtkey, keyData);

  if (kas.releaseModifiers & KEYMAP_LCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_L, False, False, 0);
    debug("fake L Ctrl raised\n");
  }

  if (kas.releaseModifiers & KEYMAP_LALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_L, False, False, 0);
    debug("fake L Alt raised\n");
  }

  if (kas.releaseModifiers & KEYMAP_RCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_R, False, False, 0);
    debug("fake R Ctrl raised\n");
  }

  if (kas.releaseModifiers & KEYMAP_RALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_R, False, False, 0);
    debug("fake R Alt raised\n");
  }

  /* NOTE: confusingly the 'keycodes' array actually contains keysyms */

  if (doCapsLkHack) {   /* Arguably this should be the dafault */
    winShift = (((GetKeyState(VK_LSHIFT) & 0x8000) ? 1 : 0) |
                ((GetKeyState(VK_RSHIFT) & 0x8000) ? 2 : 0));
    debug(" winShift %d ", winShift);
  }
  for (i = 0; kas.keycodes[i] != XK_VoidSymbol && i < MaxKeysPerKey; i++) {
    SendKeyEvent(pDpyInfo, kas.keycodes[i], down, doCapsLkHack, winShift);
      debug("Sent keysym %04x (%s)\n",
             (int)kas.keycodes[i], down ? "press" : "release");
  }

  if (kas.releaseModifiers & KEYMAP_RALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_R, True, False, 0);
    debug("fake R Alt pressed\n");
  }

  if (kas.releaseModifiers & KEYMAP_RCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_R, True, False, 0);
    debug("fake R Ctrl pressed\n");
  }

  if (kas.releaseModifiers & KEYMAP_LALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_L, False, False, 0);
    debug("fake L Alt pressed\n");
  }

  if (kas.releaseModifiers & KEYMAP_LCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_L, False, False, 0);
    debug("fake L Ctrl pressed\n");
  }
}

//
// SendKeyEvent
//

/* Note implementation of capsLockHack is different from X version */
/* This version should also catch the case of keys that are shifted on from */
/* but unshifted on to. Eg '<' above comma on from, going to '>' above '<' */
void SendKeyEvent(PDPYINFO pDpyInfo, KeySym keysym, int down,
                  int chkShift, int winShift)
{
  KeyCode keycode;
  PSHADOW   pShadow;
  int invShift;

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
    if ((keycode = XKeysymToKeycode(pShadow->dpy, keysym))) {
      invShift = 0;
      if (chkShift && (keysym != XK_Shift_R) && (keysym != XK_Shift_L)) {
        /* Check that the shift key matches where the keysym is */
        if (XKeycodeToKeysym(pShadow->dpy, keycode, winShift ? 1:0) != keysym){
          /* Ok, key does not match with current shift */
          if (XKeycodeToKeysym(pShadow->dpy,keycode,winShift ? 0:1) == keysym){
            /* But does with shift inverted */
            invShift = 1;
            debug("Invert shift ");
          }
          else
            debug(" keysym not keycode 0 or 1 hope for the best ");
        }
      }

      /* XXX mdh -- As far as I can tell we will never generate RSHIFT */
      /* Windows reports virtkey=SHIFT for both types which sends      */
      /* left shift to X. The code here is simpler taking advantage    */
      /* of this. But I had already written the general case, and if   */
      /* PCtoX is fixed to use both shifts, then you need to define    */
      /* USING_RSHIFT  */

      if (invShift) {
        KeyCode toShiftLCode = XKeysymToKeycode(pShadow->dpy, XK_Shift_L);
#ifdef USING_RSHIFT
        KeyCode toShiftRCode = XKeysymToKeycode(pShadow->dpy, XK_Shift_R);
#endif
        /* XXX mdh - Would it be better to only mess with shifts on down */
        /* XXX mdh - and only restore on up? */

        if (winShift == 0) { // Need to press, choose left
          XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, True, 0);
          debug("LSdown ");
        } else {
          // Release whichever is pressed or both
#ifdef USING_RSHIFT
          if (winShift & 1) {
                      XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, False, 0);
                  debug("LSup ");
          }
          if (winShift & 2) {
                      XTestFakeKeyEvent(pShadow->dpy, toShiftRCode, False, 0);
                  debug("RSup ");
          }
#else /* not USING_RSHIFT */
          /* Since we only ever send Left shifts, thats all we need release */
          XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, False, 0);
          debug("LSup ");
#endif /* USING_RSHIFT */
        }
        XTestFakeKeyEvent(pShadow->dpy, keycode, down, 0);
        if (winShift == 0) // Needed to press, so release
          XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, False, 0);
        else {
#ifdef USING_RSHIFT
          // Restore whichever is pressed
          if (winShift & 1)
                      XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, True, 0);
          if (winShift & 2)
                      XTestFakeKeyEvent(pShadow->dpy, toShiftRCode, True, 0);
#else /* not USING_RSHIFT */
          XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, True, 0);
#endif /* USING_RSHIFT */
        }
      }
      else
        XTestFakeKeyEvent(pShadow->dpy, keycode, down, 0);

      XFlush(pShadow->dpy);
    } /* END if */
  } /* END for */
  if (doAutoUp)
    FakeAction(pDpyInfo, FAKE_KEY, keysym, down);
}

/* SelectionClear event indicates we lost the selection */
static Bool ProcessSelectionClearW(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionClearEvent *pEv;
{
  debug("selection clear W\n");

  if (pEv->selection == XA_PRIMARY) {
    pDpyInfo->owntoXsel = 0;
    if (pDpyInfo->winSelText) {
      free(pDpyInfo->winSelText);
      pDpyInfo->winSelText = NULL;
    }
  } /* END if primary */
  return False;

} /* END ProcessSelectionClearW */

static Bool ProcessSelectionRequestW(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionRequestEvent *pEv;
{
  debug("selection request W\n");

  /* only do strings to PRIMARY */
  if ((pDpyInfo->winSelText == NULL) ||
      (pEv->selection != XA_PRIMARY) ||
      (pEv->target != XA_STRING)) { /* bad request, punt request */
    pEv->property = None;
    SendSelectionNotify(pEv); /* blam! */
  } else {
    XChangeProperty(pEv->display, pEv->requestor,
                    pEv->property, XA_STRING, 8, PropModeReplace,
                    pDpyInfo->winSelText, strlen(pDpyInfo->winSelText));
    SendSelectionNotify(pEv);
  }
  return False;
} /* END ProcessSelectionRequest */

/* This gets called back once the other X app has posted the property */
static Bool ProcessSelectionNotifyW(dpy, pDpyInfo, pEv)
Display *dpy;
PDPYINFO pDpyInfo;
XSelectionEvent *pEv;
{
  Atom type;
  int  format;
  unsigned long nitems, after;
  unsigned char *prop;
  Bool success;

#define DEFAULT_PROP_SIZE 1024L

  debug("selection notify\n");
  pDpyInfo->expectSelNotify = 0;
  /* property None indicates the source couldn't send anything */
  if (pEv->property != None) {
    /* Grab the property */
    success = False;
    if (XGetWindowProperty(dpy, pEv->requestor, XA_PRIMARY, 0L,
                           DEFAULT_PROP_SIZE, True, AnyPropertyType,
                           &type, &format, &nitems, &after, &prop)
        == Success) { /* got property */
      /* Check there is a string */
      /* XXX mdh -- emacs seems to give me type 233 but its useable */
      if ((type != None) &&
          ((type == XA_STRING) || !doClipCheck) &&
          (format != None) && (nitems != 0) &&
          (prop != None)) { /* known type */
        if (after == 0L) { /* got everything */
          success = True;
        } else { /* try to get everything */
          XFree(prop);
          /* The trick here is to find the whole size from what it told us */
          success =
            ((XGetWindowProperty(dpy, pEv->requestor, XA_PRIMARY, 0L,
                                 DEFAULT_PROP_SIZE + after + 1,
                                 True, AnyPropertyType,
                                 &type, &format, &nitems, &after, &prop)
              == Success) &&
             (type != None) && (format != None) && (nitems != 0) &&
             (after == 0L) && (prop != None));
        } /* END if got everything ... else ...*/
      } /* END if known type */
      else debug("Bad prop: type %d, format %d, nitems %d, prop %d(%s)\n",
                  (int)type, format, (int)nitems, (int)prop,
                  (prop == None) ? "none": (char *)prop);
    } /* END if got property */
      else debug("Did not get property\n");

    if (success) { /* send bits to the Windows Clipboard */
      debug("Send X selection to Windows Clipboard: %s\n", prop);
      if (OpenClipboard(pDpyInfo->edgewindow)) {
        HGLOBAL hglbCopy;
        EmptyClipboard();
        hglbCopy = GlobalAlloc(GMEM_MOVEABLE,
                               (strlen(prop) + 1) * sizeof(char));
        if (hglbCopy != NULL)
        {
          // Lock the handle and copy the text to the buffer.
          LPTSTR lptstrCopy = GlobalLock(hglbCopy);
          strcpy(lptstrCopy, prop);
          GlobalUnlock(hglbCopy);
          // Place the handle on the clipboard.
          SetClipboardData(CF_TEXT, hglbCopy);
          pDpyInfo->expectOwnClip = strlen(prop) + 1;
        }
        CloseClipboard();
      } /* END if open ok */
      XFree(prop);
    }
  }/* end ev->property != None */
  return False;
} /* END ProcessSelectionNotify */

#endif /* WIN_2_X only routines */
