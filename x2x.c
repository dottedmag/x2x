/*
 * x2x: Uses the XTEST extension to forward keystrokes from a window on
 *      one display to another display.  Useful for desks
 *      with multiple keyboards.
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
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h> /* for selection */
#include <X11/Xos.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include "format.h"

#ifdef WIN_2_X
#include <windows.h>
#include <windowsx.h>
#include <assert.h>
#include "keymap.h"
#include "resource.h"
#endif

/*#define DEBUG*/

/**********
 * definitions for edge
 **********/
#define EDGE_NONE   0 /* don't transfer between edges of screens */
#define EDGE_EAST   1 /* from display is on the east side of to display */
#define EDGE_WEST   2 /* from display is on the west side of to display */

/**********
 * functions
 **********/
static void    ParseCommandLine();
static Display *OpenAndCheckDisplay();
static Bool    CheckTestExtension();
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
 * text formatting instructions
 **********/
#define toDpyFormatLength (sizeof(toDpyFormat) / sizeof(Format))
static Format toDpyFormat[] = {
  FormatMeasureText,
  FormatSetLeft,      0,
  FormatSetTop,       0,
  FormatAddHalfTextX, 1,
  FormatAddHalfTextY, 3,
  FormatString, (Format)"unknown",
  FormatAddHalfTextX, 1,
  FormatAddHalfTextY, 1
  };
/* indexes of values to be filled in at runtime */
#define toDpyLeftIndex    2
#define toDpyTopIndex     4
#define toDpyStringIndex 10

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

#define N_BUTTONS   5

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
  Window  root;
  Window  trigger;
  Window  big;
  GC      textGC;
  Atom    wmpAtom, wmdwAtom;
  Cursor  grabCursor;
  XFS     *font;
  int     twidth, theight;
  int     lastFromX;
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
#endif /* WIN_2_X */

  /* stuff on "to" display */
  Display *toDpy;
  Window  selWin;
  unsigned int inverseMap[N_BUTTONS + 1]; /* inverse of button mapping */

  /* state of connection */
  int     mode;			/* connection */
  int     eventMask;		/* trigger */

  /* coordinate conversion stuff */
  int     toScreen;
  int     nScreens;
  short   **xTables; /* precalculated conversion tables */
  short   **yTables;
  int     fromXConn, fromXDisc; /* location of cursor after conn/disc ops */
  int     fromXIncr, fromXDecr; /* location of cursor after incr/decr ops */

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
static Bool    doClipCheck = False;

#ifdef WIN_2_X
/* These are used to allow pointer comparisons */
static char *fromWinName = "x2xFromWin";
static int dummy;
static Display *fromWin = (Display *)&dummy;
static HWND hWndSave;
static HINSTANCE m_instance;
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

  /* no OS independent wat to stop Xlib from complaining via stderr,
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

#ifdef DEBUG
  printf ("programStr = %s\n", programStr);
#endif

  for (arg = 1; arg < argc; ++arg) {
#ifdef WIN_2_X
    if (!strcasecmp(argv[arg], "-fromWin")) {
      fromDpyName = fromWinName;
      /* XXX mdh - For now only support edge windows getting big */
      /* Note: -east will override correctly (even if earlier on the line) */
      doBig = True;
      if (doEdge == EDGE_NONE) doEdge = EDGE_WEST;
      doCapsLkHack = True;

#ifdef DEBUG
      printf ("fromDpyName = %s\n", fromDpyName);
#endif
    } else
      /* Note this else will qualify the if below... */
