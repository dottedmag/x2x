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
#ifndef _FORMAT_H_
#define _FORMAT_H_

#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#endif

void formatText();

typedef XFontStruct XFS, *PXFS;

typedef long Format;

#define FormatSetTop	        1  /* set top margin */
#define FormatSetLeft	        2  /* set left margin */
#define FormatHome	        3  /* home coordinates */
#define FormatCR	        4  /* carriage return */
#define FormatNL	        5  /* new line */
#define FormatSetY	        6  /* set absolute Y coordinate */
#define FormatSetTextY	        7  /* set Y coordinate to line */
#define FormatAddY	        8  /* add absolute Y */
#define FormatAddTextY	        9  /* add lines to Y */
#define FormatAddHalfTextY     10  /* add half lines to Y */
#define FormatSetX	       11  /* set X absolute */
#define FormatSetTextX	       12  /* set X to character */
#define FormatAddX	       13  /* add absolute X */
#define FormatAddTextX	       14  /* add characters to X */
#define FormatAddHalfTextX     15  /* add characters to X */
#define FormatString	       16  /* draw string and advance X */
#define FormatStringCenter     17  /* draw string, centered on current width */
#define FormatMultiLine        18  /* draw line string with newlines */
#define FormatMeasureText      19  /* get font measurements based on strings */
#define FormatSetWidth         20  /* set width for centering */
#define FormatSetLMargin       21  /* set left margin.  -1 = current x */
#define FormatSetRMargin       22  /* set right margin. -1 = current x */
#endif /* _FORMAT_H_ */
