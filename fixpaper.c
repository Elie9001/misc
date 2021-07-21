/***
 You know when people send photos of mail, documents, etc.
 This is a simple app to clean up the picture and make it printable.
 Version 1.0
 --

 To compile this:
    gcc fixpaper.c -o fixpaper -ffast-math -Ofast -lGL -lglut -lm -std=gnu99


 Copyright 2021, Elie Goldman Smith

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

#include <GL/gl.h>
#include <GL/glut.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#define STB_IMAGE_IMPLEMENTATION
#include "aux/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "aux/stb_image_write.h"

typedef struct {
 float x;
 float y;
} vec2;

#define isFloatNonzeroEnough(f) ((f) < -0.000001f || 0.000001f < (f))

#define DEFAULT_WINDOW_WIDTH  640
#define DEFAULT_WINDOW_HEIGHT 360
int _viewport_x = DEFAULT_WINDOW_WIDTH;
int _viewport_y = DEFAULT_WINDOW_HEIGHT;

vec2 crop_points[4]; // IPC
int selected_crop_point = -1;
vec2 crop_aspect;
float wpc2ipc_scale = 1.0f;

GLuint tex;
int image_width, image_height;
unsigned char *image_data;
const char* input_filename=NULL;
#define              OUTPUT_FILENAME_MAX_CHARS 80
char output_filename[OUTPUT_FILENAME_MAX_CHARS];

int save_and_quit=0;
int finished_everything=0;


void update_output_filename() {
	time_t t; time(&t);
	strftime(output_filename, OUTPUT_FILENAME_MAX_CHARS, "paper-%F-%T.png", localtime(&t));
}


void recalc_crop_aspect() {
 crop_aspect.x = sqrtf(0.5f*( (crop_points[0].x - crop_points[1].x) * (crop_points[0].x - crop_points[1].x)
                            + (crop_points[0].y - crop_points[1].y) * (crop_points[0].y - crop_points[1].y)
                            + (crop_points[2].x - crop_points[3].x) * (crop_points[2].x - crop_points[3].x)
                            + (crop_points[2].y - crop_points[3].y) * (crop_points[2].y - crop_points[3].y)));
 crop_aspect.y = sqrtf(0.5f*( (crop_points[0].x - crop_points[3].x) * (crop_points[0].x - crop_points[3].x)
                            + (crop_points[0].y - crop_points[3].y) * (crop_points[0].y - crop_points[3].y)
                            + (crop_points[2].x - crop_points[1].x) * (crop_points[2].x - crop_points[1].x)
                            + (crop_points[2].y - crop_points[1].y) * (crop_points[2].y - crop_points[1].y)));
}

void reset_crop_points() {
 crop_points[0].x = 0.0f;         crop_points[0].y = image_height;
 crop_points[1].x = image_width;  crop_points[1].y = image_height;
 crop_points[2].x = image_width;  crop_points[2].y = 0.0f;
 crop_points[3].x = 0.0f;         crop_points[3].y = 0.0f;
}


/* Conversion functions for different coordinate systems:
    WPC - window pixel coords
    IPC - image pixel coords
    TXC - texture coords (OpenGL)
    NDC - normalized device coords (OpenGL)
*/
vec2 wpc2ipc(vec2 coord) {
 coord.x -= _viewport_x/4;
 coord.y -= _viewport_y/2;
 coord.x *= wpc2ipc_scale;
 coord.y *= wpc2ipc_scale;
 coord.x += image_width/2;
 coord.y += image_height/2;
 return coord;
}
vec2 ipc2wpc(vec2 coord) {
 coord.x -= image_width/2;
 coord.y -= image_height/2;
 coord.x /= wpc2ipc_scale;
 coord.y /= wpc2ipc_scale;
 coord.x += _viewport_x/4;
 coord.y += _viewport_y/2;
 return coord; 
}
vec2 wpc2ndc(vec2 coord) {
 coord.x = coord.x *  2.0f/_viewport_x - 1.0f;
 coord.y = coord.y * -2.0f/_viewport_y + 1.0f;
 return coord;
}
vec2 ipc2txc(vec2 coord) {
 coord.x /= image_width;
 coord.y /= image_height;
 return coord;
}





void textGL(const char *str, float line) {
 glRasterPos2f(-9.f *  strlen(str)   / _viewport_x,
              -15.f * (line*2.f+1.f) / _viewport_y);
 while (*str) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *(str++));
}

