/***
 Simple night-time filter for X11
 Removes blue light from the screen, so you can sleep better.
 Version 1.0

 I created this because I wanted a filter that works without gnome or compiz... and I wanted the best color rendering. Xgamma couldn't do it right, so I made my own.
 --

 To compile:
     gcc xnightfilter.c -O -o xnightfilter -lX11 -lXxf86vm


 Copyright 2020, Elie Goldman Smith

 This program is FREE SOFTWARE: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
***/
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/xf86vmode.h>
typedef uint16_t ramp_t;
ramp_t *r_ramp = NULL, *g_ramp = NULL, *b_ramp = NULL;
int ramp_size = 0;

#define STATE_FILENAME "/tmp/xnightfilter-state"


void none() {
 double factor = 65535.0 / (ramp_size-1);
 for (int i=0; i < ramp_size; i++) r_ramp[i] = g_ramp[i] = b_ramp[i] = i * factor + 0.5;
}

void basic() {
 double rFactor = 65535.0 / (ramp_size-1); // full max red
 double gFactor = 49152.0 / (ramp_size-1); // 3/4 max green
 double bFactor = 1.0 / (ramp_size-1);     // 1/3 max blue, but curved below
 for (int i=0; i < ramp_size; i++) {
  r_ramp[i] = i * rFactor            + 0.5; //
  g_ramp[i] = i * gFactor            + 0.5; // the 0.5 is for rounding
  double x = 1.0 - i * bFactor;
  b_ramp[i] = 21845.0*(1.0 - x*x)    + 0.5; //
 }
 puts("* basic night filter");
}

void fluorescent() {
 double factor = 1.0 / (ramp_size-1);
 for (int i=0; i < ramp_size; i++) {
  double x = i * factor;   // polynomial ramps:
  r_ramp[i] = 65535.0*(0.5*x*x*x - 1.5*x*x + 2.0*x)  + 0.5;
  g_ramp[i] = 65535.0*(    x*x*x - 0.5*x*x + 0.5*x)  + 0.5;
  b_ramp[i] = 21845.0*(              - x*x + 2.0*x)  + 0.5;
 }
 puts("* fluorescent night filter");
}

void minty() {
 double factor = 1.0 / (ramp_size-1);
 for (int i=0; i < ramp_size; i++) {
  double x = i * factor;
  r_ramp[i] = b_ramp[i] = 32768.0*(0.5*x*x*x - 1.5*x*x + 2.0*x)  + 0.5;
  g_ramp[i] =             65535.0*x                              + 0.5;
 }
 puts("* minty green filter (just for fun)");
}

void blue() {
 double factor = 1.0 / (ramp_size-1);
 for (int i=0; i < ramp_size; i++) {
  double x = i * factor;
  r_ramp[i] = g_ramp[i] = 32768.0*(0.5*x*x*x - 1.5*x*x + 2.0*x)  + 0.5;
  b_ramp[i] =             65535.0*x                              + 0.5;
 }
 puts("* blue tint (not a night filter! don't use before bed!)");
}


void invert() {
 for (int i=0; i < ramp_size/2; i++) {
  ramp_t v = r_ramp[i];
  r_ramp[i] = r_ramp[ramp_size-1-i];
  r_ramp[ramp_size-1-i] = v;
  v = g_ramp[i];
  g_ramp[i] = g_ramp[ramp_size-1-i];
  g_ramp[ramp_size-1-i] = v;
  v = b_ramp[i];
  b_ramp[i] = b_ramp[ramp_size-1-i];
  b_ramp[ramp_size-1-i] = v;
 }
 puts("* inversion");
}



int main(int argc, char *argv[])
{
 Display *dpy = NULL;
 int screen = -1;
 char param = '-';

 if (argc >= 2) {
  param = argv[1][0];
  if (param < '0' || param > '9') {
   printf("\n\
This is a screen filter for X11.\n\
Usage:\n\
%s 0: No filter\n\
%s 1: Basic night filter\n\
%s 2: Invert colors + basic night filter\n\
%s 3: Invert colors only\n",argv[0],argv[0],argv[0],argv[0]);
   return 1;
  }
 }
 else {
  FILE *f = fopen(STATE_FILENAME, "r");
  if (f) {
   if (!fread(&param, 1, 1, f)) param = '0';
   if (++param > '3') param = '0';
   fclose(f);
  }
  else param = '1';
 }
 FILE *f = fopen(STATE_FILENAME, "w");
 if (f) {
  fwrite(&param, 1, 1, f);
  fclose(f);
 }


 if ((dpy = XOpenDisplay(NULL)) == NULL) puts("Cannot open display!");
 else screen = DefaultScreen(dpy);

 if (!XF86VidModeGetGammaRampSize(dpy, screen, &ramp_size)) {
  puts("Cannot get ramp size! Defaulting to 256");
  ramp_size = 256;
 }
 r_ramp = (ramp_t *) malloc(ramp_size * sizeof(ramp_t));
 g_ramp = (ramp_t *) malloc(ramp_size * sizeof(ramp_t));
 b_ramp = (ramp_t *) malloc(ramp_size * sizeof(ramp_t)); 
 if (!r_ramp || !g_ramp || !b_ramp) puts("Cannot allocate memory!");

 switch (param) {
  case '0': none(); puts("filter off"); break;
  case '1': basic(); break;
  case '2': basic(); invert(); break;
  case '3': none(); invert(); break;
  case '4': fluorescent(); break;
  case '5': minty(); break;
  case '6': blue(); break;
  case '7': blue(); invert(); break;
  case '8': minty(); invert(); break;
  case '9': fluorescent(); invert(); break;
 }

 if (!XF86VidModeSetGammaRamp(dpy, screen, ramp_size, r_ramp, g_ramp, b_ramp)) puts("Cannot set gamma ramps!");
 free(r_ramp);
 free(g_ramp);
 free(b_ramp);
 XCloseDisplay(dpy);
 return 0;
}