#endif /* WIN_2_X */
    if (!strcasecmp(argv[arg], "-from")) {
      if (++arg >= argc) Usage();
      fromDpyName = argv[arg];

#ifdef DEBUG
      printf ("fromDpyName = %s\n", fromDpyName);
#endif
    } else if (!strcasecmp(argv[arg], "-to")) {
      if (++arg >= argc) Usage();
      toDpyName = argv[arg];

#ifdef DEBUG
      printf ("toDpyName = %s\n", toDpyName);
#endif
    } else if (!strcasecmp(argv[arg], "-font")) {
      if (++arg >= argc) Usage();
      fontName = argv[arg];

#ifdef DEBUG
      printf ("fontName = %s\n", fontName);
#endif
    } else if (!strcasecmp(argv[arg], "-geometry")) {
      if (++arg >= argc) Usage();
      geomStr = argv[arg];

#ifdef DEBUG
      printf ("geometry = %s\n", geomStr);
#endif
    } else if (!strcasecmp(argv[arg], "-wait")) {
      waitDpy = True;

#ifdef DEBUG
      printf("will wait for displays\n");
#endif
    } else if (!strcasecmp(argv[arg], "-big")) {
      doBig = True;

#ifdef DEBUG
      printf("will create big window on from display\n");
#endif
    } else if (!strcasecmp(argv[arg], "-nomouse")) {
      doMouse = False;

#ifdef DEBUG
      printf("will not capture mouse (eek!)\n");
#endif
    } else if (!strcasecmp(argv[arg], "-nopointermap")) {
      doPointerMap = False;

#ifdef DEBUG
      printf("will not do pointer mapping\n");
#endif
    } else if (!strcasecmp(argv[arg], "-east")) {
      doEdge = EDGE_EAST;
#ifdef DEBUG
      printf("\"from\" is on the east side of \"to\"\n");
#endif
    } else if (!strcasecmp(argv[arg], "-west")) {
      doEdge = EDGE_WEST;
#ifdef DEBUG
      printf("\"from\" is on the west side of \"to\"\n");
#endif
    } else if (!strcasecmp(argv[arg], "-nosel")) {
      doSel = False;
#ifdef DEBUG
      printf("will not transmit X selections between displays\n");
#endif
    } else if (!strcasecmp(argv[arg], "-noautoup")) {
      doAutoUp = False;
#ifdef DEBUG
      printf("will not automatically lift keys and buttons\n");
#endif
    } else if (!strcasecmp(argv[arg], "-buttonblock")) {
      doBtnBlock = True;
#ifdef DEBUG
      printf("mouse buttons down will block disconnects\n");
#endif
    } else if (!strcasecmp(argv[arg], "-capslockhack")) {
      doCapsLkHack = True;
#ifdef DEBUG
      printf("behavior of CapsLock will be hacked\n");
#endif
    } else if (!strcasecmp(argv[arg], "-clipcheck")) {
      doClipCheck = True;
#ifdef DEBUG
      printf("Clipboard type will be checked for XA_STRING\n");
#endif
    } else if (!strcasecmp(argv[arg], "-nocapslockhack")) {
      doCapsLkHack = False;
#ifdef DEBUG
      printf("behavior of CapsLock will not be hacked\n");
#endif
    } else if (!strcasecmp(argv[arg], "-sticky")) {
      if (++arg >= argc) Usage();
      if ((keysym = XStringToKeysym(argv[arg])) != NoSymbol) {
	pNewSticky = (PSTICKY)malloc(sizeof(STICKY));
	pNewSticky->pNext  = stickies;
	pNewSticky->keysym = keysym;
	stickies = pNewSticky;
#ifdef DEBUG
	printf("will press/release sticky key: %s\n", argv[arg]);
#endif
      } else {
	printf("x2x: warning: can't translate %s\n", argv[arg]);
      }
    } else if (!strcasecmp(argv[arg], "-resurface")) {
      doResurface = True;
#ifdef DEBUG
      printf("will resurface the trigger window when obscured\n");
#endif
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
#ifdef WIN_2_X
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
    MSG		msg;		/* A Win32 message structure. */
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
  int       twidth, theight; /* text dimensions */
  int       xoff, yoff; /* window offsets */
  unsigned int width, height; /* window width, height */
  int       geomMask;		/* mask returned by parse */
  int       gravMask;
  int       gravity;
  int       xret, yret, wret, hret, bret, dret;
  XSetWindowAttributes xswa;
  XSizeHints *xsh;
  int       eventMask;
  GC        textGC;
  char      *windowName;
  XFS       *font;
  PSHADOW   pShadow;
  int       triggerLoc;

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
#endif

  /* other dpyinfo values */
  pDpyInfo->mode        = X2X_DISCONNECTED;
  pDpyInfo->unreasonableDelta = fromWidth / 2;
  pDpyInfo->pFakeThings = NULL;

  /* window init structures */
  xswa.override_redirect = True;
  xsh = XAllocSizeHints();
  eventMask = KeyPressMask | KeyReleaseMask;

  /* cursor locations for moving between screens */
  pDpyInfo->fromXIncr = triggerw;
  pDpyInfo->fromXDecr = fromWidth - triggerw - 1;
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

    if (doEdge == EDGE_EAST) {
      /* trigger window location */
      triggerLoc = fromWidth - triggerw;
      toHeight = XHeightOfScreen(XScreenOfDisplay(toDpy, 0));
      pDpyInfo->fromXConn = 1;
      pDpyInfo->fromXDisc = fromWidth - triggerw - 1;
    } else {
      /* trigger window location */
      triggerLoc = 0;
      toHeight = XHeightOfScreen(XScreenOfDisplay(toDpy, nScreens - 1));
      toWidth  = XWidthOfScreen(XScreenOfDisplay(toDpy, nScreens - 1));
      pDpyInfo->fromXConn = fromWidth - triggerw - 1;
      pDpyInfo->fromXDisc = triggerw;
    } /* END if doEdge == ... */

    xswa.background_pixel = black;

#ifdef WIN_2_X
    if (fromDpy == fromWin) {

      /* Create both trigger and big windows here */
      /* This code is based on Win2VBC/vncviewer ClientConnection.cpp */
      WNDCLASS wndclass;
      const DWORD winstyle = 0;

      wndclass.style		= 0;
      wndclass.lpfnWndProc	= WinProcessMessage;
      wndclass.cbClsExtra	= 0;
      wndclass.cbWndExtra	= 0;
      wndclass.hInstance	= m_instance;
      /* XXX mdh - For now just use system provided resources */
      wndclass.hIcon		= LoadIcon(NULL, IDI_APPLICATION);
      // wndclass.hCursor	= LoadCursor(NULL, IDC_NO);
      wndclass.hCursor		= LoadCursor(m_instance, MAKEINTRESOURCE(IDC_NOCURSOR));
      wndclass.hbrBackground	= (HBRUSH) GetStockObject(BLACK_BRUSH);
      wndclass.lpszMenuName	= (const TCHAR *) NULL;
      wndclass.lpszClassName	= fromWinName;

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
	 (doEdge == EDGE_EAST) ? fromWidth -1: 0,
	 0,
	 1,
	 fromHeight,
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
    trigger = pDpyInfo->trigger =
      XCreateWindow(fromDpy, root, triggerLoc, 0, triggerw, fromHeight,
		    0, 0, InputOutput, 0,
		    CWBackPixel | CWOverrideRedirect, &xswa);
#ifdef WIN_2_X
    }
#endif
    font = NULL;

  } else { /* normal window for text: do size grovelling */
    pDpyInfo->grabCursor = XCreateFontCursor(fromDpy, XC_exchange);
    eventMask |= StructureNotifyMask | ExposureMask;
    if (doMouse) eventMask |= ButtonPressMask | ButtonReleaseMask;

    /* determine size of text */
    if (((font = XLoadQueryFont(fromDpy, fontName)) != NULL) ||
	((font = XLoadQueryFont(fromDpy, defaultFN)) != NULL) ||
	((font = XLoadQueryFont(fromDpy, "fixed")) != NULL)) {
      /* have a font */
      toDpyFormat[toDpyStringIndex] = (Format)toDpyName;
      formatText(NULL, NULL, NULL, font,
		 toDpyFormatLength, toDpyFormat, &twidth, &theight);

      textGC = pDpyInfo->textGC = XCreateGC(fromDpy, root, 0, NULL);
      XSetState(fromDpy, textGC, black, white, GXcopy, AllPlanes);
      XSetFont(fromDpy, textGC, font->fid);

    } else { /* should not have to execute this clause: */
      twidth = theight = 100; /* default window size */
    } /* END if have a font ... else ... */

    /* determine size of window */
    xoff = yoff = 0;
    width = twidth;
    height = theight;
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

  windowName = (char *)malloc(strlen(programStr) + strlen(toDpyName) + 2);
  strcpy(windowName, programStr);
  strcat(windowName, " ");
  strcat(windowName, toDpyName);
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
  free(windowName);
#ifdef WIN_2_X
  }
