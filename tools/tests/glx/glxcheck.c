/* glxcheck: what Steam's client does with GLX at startup, as one test program.
 *
 *   glxcheck [--steam-env]
 *
 * --steam-env first sets LIBGL_KOPPER_DISABLE=true in its own process, as the updated Steam
 * client does ("setting LIBGL_KOPPER_DISABLE=true for steamwebhelper ...") before its vgui2
 * surface calls glXChooseVisual. Then: glXChooseVisual with the attribute sets an old-style GL
 * client asks for, a context on the first visual found, a window, a clear and a swap.
 * Prints one "glxcheck:" line per step; exits 0 only when a visual was found and a frame drawn. */
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--steam-env")) setenv("LIBGL_KOPPER_DISABLE", "true", 1);
  printf("glxcheck: LIBGL_KOPPER_DISABLE reads as %s\n",
         getenv("LIBGL_KOPPER_DISABLE") ? getenv("LIBGL_KOPPER_DISABLE") : "(unset)");
  Display *x = XOpenDisplay(NULL);
  if (!x) { printf("glxcheck: no X display\n"); return 2; }
  int scr = DefaultScreen(x);
  static int sets[][16] = {
      {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, None},
      {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, None},
      {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 24, None},
      {GLX_RGBA, GLX_DOUBLEBUFFER, None},
      {GLX_RGBA, None},
  };
  const char *names[] = {"rgb888 depth24 stencil8 double", "rgba8888 double", "rgba depth24 double", "rgba double", "rgba"};
  XVisualInfo *vi = NULL;
  int found = 0;
  for (unsigned i = 0; i < sizeof(sets) / sizeof(sets[0]); i++) {
    XVisualInfo *v = glXChooseVisual(x, scr, sets[i]);
    printf("glxcheck: glXChooseVisual(%s): %s", names[i], v ? "visual" : "NULL (glXChooseVisual failed)");
    if (v) printf(" 0x%lx depth %d", v->visualid, v->depth);
    printf("\n");
    if (v) { found++; if (!vi) vi = v; else XFree(v); }
  }
  if (!vi) { printf("glxcheck: RESULT no visual: Steam's client would exit here\n"); return 1; }
  GLXContext ctx = glXCreateContext(x, vi, NULL, True);
  if (!ctx) { printf("glxcheck: RESULT glXCreateContext failed\n"); return 1; }
  XSetWindowAttributes swa = {0};
  swa.colormap = XCreateColormap(x, RootWindow(x, scr), vi->visual, AllocNone);
  Window w = XCreateWindow(x, RootWindow(x, scr), 0, 0, 64, 64, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
  XMapWindow(x, w);
  XSync(x, False);
  if (!glXMakeCurrent(x, w, ctx)) { printf("glxcheck: RESULT glXMakeCurrent failed\n"); return 1; }
  printf("glxcheck: renderer %s, version %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
  glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glXSwapBuffers(x, w);
  glFinish();
  unsigned char px[4] = {0};
  glReadBuffer(GL_FRONT);
  glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  printf("glxcheck: front pixel %u %u %u, GL error 0x%x\n", px[0], px[1], px[2], glGetError());
  glXMakeCurrent(x, None, NULL);
  glXDestroyContext(x, ctx);
  XDestroyWindow(x, w);
  XCloseDisplay(x);
  printf("glxcheck: RESULT ok, %d of 5 attribute sets matched, a frame drawn\n", found);
  return 0;
}