void flush() {
 glutSwapBuffers();
 glClear(GL_COLOR_BUFFER_BIT);
 glColor3f(1.0f, 1.0f, 1.0f);
}




void blur_1d(const float *in, float *out, int line_size, int blur_size, int stride) // for horizonal blur, stride should be 1. For vertical blur, stride should be the width of the image.
{
 if (blur_size > line_size/8) blur_size = line_size/8;
 if (blur_size < 1) blur_size = 1;
 float norm = 0.5f / blur_size;
 float val=0;
 int i;
 for (i=0; i<blur_size; i++) val += in[i*stride];
 for (i=0; i<blur_size; i++) {
  val += in[(i+blur_size)*stride]; 
  out[i*stride] = val * norm; // val * norm * 2.0f*(1.0f-i*norm);
 }
 for (   ; i<line_size-blur_size; i++) {
  val += in[(i+blur_size)*stride];
  val -= in[(i-blur_size)*stride];
  out[i*stride] = val * norm;
 }
 for (   ; i<line_size; i++) {
  val -= in[(i-blur_size)*stride];
  out[i*stride] = val * norm; // val * norm * 2.0f*(1.0f-(line_size-i)*norm);
 }
}





void init() {
 glEnable(GL_TEXTURE_2D);
 glGenTextures(1, &tex);
 glActiveTexture(GL_TEXTURE0);
 glBindTexture(GL_TEXTURE_2D, tex);
 printf("Loading %s...\n", input_filename);
 textGL("Loading image...",0);  flush();
 int nChannels;
 image_data = stbi_load(input_filename, &image_width, &image_height, &nChannels, 0);
 if (image_data) {
  printf("Input resolution: %d x %d pixels\n", image_width, image_height);
  printf("Pre-processing the image");     fflush(stdin);
  textGL("Pre-processing the image...",0); flush();
  int size = image_width * image_height;
  int local_range = 256; // this is the approximate radius (in pixels) for the brightness/contrast auto-adjustments in pre-processing. XXX: Its value shouldn't be hard-coded like this, but where should the user control it instead?
  
  // set up 3 temporary buffers which will be needed for pre-processing
  float *buf1 = malloc(size*sizeof(float)); //
  float *buf2 = malloc(size*sizeof(float)); // TODO: error checking in case malloc failed
  float *buf3 = malloc(size*sizeof(float)); //

  // load image data into buf1 (convert to greyscale, 0.0 = middle grey)
  if (nChannels == 3) {
   for (int i=0; i<size; i++) buf1[i] = 0.299f*(image_data[i*3]   - 127.5f)
                                      + 0.587f*(image_data[i*3+1] - 127.5f)
                                      + 0.114f*(image_data[i*3+2] - 127.5f);
  }
  else {
   for (int i=0; i<size; i++) buf1[i] = image_data[i*nChannels] - 127.5f;
  }
  // and we don't need the original image data anymore
  stbi_image_free(image_data);
 
  // buf2 := horizontally blurred buf1
  for (int i=0; i<size; i+=image_width) blur_1d(&buf1[i], &buf2[i], image_width, local_range, 1);
  putchar('.'); fflush(stdout);
  
  // buf3 := vertically blurred buf2
  // buf3 becomes a "local average brightness" map.
  for (int i=0; i<image_width; i++) blur_1d(&buf2[i], &buf3[i], image_height, local_range, image_width);
  putchar('.'); fflush(stdout);
  
  // buf1 -= buf3
  // buf1 becomes a "brightness-corrected image". RGB values have a local average of 0.
  for (int i=0; i<size; i++) buf1[i] = buf1[i] - buf3[i];
  putchar('.'); fflush(stdout);
  
  // buf2 := buf1 values squared
  for (int i=0; i<size; i++) buf2[i] = buf1[i] * buf1[i];
  putchar('.'); fflush(stdout);
  
  // buf3 := horizontally blurred buf2
  for (int i=0; i<size; i+=image_width) blur_1d(&buf2[i], &buf3[i], image_width, local_range, 1);
  putchar('.'); fflush(stdout);
  
  // buf2 := vertically blurred buf3
  for (int i=0; i<image_width; i++) blur_1d(&buf3[i], &buf2[i], image_height, local_range, image_width);
  putchar('.'); fflush(stdout);
  
  // buf2 := inverse square root buf2
  // buf2 becomes a "reciprocal of the local standard deviation" map.
  for (int i=0; i<size; i++) buf2[i] = 1.0f / sqrtf(buf2[i]);
  putchar('.'); fflush(stdout);
  
  // buf1 *= buf2
  // buf1 becomes a "contrast-normalized image". RGB values have a local standard deviation of 1.
  for (int i=0; i<size; i++) buf1[i] *= buf2[i];
  putchar('.'); fflush(stdout);

  // buf1: tweak the contrast a bit more, and shift everything by 1 so an 'average' pixel will appear white
  for (int i=0; i<size; i++) buf1[i] = buf1[i] * 0.5f + 1.0f;
  putchar('.'); fflush(stdout);

  // send buf1 to the graphics card, as a texture
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, image_width, image_height, 0, GL_RED, GL_FLOAT, buf1); // XXX: how to handle the case where dimensions exceed GL_MAX_TEXTURE_SIZE?

  // we don't need the temporary buffers anymore
  free(buf1);
  free(buf2);
  free(buf3);
  
  // more OpenGL stuff
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  reset_crop_points();
  recalc_crop_aspect();
  printf("\nDone.\n");
 }
 else printf("Failed.\n");
}