#endif

  /* conversion stuff */
  pDpyInfo->toScreen = (doEdge == EDGE_WEST) ? (nScreens - 1) : 0;

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
    if ((screenNum != 0) || (doEdge == EDGE_EAST))
      xTable[0] = COORD_DECR;
    if (((screenNum + 1) < nScreens) || (doEdge == EDGE_WEST)) {
      xTable[fromWidth - 1] = COORD_INCR;
      /* work-around for bug: on at least one tested screen, cursor
	 never moved past fromWidth - 2 */
      xTable[fromWidth - 2] = COORD_INCR;
    }

  } /* END for screenNum */

  free(heights);
  free(widths);

  /* always create propWin for events from toDpy */
  propWin = XCreateWindow(toDpy, toRoot, 0, 0, 1, 1, 0, 0, InputOutput,
			  CopyFromParent, 0, NULL);
  pDpyInfo->toDpyXtra.propWin = propWin;
#ifdef DEBUG
  printf("Create window %x on todpy\n", (unsigned int)propWin);
#endif
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
#ifdef DEBUG
    printf("SelectionOwner to propWin %x\n", (unsigned int)propWin);
#endif
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
  if ((pDpyInfo->font = font)) { /* paint text */
    /* position text */
    pDpyInfo->twidth = twidth;
    pDpyInfo->theight = theight;
    toDpyFormat[toDpyLeftIndex] = MAX(0,((width - twidth) / 2));
    toDpyFormat[toDpyTopIndex]  = MAX(0,((height - theight) / 2));

    formatText(fromDpy, trigger, &(textGC), font,
	       toDpyFormatLength, toDpyFormat, NULL, NULL);
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

#ifdef DEBUG
  printf("connecting\n");
#endif
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

#ifdef DEBUG
  printf("disconnecting\n");
#endif
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
#ifdef DEBUG
    printf("no handler for window 0x%x, event type %d\n",
	   (unsigned int)pEv->window, pEv->type);
#endif
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
  int       toX, fromX, delta;
  Display   *fromDpy;
  Bool      bAbortedDisconnect;

  /* find the screen */
  toScreenNum = pDpyInfo->toScreen;
  fromX = pEv->x_root;

  /* check to make sure the cursor is still on the from screen */
  if (!(pEv->same_screen)) {
    toX = (pDpyInfo->lastFromX < fromX) ? COORD_DECR : COORD_INCR;
  } else {
    toX = pDpyInfo->xTables[toScreenNum][fromX];

    /* sanity check motion: necessary for nondeterminism surrounding warps */
    delta = pDpyInfo->lastFromX - fromX;
    if (delta < 0) delta = -delta;
    if (delta > pDpyInfo->unreasonableDelta) return False;
  }

  if (SPECIAL_COORD(toX) != 0) { /* special coordinate */
    bAbortedDisconnect = False;
    if (toX == COORD_INCR) {
      if (toScreenNum != (pDpyInfo->nScreens - 1)) { /* next screen */
	toScreenNum = ++(pDpyInfo->toScreen);
	fromX = pDpyInfo->fromXIncr;
	toX = pDpyInfo->xTables[toScreenNum][fromX];
      } else { /* disconnect! */
	if (doBtnBlock &&
	    (pEv->state & (Button1Mask | Button2Mask | Button3Mask |
			   Button4Mask | Button5Mask)))
	  bAbortedDisconnect = True;
	else {
	  DoDisconnect(pDpyInfo);
	  fromX = pDpyInfo->fromXDisc;
	}
	toX = pDpyInfo->xTables[toScreenNum][pDpyInfo->fromXConn];
      }
    } else { /* DECR */
      if (toScreenNum != 0) { /* previous screen */
	toScreenNum = --(pDpyInfo->toScreen);
	fromX = pDpyInfo->fromXDecr;
	toX = pDpyInfo->xTables[toScreenNum][fromX];
      } else { /* disconnect! */
	if (doBtnBlock &&
	    (pEv->state & (Button1Mask | Button2Mask | Button3Mask |
			   Button4Mask | Button5Mask)))
	  bAbortedDisconnect = True;
	else {
	  DoDisconnect(pDpyInfo);
	  fromX = pDpyInfo->fromXDisc;
	}
	toX = pDpyInfo->xTables[toScreenNum][pDpyInfo->fromXConn];
      }
    } /* END if toX */
    if (!bAbortedDisconnect) {
      fromDpy = pDpyInfo->fromDpy;
      XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
		   fromX, pEv->y_root);
      XFlush(fromDpy);
    }
  } /* END if SPECIAL_COORD */
  pDpyInfo->lastFromX = fromX;

  for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
    XTestFakeMotionEvent(pShadow->dpy, toScreenNum, toX,
			 pDpyInfo->yTables[toScreenNum][pEv->y_root], 0);
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
  if (pDpyInfo->font)
    formatText(pDpyInfo->fromDpy, pDpyInfo->trigger,
	       &(pDpyInfo->textGC), pDpyInfo->font,
	       toDpyFormatLength, toDpyFormat, NULL, NULL);
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
    XWarpPointer(fromDpy, None, pDpyInfo->root, 0, 0, 0, 0,
		 pDpyInfo->fromXConn, pEv->y_root);
    xmev.x_root = pDpyInfo->lastFromX = pDpyInfo->fromXConn;
    xmev.y_root = pEv->y_root;
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

  switch (pDpyInfo->mode) {
  case X2X_DISCONNECTED:
    pDpyInfo->mode = X2X_AWAIT_RELEASE;
#ifdef DEBUG
    printf("awaiting button release before connecting\n");
#endif
    break;
  case X2X_CONNECTED:
    if (pEv->button <= N_BUTTONS) {
      toButton = pDpyInfo->inverseMap[pEv->button];
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
	XTestFakeButtonEvent(pShadow->dpy, toButton, True, 0);
#ifdef DEBUG
	printf("from button %d down, to button %d down\n", pEv->button,toButton);
#endif
	XFlush(pShadow->dpy);
      } /* END for */
      if (doAutoUp)
	FakeAction(pDpyInfo, FAKE_BUTTON, toButton, True);
      if (doEdge) break;
    }

    /* check if more than one button pressed */
    state = pEv->state;
    switch (pEv->button) {
    case Button1: state &= ~Button1Mask; break;
    case Button2: state &= ~Button2Mask; break;
    case Button3: state &= ~Button3Mask; break;
    case Button4: state &= ~Button4Mask; break;
    case Button5: state &= ~Button5Mask; break;
    default:
#ifdef DEBUG
      printf("unknown button %d\n", pEv->button);
#endif
      break;
    } /* END switch button */
    if (state) { /* then more than one button pressed */
#ifdef DEBUG
      printf("awaiting button release before disconnecting\n");
#endif
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
    if (pEv->button <= N_BUTTONS) {
      toButton = pDpyInfo->inverseMap[pEv->button];
      for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
	XTestFakeButtonEvent(pShadow->dpy, toButton, False, 0);
#ifdef DEBUG
	printf("from button %d up, to button %d up\n", pEv->button, toButton);
#endif
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
#ifdef DEBUG
      printf("unknown button %d\n", pEv->button);
#endif
      break;
    } /* END switch button */
    if (!state) { /* all buttons up: time to (dis)connect */
      if (pDpyInfo->mode == X2X_AWAIT_RELEASE) { /* connect */
	DoConnect(pDpyInfo);
	xmev.x_root = pDpyInfo->lastFromX = pEv->x_root;
	xmev.y_root = pEv->y_root;
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

  XLookupString(pEv, NULL, 0, &keysym, NULL);
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
  if (pDpyInfo->font) {
    /* reposition text */
    toDpyFormat[toDpyLeftIndex] =
      MAX(0,((pEv->width - pDpyInfo->twidth) / 2));
    toDpyFormat[toDpyTopIndex]  =
      MAX(0,((pEv->height - pDpyInfo->theight) / 2));
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

#ifdef DEBUG
    printf("selection request\n");
#endif

  /* bribe me to support more general selection requests,
     or send me the code to do it. */
  if ((pDpyXtra->sState != SELSTATE_ON) ||
      (pEv->selection != XA_PRIMARY) ||
      (pEv->target > XA_LAST_PREDEFINED)) { /* bad request, punt request */
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

#ifdef DEBUG
    printf("property notify\n");
#endif

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

#define DEFAULT_PROP_SIZE 1024L

#ifdef DEBUG
    printf("selection notify\n");
#endif

  if ((dpy == pDpyInfo->sDpy) && (pDpyInfo->sTime == pEv->time)) {
    success = False;
    /* corresponding select */
    if (XGetWindowProperty(dpy, pEv->requestor, XA_PRIMARY, 0L,
			   DEFAULT_PROP_SIZE, True, AnyPropertyType,
			   &type, &format, &nitems, &after, &prop)
	== Success) { /* got property */
      if ((type != None) && (format != None) && (nitems != 0) &&
	  (prop != None) && (type <= XA_LAST_PREDEFINED)) { /* known type */
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

#ifdef DEBUG
    printf("selection clear\n");
#endif

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
#ifdef DEBUG
  printf("mapping\n");
#endif

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
#ifdef DEBUG
	    printf("key 0x%x up\n", pFake->thing);
#endif
	  } /* END if */
	} else { /* button goes up */
	  XTestFakeButtonEvent(pShadow->dpy, pFake->thing, False, 0);
#ifdef DEBUG
	  printf("button %d up\n", pFake->thing);
#endif
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
  int nButtons;

  if (dpy == pDpyInfo->toDpy) { /* only care about toDpy */
    /* straightforward mapping */
    for (buttCtr = 1; buttCtr <= N_BUTTONS; ++buttCtr) {
      pDpyInfo->inverseMap[buttCtr] = buttCtr;
    } /* END for */

    nButtons = MIN(N_BUTTONS, XGetPointerMapping(dpy, buttonMap, N_BUTTONS));
#ifdef WIN_2_X
    pDpyInfo->nXbuttons = nButtons;
#endif
    if (doPointerMap) {
      for (buttCtr = 0; buttCtr < nButtons; ++buttCtr) {
#ifdef DEBUG
	printf("button %d -> %d\n", buttCtr + 1, buttonMap[buttCtr]);
#endif
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
#ifdef DEBUG
  printf("MoveWindowToEdge\n");
#endif

  SetWindowPos(pDpyInfo->bigwindow, HWND_BOTTOM,
	       0, 0,
	       pDpyInfo->fromWidth, pDpyInfo->fromHeight,
	       SWP_HIDEWINDOW /* | SWP_NOREDRAW */);

  SetWindowPos(pDpyInfo->edgewindow, HWND_TOPMOST,
	       (doEdge == EDGE_EAST) ? pDpyInfo->fromWidth -1: 0,
	       0,
	       1,
	       pDpyInfo->fromHeight,
	       SWP_SHOWWINDOW | SWP_NOREDRAW);

  pDpyInfo->onedge=1;

  SetForegroundWindow(hWndSave);
}

int MoveWindowToScreen(PDPYINFO pDpyInfo)
{
  int notfg;
  LPINPUT pInputs;

  if(!pDpyInfo->onedge) return 1;
#ifdef DEBUG
  printf("MoveWindowToScreen\n");
#endif

  hWndSave = GetForegroundWindow();

  if ((notfg = (SetForegroundWindow(pDpyInfo->bigwindow) == 0))) {
#ifdef DEBUG
    printf("Did not become foreground\n");
#endif

    /* This code thanks to Thomas Chadwick */
    /* Fakes that the user clicked the mouse to move the focus */
    /* This is to deal with the XP and 2000 behaviour that attempts to */
    /* prevent an application from stealing the focus */
#ifdef DEBUG
    printf("Using SendInput to synthesize a mouse click\n");
#endif

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
#ifdef DEBUG
      printf("SendInput failed\n");
#endif
      free(pInputs);
      return 0;
    }
    free(pInputs);
    return 1;
  }
  SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
	       0, 0,
	       pDpyInfo->fromWidth, pDpyInfo->fromHeight,
	       SWP_SHOWWINDOW | SWP_NOREDRAW);
  SetFocus(pDpyInfo->bigwindow);

  pDpyInfo->onedge=0;
  return 1;
}

static void DoWinConnect(pDpyInfo, x, y)
PDPYINFO pDpyInfo;
int x,y;
{

#ifdef DEBUG
  printf("connecting (Win2x)\n");
#endif
  pDpyInfo->mode = X2X_CONNECTED;

  if (!pDpyInfo->onedge) return;

  if (MoveWindowToScreen(pDpyInfo)) {
#ifdef DEBUG
    printf("Warp Cursor: %d,%d -> %d, %d\n",
	   x,y,
	   (doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3,
	   y);
#endif
    SetCursorPos((doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3, y);

    pDpyInfo->lastFromX = (doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3;
  }
}

static void DoWinDisconnect(pDpyInfo, x, y)
PDPYINFO pDpyInfo;
int x,y;
{

#ifdef DEBUG
  printf("disconnecting\n");
#endif
  pDpyInfo->mode = X2X_DISCONNECTED;

  /* If we own the X selection, then windows has it! */
  /* otherwise transfer the info by asking the owning X window to */
  /* tell us (via ProcessSelectNotifyW) */
  if (!pDpyInfo->owntoXsel) {
#ifdef DEBUG
    printf("Ask X for -to selection\n");
#endif
    XConvertSelection(pDpyInfo->toDpy, XA_PRIMARY, XA_STRING,
		      XA_PRIMARY, pDpyInfo->toDpyXtra.propWin, CurrentTime);
    XFlush(pDpyInfo->toDpy);
    pDpyInfo->expectSelNotify = 1;
  }

  if (x >= 0) {
#ifdef DEBUG
    printf("Warp Cursor: %d,%d -> %d, %d\n",
	   x,y,
	   (doEdge == EDGE_EAST) ? pDpyInfo->fromWidth - 2 : 2,
	   y);
#endif
    SetCursorPos((doEdge == EDGE_EAST) ? pDpyInfo->fromWidth - 2 : 2, y);
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
#ifdef DEBUG
      printf("#");
#endif
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
#ifdef DEBUG
	  printf("No focus and pDpyInfo NULL\n");
#endif
	  return 0;
	}
	if (pDpyInfo->onedge) {
#ifdef DEBUG
	  printf("No focus and currently on edge\n");
#endif
	  return 0;
	}
	if (hwnd == pDpyInfo->bigwindow) {
	  /* Ok, event for the bigwindow, but no focus -> take it */
#ifdef DEBUG
	  printf("No focus on bigwindow mouse event, grab it\n");
#endif
	  SetForegroundWindow(pDpyInfo->bigwindow);
	  SetFocus(pDpyInfo->bigwindow);
	}
#ifdef DEBUG
	else
	  printf("No focus, not on edge, not bigwindow\n");
#endif
      }

      pt.x = GET_X_LPARAM(lParam);
      pt.y = GET_Y_LPARAM(lParam);

      ClientToScreen(hwnd, &pt);
      x = pt.x;
      y = pt.y;

      if(x<0 || x>32768) x=0;
      if(y<0 || y>32768) y=0;
      if(x>=pDpyInfo->fromWidth) x = pDpyInfo->fromWidth - 1;
      if(y>=pDpyInfo->fromHeight) y = pDpyInfo->fromHeight - 1;

      if(pDpyInfo->onedge)
      {
	if(hwnd == pDpyInfo->edgewindow)
	{
#ifdef DEBUG
	  printf("onedge mouse connect\n");
#endif
	  DoWinConnect(pDpyInfo, x,y);
	}
	else {
#ifdef DEBUG
	  printf("onedge mouse move to non edge window ");
#endif
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
	delta = pDpyInfo->lastFromX - x;
	if (delta < 0) delta = -delta;
	if (delta > pDpyInfo->unreasonableDelta) {
	  /* Guess that the warp failed and try it again... */
#ifdef DEBUG
	  printf("Retry warp to (%d, %d)\n",
		 (doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3,
		 y);
#endif
	  SetCursorPos((doEdge == EDGE_EAST) ? 1 : pDpyInfo->fromWidth - 3, y);
	}
	//WinPointerEvent(pDpyInfo, x,y, wParam, iMsg);

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

#ifdef DEBUG
      printf("Wheel moved to new delta %d (%s window)\n", wdelta,
	     (hwnd == pDpyInfo->edgewindow) ? "edge" :
	     (hwnd == pDpyInfo->bigwindow) ? "big" : "other");
#endif

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
#ifdef DEBUG
	printf("Attempt to restore from screen saver (key)!\n");
#endif
	if (!pDpyInfo->onedge) {
	  SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
		       0, 0,
		       pDpyInfo->fromWidth, pDpyInfo->fromHeight,
		       SWP_HIDEWINDOW);
	  SetWindowPos(pDpyInfo->bigwindow, HWND_TOPMOST,
		       0, 0,
		       pDpyInfo->fromWidth, pDpyInfo->fromHeight,
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
#ifdef DEBUG
	printf("Activated by action: WM_SETCURSOR\n");
#endif
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
#ifdef DEBUG
	    /* XXX mdh - hope this does (null) as expected! */
	    printf("Windows clipboard changed to %s\n", pDpyInfo->winSelText);
#endif
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
#ifdef DEBUG
	      printf("Saw own addition to clipboard\n");
	    } else {
		printf("Oops. expectOwrClip %d with len %d\n",
		       pDpyInfo->expectOwnClip, len);
#endif
	    }
	  } else {
	    /* This can race during creation */
	    /* but we will claim selection in the init routine */
	    if (pDpyInfo->toDpyXtra.propWin != 0) {
#ifdef DEBUG
	      printf("Selection Owner to %x\n",
		     (unsigned int)pDpyInfo->toDpyXtra.propWin);
#endif
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
#ifdef DEBUG
      printf("WM_SYSCOMMAND with wParam %d (0x%x)\n", wParam, wParam);
#endif
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

      //    case WM_REGIONUPDATED:
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
  }

#ifdef DEBUG
  printf("Unused message: %d (0x%04x)\n", iMsg, iMsg);
#endif
  return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

void WinPointerEvent(PDPYINFO pDpyInfo,
		     int x, int y, DWORD keyflags, UINT msg)
{
  int down = 0;
  unsigned int button, toButton;
  int       toScreenNum;
  PSHADOW   pShadow;
  int       toX, fromX, fromY, delta;

  button = 0;
  switch (msg) {
  case WM_MOUSEMOVE:

    /* seems that we get repeats, ignore them */
    if ((x == pDpyInfo->lastFromX) && (y == pDpyInfo->lastFromY)) {
#ifdef DEBUGMOUSE
      printf("m() ");
#endif
      return;
    }
#ifdef DEBUGMOUSE
    printf("m(%d, %d) ", x,y);
#endif
    /* find the screen */
    toScreenNum = pDpyInfo->toScreen;
    fromX = x;
    fromY = y;
    toX = pDpyInfo->xTables[toScreenNum][fromX];

    /* sanity check motion: necessary for nondeterminism surrounding warps */
    delta = pDpyInfo->lastFromX - fromX;
    if (delta < 0) delta = -delta;
    if (delta > pDpyInfo->unreasonableDelta) {
      if (pDpyInfo->unreasonableCount++ < MAX_UNREASONABLES) {
#ifdef DEBUG
	printf("Unreasonable x delta last = %d this = %d\n",
	       pDpyInfo->lastFromX, fromX);
#endif
	return;
      }
      pDpyInfo->unreasonableCount = 0;
#ifdef DEBUG
      printf("Too many unreasonable deltas to really be unreasonable!\n");
#endif
    }

    if (SPECIAL_COORD(toX) != 0) { /* special coordinate */
      if (toX == COORD_INCR) {
	if (toScreenNum != (pDpyInfo->nScreens - 1)) { /* next screen */
	  toScreenNum = ++(pDpyInfo->toScreen);
	  fromX = pDpyInfo->fromXIncr;
	  toX = pDpyInfo->xTables[toScreenNum][fromX];
	} else { /* disconnect! */
#ifdef DEBUG
	  printf("INCR screen %d: ", toScreenNum);
#endif
	  if (doBtnBlock &&
	      (keyflags & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))) {
#ifdef DEBUG
	    printf("Disconnect aborted by button state 0x%x\n",
		   (unsigned int)keyflags);
#endif
	  } else {
	    DoWinDisconnect(pDpyInfo, x, y);
	    fromX = pDpyInfo->fromXDisc;
	  }
	  toX = pDpyInfo->xTables[toScreenNum][pDpyInfo->fromXConn];
	}
      } else { /* DECR */
	if (toScreenNum != 0) { /* previous screen */
	  toScreenNum = --(pDpyInfo->toScreen);
	  fromX = pDpyInfo->fromXDecr;
	  toX = pDpyInfo->xTables[toScreenNum][fromX];
	} else { /* disconnect! */
#ifdef DEBUG
	  printf("DECR screen %d: ", toScreenNum);
#endif
	  if (doBtnBlock &&
	      (keyflags & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))) {
#ifdef DEBUG
	    printf("Disconnect aborted by button state 0x%x\n",
		   (unsigned int)keyflags);
#endif
	  } else {
	    DoWinDisconnect(pDpyInfo, x, y);
	    fromX = pDpyInfo->fromXDisc;
	  }
	  toX = pDpyInfo->xTables[toScreenNum][pDpyInfo->fromXConn];
	}
      } /* END if toX */
    } /* END if SPECIAL_COORD */
    pDpyInfo->lastFromX = fromX;
    pDpyInfo->lastFromY = fromY;
    for (pShadow = shadows; pShadow; pShadow = pShadow->pNext) {
      XTestFakeMotionEvent(pShadow->dpy, toScreenNum, toX,
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
#ifdef DEBUG
	printf("from button %d %s, to button %d %s\n",
	       button, down ? "down":"up", toButton, down ? "down":"up");
#endif
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
#ifdef DEBUG
	printf("Click from button %d, to button %d\n",
	       button, toButton);
#endif
	XFlush(pShadow->dpy);
      } /* END for */
#ifdef DEBUG
    else
      printf("to only has %d buttons, cant clikc from %d -> to %d\n",
	     pDpyInfo->nXbuttons, button, toButton);
#endif
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
#ifdef DEBUG
    printf("Ignore key while onedge\n");
#endif
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
    printf("Process key: %s (vk %d keyData %04x): ", keyname, virtkey, (unsigned int)keyData);
  };
#endif

  if (doCapsLkHack && (virtkey == VK_CAPITAL)) {
    /* We rely on Windows to process Caps Lock so don't send to X */
#ifdef DEBUG
    printf(" Ignore Caps Lock\n");
#endif
    return;
  }

  /* Special cases */
  if ((virtkey == VK_HOME) || (virtkey == VK_END)) {
    if (GetKeyState(VK_RMENU) & 0x8000) { // right ALT down
#ifdef DEBUG
      printf("Magic key \n");
#endif
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
#ifdef DEBUG
    printf("fake L Ctrl raised\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_LALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_L, False, False, 0);
#ifdef DEBUG
    printf("fake L Alt raised\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_RCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_R, False, False, 0);
#ifdef DEBUG
    printf("fake R Ctrl raised\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_RALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_R, False, False, 0);
#ifdef DEBUG
    printf("fake R Alt raised\n");
#endif
  }

  /* NOTE: confusingly the 'keycodes' array actually contains keysyms */

  if (doCapsLkHack) {   /* Arguably this should be the dafault */
    winShift = (((GetKeyState(VK_LSHIFT) & 0x8000) ? 1 : 0) |
		((GetKeyState(VK_RSHIFT) & 0x8000) ? 2 : 0));
#ifdef DEBUG
    printf(" winShift %d ", winShift);
#endif
  }
  for (i = 0; kas.keycodes[i] != XK_VoidSymbol && i < MaxKeysPerKey; i++) {
    SendKeyEvent(pDpyInfo, kas.keycodes[i], down, doCapsLkHack, winShift);
#ifdef DEBUG
      printf("Sent keysym %04x (%s)\n",
	     (int)kas.keycodes[i], down ? "press" : "release");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_RALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_R, True, False, 0);
#ifdef DEBUG
    printf("fake R Alt pressed\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_RCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_R, True, False, 0);
#ifdef DEBUG
    printf("fake R Ctrl pressed\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_LALT) {
    SendKeyEvent(pDpyInfo, XK_Alt_L, False, False, 0);
#ifdef DEBUG
    printf("fake L Alt pressed\n");
#endif
  }

  if (kas.releaseModifiers & KEYMAP_LCONTROL) {
    SendKeyEvent(pDpyInfo, XK_Control_L, False, False, 0);
#ifdef DEBUG
    printf("fake L Ctrl pressed\n");
#endif
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
#ifdef DEBUG
	    printf("Invert shift ");
#endif
	  }
#ifdef DEBUG
	  else
	    printf(" keysym not keycode 0 or 1 hope for the best ");
#endif
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
#ifdef DEBUG
	  printf("LSdown ");
#endif
	} else {
	  // Release whichever is pressed or both
#ifdef USING_RSHIFT
	  if (winShift & 1) {
	    	  XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, False, 0);
#ifdef DEBUG
		  printf("LSup ");
#endif
	  }
	  if (winShift & 2) {
	    	  XTestFakeKeyEvent(pShadow->dpy, toShiftRCode, False, 0);
#ifdef DEBUG
		  printf("RSup ");
#endif
	  }
#else /* not USING_RSHIFT */
	  /* Since we only ever send Left shifts, thats all we need release */
	  XTestFakeKeyEvent(pShadow->dpy, toShiftLCode, False, 0);
#ifdef DEBUG
	  printf("LSup ");
#endif
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

#ifdef DEBUG
    printf("selection clear W\n");
#endif

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
#ifdef DEBUG
  printf("selection request W\n");
#endif

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

#ifdef DEBUG
  printf("selection notify\n");
#endif
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
#ifdef DEBUG
      else printf("Bad prop: type %d, format %d, nitems %d, prop %d(%s)\n",
		  (int)type, format, (int)nitems, (int)prop,
		  (prop == None) ? "none": (char *)prop);
#endif
    } /* END if got property */
#ifdef DEBUG
      else printf("Did not get property\n");
#endif

    if (success) { /* send bits to the Windows Clipboard */
#ifdef DEBUG
      printf("Send X selection to Windows Clipboard: %s\n", prop);
#endif
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
