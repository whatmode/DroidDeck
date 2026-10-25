/* glmem: GL textures and framebuffers as a UI makes them (the driver's Zink, through the bridge).
 * Makes textures, draws into them through framebuffers, samples one into the window, and reads
 * the pixels back; the server's log then says which memory they came from.
 * Prints "glmem:" lines; exits 0 when every read-back pixel is right. */
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*GenFb)(GLsizei, GLuint *);
typedef void (*BindFb)(GLenum, GLuint);
typedef void (*FbTex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*FbStatus)(GLenum);
typedef void (*DelFb)(GLsizei, const GLuint *);

int main(void) {
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) { printf("glmem: FAIL no display\n"); return 1; }
  int attrs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
  XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attrs);
  if (!vi) { printf("glmem: FAIL no visual\n"); return 1; }
  XSetWindowAttributes swa = {0};
  swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
  Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 256, 256, 0, vi->depth, InputOutput, vi->visual,
                             CWColormap, &swa);
  XMapWindow(dpy, win);
  GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
  glXMakeCurrent(dpy, win, ctx);
  printf("glmem: renderer %s\n", (const char *)glGetString(GL_RENDERER));
  GenFb genfb = (GenFb)glXGetProcAddress((const GLubyte *)"glGenFramebuffers");
  BindFb bindfb = (BindFb)glXGetProcAddress((const GLubyte *)"glBindFramebuffer");
  FbTex fbtex = (FbTex)glXGetProcAddress((const GLubyte *)"glFramebufferTexture2D");
  FbStatus fbstatus = (FbStatus)glXGetProcAddress((const GLubyte *)"glCheckFramebufferStatus");
  DelFb delfb = (DelFb)glXGetProcAddress((const GLubyte *)"glDeleteFramebuffers");
  if (!genfb || !bindfb || !fbtex || !fbstatus || !delfb) { printf("glmem: FAIL no framebuffer objects\n"); return 1; }

  unsigned bad = 0, checked = 0;
  for (int round = 0; round < 30; round++) {
    GLuint tex[8], fb;
    glGenTextures(8, tex);
    genfb(1, &fb);
    for (int i = 0; i < 8; i++) {
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256 + 32 * i, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      bindfb(GL_FRAMEBUFFER, fb);
      fbtex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[i], 0);
      if (fbstatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("glmem: FAIL framebuffer incomplete\n"); return 1; }
      glViewport(0, 0, 256 + 32 * i, 256);
      glClearColor(0.0f, (float)i / 7.0f, 1.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      unsigned char px[4 * 16];
      glReadPixels(100, 100, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, px);
      int want = (i * 255 + 3) / 7;
      for (int p = 0; p < 16; p++) {
        checked++;
        if (px[4 * p] > 1 || abs(px[4 * p + 1] - want) > 2 || px[4 * p + 2] < 254) bad++;
      }
    }
    /* the last texture shown in the window */
    bindfb(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 256, 256);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex[7]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    unsigned char c[4];
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
    checked++;
    if (c[0] > 1 || c[1] < 253 || c[2] < 254) bad++;
    glFlush();
    delfb(1, &fb);
    glDeleteTextures(8, tex);
    usleep(50000);
  }
  glFinish();
  printf("glmem: %s textures drawn and read back (%u of %u pixels wrong)\n", bad ? "FAIL" : "PASS", bad, checked);
  glXMakeCurrent(dpy, None, NULL);
  glXDestroyContext(dpy, ctx);
  XCloseDisplay(dpy);
  return bad ? 1 : 0;
}