void draw()
{
 if (!image_data) {
  textGL("Input file doesn't exist, or is not an image.",0);  flush();
  return;
 }
 if (finished_everything) {
  textGL("Saved to file:",               -1);
  textGL(output_filename ,                0);
  textGL("You can close this window now.",2);
  flush();
  return;
 }

 vec2 a = ipc2txc(crop_points[0]);
 vec2 b = ipc2txc(crop_points[1]);
 vec2 c = ipc2txc(crop_points[2]);
 vec2 d = ipc2txc(crop_points[3]);
 // The crop area is a quadrilateral shape defined by the 4 points a,b,c,d.
 // We have to interperet this shape as being a rectangle in perspective.
 // So first we calculate the "vanishing points":
 // let p be the intersection point of the 2 lines defined by (a,b) and (c,d)
 // let q be the intersection point of the 2 lines defined by (a,d) and (c,b)
 // There's a long formula for these points, and it's a fraction.
 // We calculate the denominators first, so we can handle special cases where the denominator is 0 (parallel lines).
 vec2 p,q;
 p.x = a.x*c.y - a.x*d.y - b.x*c.y + b.x*d.y - c.x*a.y + c.x*b.y + d.x*a.y - d.x*b.y;
 p.y = a.y*c.x - a.y*d.x - b.y*c.x + b.y*d.x - c.y*a.x + c.y*b.x + d.y*a.x - d.y*b.x;
 q.x = a.x*c.y - a.x*b.y - d.x*c.y + d.x*b.y - c.x*a.y + c.x*d.y + b.x*a.y - b.x*d.y;
 q.y = a.y*c.x - a.y*b.x - d.y*c.x + d.y*b.x - c.y*a.x + c.y*d.x + b.y*a.x - b.y*d.x;
 float aFactor, bFactor, cFactor, dFactor; // These factors will tell OpenGL how to interpolate texture coordinates in proper perspective.
 if (isFloatNonzeroEnough(p.x) && isFloatNonzeroEnough(p.y)) {
  if (isFloatNonzeroEnough(q.x) && isFloatNonzeroEnough(q.y)) {
   // The crop area has no parallel lines.
   // So we finish calculating the vanishing points:
   p.x = (a.x*c.x*b.y - a.x*c.x*d.y - a.x*d.x*b.y + a.x*d.x*c.y - b.x*c.x*a.y + b.x*c.x*d.y + b.x*d.x*a.y - b.x*d.x*c.y) / p.x;
   p.y = (a.y*c.y*b.x - a.y*c.y*d.x - a.y*d.y*b.x + a.y*d.y*c.x - b.y*c.y*a.x + b.y*c.y*d.x + b.y*d.y*a.x - b.y*d.y*c.x) / p.y;
   q.x = (a.x*c.x*d.y - a.x*c.x*b.y - a.x*b.x*d.y + a.x*b.x*c.y - d.x*c.x*a.y + d.x*c.x*b.y + d.x*b.x*a.y - d.x*b.x*c.y) / q.x;
   q.y = (a.y*c.y*d.x - a.y*c.y*b.x - a.y*b.y*d.x + a.y*b.y*c.x - d.y*c.y*a.x + d.y*c.y*b.x + d.y*b.y*a.x - d.y*b.y*c.x) / q.y;
   // Next, consider the "horizon" line which runs between the two vanishing points.
   // Then for each point a,b,c,d, we calculate its distance from the horizon.
   // The reciprocal of that, will be the 'factor' we give to OpenGL.
   aFactor = 1.0f/(q.x*a.y + a.x*p.y + p.x*q.y - q.x*p.y - p.x*a.y - a.x*q.y);
   bFactor = 1.0f/(q.x*b.y + b.x*p.y + p.x*q.y - q.x*p.y - p.x*b.y - b.x*q.y);
   cFactor = 1.0f/(q.x*c.y + c.x*p.y + p.x*q.y - q.x*p.y - p.x*c.y - c.x*q.y);
   dFactor = 1.0f/(q.x*d.y + d.x*p.y + p.x*q.y - q.x*p.y - p.x*d.y - d.x*q.y);
  }
  else {
   // The crop area is a trapezoid:
   // lines AD and CB are parallel.
   if (fabs(a.x-d.x) > fabs(a.y-d.y)) {
    aFactor = dFactor = 1.0f / (a.x - d.x);
    cFactor = bFactor = 1.0f / (b.x - c.x);
   } else {
    aFactor = dFactor = 1.0f / (a.y - d.y);
    cFactor = bFactor = 1.0f / (b.y - c.y);
   }
  }
 }
 else if (isFloatNonzeroEnough(q.x) && isFloatNonzeroEnough(q.y)) {
  // The crop area is a trapezoid:
  // lines AB and CD are parallel.
  if (fabs(a.x-b.x) > fabs(a.y-b.y)) {
   aFactor = bFactor = 1.0f / (a.x - b.x);
   cFactor = dFactor = 1.0f / (d.x - c.x);
  } else {
   aFactor = bFactor = 1.0f / (a.y - b.y);
   cFactor = dFactor = 1.0f / (d.y - c.y);
  }
 }
 else {
  // The crop area is a parallellogram, possibly a rectangle.
  // So there's no perspective interpolation - we set all the 'factors' to 1.
  aFactor = bFactor = cFactor = dFactor = 1.0f;
 }


 if (save_and_quit)
 {
  printf("Saving...\n");
  textGL("Saving...",0); flush();
  
  GLint width = crop_aspect.x+0.5f;
  GLint height = crop_aspect.y+0.5f; // XXX: how to handle the case where dimensions exceed GL_MAX_VIEWPORT_DIMS?
  GLuint fb; // frame buffer
  GLuint rb; // render buffer

  // set up an offscreen buffer, and use it
  glGenFramebuffersEXT(1, &fb);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
  glGenRenderbuffersEXT(1, &rb);
  glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
  glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, width, height);
  glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
  if (status != GL_FRAMEBUFFER_COMPLETE_EXT) { /* fail */ }
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
  glViewport(0, 0, width, height);

  // render the frame
  glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS);
  glTexCoord4f(d.x*dFactor, d.y*dFactor, 0.0f, dFactor);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord4f(c.x*cFactor, c.y*cFactor, 0.0f, cFactor);
  glVertex2f( 1.0f, -1.0f);
  glTexCoord4f(b.x*bFactor, b.y*bFactor, 0.0f, bFactor);
  glVertex2f( 1.0f,  1.0f);
  glTexCoord4f(a.x*aFactor, a.y*aFactor, 0.0f, aFactor);
  glVertex2f(-1.0f,  1.0f);
  glEnd();
  glDisable(GL_TEXTURE_2D);

  // capture the pixels that were rendered
  unsigned char *data = malloc(width * height);
  if (!data) { /* TODO: handle error */ }
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0,0, width, height, GL_RED, GL_UNSIGNED_BYTE, data);

  // delete the offscreen buffer, reset openGL to using the default buffers
  glDeleteRenderbuffersEXT(1, &rb);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); // unbind
  glDeleteFramebuffersEXT(1, &fb);
  glViewport(0, 0, (GLint)_viewport_x, (GLint)_viewport_y);
  glClear(GL_COLOR_BUFFER_BIT);

  // write the captured pixels to a file
  update_output_filename();
  stbi_write_png(output_filename, width, height, 1, data, width);

  free(data);
  printf("Saved to %s\n", output_filename);
  printf("Output resolution: %d x %d pixels\n", width, height);
  finished_everything = 1;
  draw();
  return;
 }


 float inv_vx = 1.0f / _viewport_x;
 float inv_vy = 1.0f / _viewport_y;
 glClear(GL_COLOR_BUFFER_BIT);

 // left pane
 vec2 aspect; aspect.x = image_width * inv_vx*2.0f;
              aspect.y = image_height * inv_vy;
 if (aspect.x > aspect.y) {
  float ratio = aspect.x / aspect.y;
  glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 0.5f+0.5f*ratio);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(1.0f, 0.5f+0.5f*ratio);
  glVertex2f( 0.0f, -1.0f);
  glTexCoord2f(1.0f, 0.5f-0.5f*ratio);
  glVertex2f( 0.0f,  1.0f);
  glTexCoord2f(0.0f, 0.5f-0.5f*ratio);
  glVertex2f(-1.0f,  1.0f);
  glEnd();
 }
 else {
  float ratio = 0.5f * aspect.y / aspect.x;
  glEnable(GL_TEXTURE_2D);
  glBegin(GL_QUADS);
  glTexCoord2f(0.5f-ratio, 1.0f);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(0.5f+ratio, 1.0f);
  glVertex2f( 0.0f, -1.0f);
  glTexCoord2f(0.5f+ratio, 0.0f);
  glVertex2f( 0.0f,  1.0f);
  glTexCoord2f(0.5f-ratio, 0.0f);
  glVertex2f(-1.0f,  1.0f);
  glEnd();
 }

 // right pane 
 aspect = crop_aspect;
 aspect.x *= inv_vx*2.0f;
 aspect.y *= inv_vy;
 if (aspect.x > aspect.y) {
  float ratio = aspect.y / aspect.x;
  glBegin(GL_QUADS);
  glTexCoord4f(a.x*aFactor, a.y*aFactor, 0.0f, aFactor);
  glVertex2f(0.0f, -ratio);
  glTexCoord4f(b.x*bFactor, b.y*bFactor, 0.0f, bFactor);
  glVertex2f(1.0f, -ratio);
  glTexCoord4f(c.x*cFactor, c.y*cFactor, 0.0f, cFactor);
  glVertex2f(1.0f,  ratio);
  glTexCoord4f(d.x*dFactor, d.y*dFactor, 0.0f, dFactor);
  glVertex2f(0.0f,  ratio);
  glEnd();
 }
 else {
  float ratio = 0.5f * aspect.x / aspect.y;
  glBegin(GL_QUADS);
  glTexCoord4f(a.x*aFactor, a.y*aFactor, 0.0f, aFactor);
  glVertex2f(0.5f-ratio, -1.0f);
  glTexCoord4f(b.x*bFactor, b.y*bFactor, 0.0f, bFactor);
  glVertex2f(0.5f+ratio, -1.0f);
  glTexCoord4f(c.x*cFactor, c.y*cFactor, 0.0f, cFactor);
  glVertex2f(0.5f+ratio,  1.0f);
  glTexCoord4f(d.x*dFactor, d.y*dFactor, 0.0f, dFactor);
  glVertex2f(0.5f-ratio,  1.0f);
  glEnd();
 }

 // cropping indicator
 glDisable(GL_TEXTURE_2D);
 a = wpc2ndc(ipc2wpc(crop_points[0]));
 b = wpc2ndc(ipc2wpc(crop_points[1]));
 c = wpc2ndc(ipc2wpc(crop_points[2]));
 d = wpc2ndc(ipc2wpc(crop_points[3]));
 glColor3f(0.0f, 0.4f, 0.0f);
 glRasterPos2f(a.x-20.f/_viewport_x, a.y-13.f/_viewport_y); glutBitmapCharacter(GLUT_BITMAP_8_BY_13, '4');
 glRasterPos2f(b.x +4.f/_viewport_x, b.y-13.f/_viewport_y); glutBitmapCharacter(GLUT_BITMAP_8_BY_13, '3');
 glRasterPos2f(c.x +4.f/_viewport_x, c.y);                  glutBitmapCharacter(GLUT_BITMAP_8_BY_13, '2');
 glRasterPos2f(d.x-20.f/_viewport_x, d.y);                  glutBitmapCharacter(GLUT_BITMAP_8_BY_13, '1');
 glLineWidth(6.0f);
 glBegin(GL_LINE_LOOP);
 glVertex2f(a.x, a.y);
 glVertex2f(b.x, b.y);
 glVertex2f(c.x, c.y);
 glVertex2f(d.x, d.y);
 glEnd();
 glLineWidth(2.0f);
 glColor3f(0.0f, 1.0f, 0.0f);
 glBegin(GL_LINE_LOOP);
 glVertex2f(a.x, a.y);
 glVertex2f(b.x, b.y);
 glVertex2f(c.x, c.y);
 glVertex2f(d.x, d.y);
 glEnd();

 // ready
 flush();
}




