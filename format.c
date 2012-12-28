/*
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
#include <X11/Xlib.h>
#include <string.h>
#include "format.h"

static void formatMeasureText();

void formatText(dpy, textw, pgc, tfont, nformats, formatv, pwidth, pheight)
Display *dpy;
Window  textw;
GC      *pgc;
XFS     *tfont;
int     nformats;
Format  *formatv;
int     *pwidth, *pheight;
{
  int   fontheight, fontwidth;
  int   x, y;
  int   len, textwidth, xoffset;
  int   format;
  int   top, left, lmargin, rmargin;
  int   width, height;
  char  *buffer;
  Bool  bMLineCenter;

  top = left = lmargin = rmargin = x = y = width = height = 0;
  fontheight = tfont->ascent + tfont->descent;
  fontwidth  = tfont->max_bounds.width;
  bMLineCenter = False;

  for (format = 0; format < nformats; ++format) {
    switch (formatv[format]) {
    case FormatSetTop:
      y = top = formatv[++format];
      break;
    case FormatSetLeft:
      x = left = formatv[++format];
      break;
    case FormatHome:
      x = left + lmargin;
      y = top;
      break;
    case FormatCR:
      x = left + lmargin;
      break;
    case FormatNL:
      x = left + lmargin;
      y += fontheight;
      break;
    case FormatSetY:
      y = top + formatv[++format];
      break;
    case FormatSetTextY:
      y = top + fontheight * formatv[++format];
      break;
    case FormatAddY:
      y += formatv[++format];
      break;
    case FormatAddTextY:
      y += fontheight * formatv[++format];
      break;
    case FormatAddHalfTextY:
      y += (fontheight >> 1) * formatv[++format];
      break;
    case FormatSetX:
      x = left + formatv[++format];
      break;
    case FormatSetTextX:
      x = left + fontwidth * formatv[++format];
      break;
    case FormatAddX:
      x += formatv[++format];
      break;
    case FormatAddTextX:
      x += fontwidth * formatv[++format];
      break;
    case FormatAddHalfTextX:
      x += (fontwidth >> 1) * formatv[++format];
      break;
    case FormatString:
      len = strlen((char *)formatv[++format]);
      if (textw)
	XDrawString(dpy, textw, *pgc, x, y, (char *)formatv[format], len);
      x += XTextWidth(tfont, (char *)formatv[format], len);
      break;
    case FormatStringCenter:
      x = left;
      len = strlen((char *)formatv[++format]);
      textwidth = XTextWidth(tfont, (char *)formatv[format], len);
      xoffset = MAX(lmargin, ((width >> 1) - (textwidth >> 1)));
      if (textw)
	XDrawString(dpy, textw, *pgc, x + xoffset, y, 
		    (char *)formatv[format], len);
      x += textwidth;
      break;
    case FormatMultiLine:
      buffer = (char *)formatv[++format];
      len = 0;
      x = left;
      while (*buffer) {
	/* if first character in the line is a tab, then center */
	if (buffer[len] == '\t') {
	  buffer++; /* don't print the tab */
	  bMLineCenter = True;
	}
	while ((buffer[len] != '\n') && buffer[len])
	  len++;
	textwidth = XTextWidth(tfont, buffer, len);
	if (textw) {
	  if (bMLineCenter) {
	    xoffset = MAX(lmargin, ((width >> 1) - (textwidth >> 1)));
	    bMLineCenter = False; /* ready for next line */
	  } else { /* normal print */
	    xoffset = lmargin;
	  }
	  XDrawString(dpy, textw, *pgc, left + xoffset, y, 
		      (char *)buffer, len);
	}
	if (buffer[len]) {
	  width = MAX(width, left + lmargin + textwidth + rmargin);
	  buffer += len + 1;
	  len = 0;
	  y += fontheight;
	}
	else {
	  x = left + lmargin + textwidth;
	  break;
	}
      }
      break;
    case FormatMeasureText:
      formatMeasureText(tfont, (nformats - format - 1),
			&formatv[format + 1], &fontwidth, &fontheight);
      break;
    case FormatSetWidth:
      width = formatv[++format];
      break;
    case FormatSetLMargin:
      if ((lmargin = formatv[++format]) == -1)
	lmargin = MAX(0, x - left);
      break;
    case FormatSetRMargin:
      if ((rmargin = formatv[++format]) == -1)
	rmargin = MAX(0, x - left);
      break;
    default: /* might want an error message here */
      break;
    } /* END switch */

    width  = MAX(width,  x - left + rmargin);
    height = MAX(height, y - top);

  } /* END for */

  if (pwidth)  *pwidth  = width;
  if (pheight) *pheight = height;

} /* END formatText */

static void formatMeasureText(tfont, nformats, formatv, pwidth, pheight)
XFS     *tfont;
int     nformats;
Format  *formatv;
int     *pwidth, *pheight;
{
  int           fontheight, fontwidth;
  int           width, height;
  int           len;
  int           format;
  unsigned      minchar, maxchar;
  unsigned char *buffer, thechar;
  XCharStruct   *xcs;
  
  fontheight = fontwidth = 0;
  minchar = tfont->min_char_or_byte2;
  maxchar = tfont->max_char_or_byte2;

  for (format = 0; format < nformats; ++format) {
    switch (formatv[format]) {
    case FormatHome: case FormatCR: case FormatNL: case FormatMeasureText:
      /* nothing to do */
      break;
    case FormatString: case FormatStringCenter: case FormatMultiLine:
      for (buffer = (unsigned char *)formatv[++format];
	   thechar = *buffer;
	   buffer++) {
	if ((thechar != '\n') && (thechar != '\t') &&
	    (minchar <= thechar) && (thechar <= maxchar)) {
	  xcs = &(tfont->per_char[thechar]);
	  if (fontwidth < (width = xcs->width))
	    fontwidth = width;
	  if (fontheight < (height = (xcs->ascent + xcs->descent)))
	    fontheight = height;
	} /* END if thechar */
      } /* END for buffer */
      break;
    default: /* most format instructions need to increment the counter */
      format++;
      break;
    } /* END switch */
  } /* END for */

  if (pwidth)  *pwidth  = fontwidth;
  if (pheight) *pheight = fontheight;

} /* END formatMeasureText */