void done() {
 glDeleteTextures(1, &tex);
}









void reshape_window(int width, int height) {
 glViewport(0, 0, (GLint) width, (GLint) height);
 _viewport_x = width;
 _viewport_y = height;
 float scale1 = (float)image_width / (width/2);
 float scale2 = (float)image_height / height;
 wpc2ipc_scale = scale1 > scale2 ? scale1 : scale2;
}


void mouse_motion(int x, int y) {
 static int last_x = -1;  if (last_x == -1) last_x = x;
 static int last_y = -1;  if (last_y == -1) last_y = y;
 if (selected_crop_point >= 0) {
  vec2 v; v.x=x; v.y=y;
  crop_points[selected_crop_point] = wpc2ipc(v);
  recalc_crop_aspect();
  draw();
 }
 else if (selected_crop_point == -16) {
  float dx = (x - last_x) * wpc2ipc_scale;
  float dy = (y - last_y) * wpc2ipc_scale;
  last_x = x;
  last_y = y;
  for (int i=0; i<4; i++) {
   crop_points[i].x += dx;
   crop_points[i].y += dy;   
  }
  draw();
 }
 last_x = x;
 last_y = y;
}


void mouse_func(int button, int state, int x, int y) {
 if (state==GLUT_DOWN) {
  if (button==GLUT_LEFT_BUTTON) {
   vec2 v; v.x=x; v.y=y;
   v = wpc2ipc(v);
   float dist=1e12;
   for (int i=0; i<4; i++) {
    float d = (v.x - crop_points[i].x) * (v.x - crop_points[i].x)
            + (v.y - crop_points[i].y) * (v.y - crop_points[i].y);
    if (d < dist) {
     dist = d;
     selected_crop_point = i;
    }
   }
  }
  else if (button==GLUT_RIGHT_BUTTON) selected_crop_point = -16;
 }
 else selected_crop_point = -1;
 mouse_motion(x,y);
}


void key_down(unsigned char key, int x, int y) {
 vec2 v;
 switch(key) {
  case '1':                   selected_crop_point=3; mouse_motion(x,y); break;
  case '2':                   selected_crop_point=2; mouse_motion(x,y); break;
  case '3':case 'w':case 'W': selected_crop_point=1; mouse_motion(x,y); break;
  case '4':case 'q':case 'Q': selected_crop_point=0; mouse_motion(x,y); break; // on a QWERTY keyboard, pressing 1,2,W,Q feels more natural than 1,2,3,4
  case '<':case ',':
   v = crop_points[3];
   crop_points[3] = crop_points[2];
   crop_points[2] = crop_points[1];
   crop_points[1] = crop_points[0];
   crop_points[0] = v;
   recalc_crop_aspect(); draw();
  break;
  case '>':case '.':
   v = crop_points[0];
   crop_points[0] = crop_points[1];
   crop_points[1] = crop_points[2];
   crop_points[2] = crop_points[3];
   crop_points[3] = v;
   recalc_crop_aspect(); draw();
  break;
  case '\b': reset_crop_points(); recalc_crop_aspect(); draw(); break;
  case '\r': save_and_quit = 1; recalc_crop_aspect(); draw(); break;
  case 27: exit(0); break;
 }
}
void key_up(unsigned char key, int x, int y) {
 selected_crop_point = -1;
}





int main(int argc, char **argv)
{
 if (argc != 2) {
  update_output_filename();
  printf("This program is for enhancing photos of papers, to make them printable.\nIt auto-adjusts contrast and allows you to crop in perspective.\n\nUsage: %s <input image file name>\n\nOutput filename will be automatically generated,\nfor example '%s'\n", argv[0], output_filename);
  return 1;
 }
 input_filename = argv[1];
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
	glutInit(&argc, argv);
	glutCreateWindow("Clean up a photo of a paper");
 glutReshapeFunc(reshape_window);
 glutMouseFunc(mouse_func);
 glutMotionFunc(mouse_motion);
 glutPassiveMotionFunc(mouse_motion);
 glutKeyboardFunc(key_down);
 glutKeyboardUpFunc(key_up);
 glutDisplayFunc(draw);
 init();
 atexit(done);
	glutMainLoop();
	return 0;
}
