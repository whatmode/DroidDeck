/* swapint: a GL window that switches its swap interval between frames, the way Steam's GL
 * compositor does. Prints "swapint: done" after 12 frames. */
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
int main(void) {
  Display *x = XOpenDisplay(NULL);
  if (!x) return 2;
  int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
  XVisualInfo *vi = glXChooseVisual(x, DefaultScreen(x), attr);
  if (!vi) { printf("swapint: no visual\n"); return 1; }
  XSetWindowAttributes swa = {0};
  swa.colormap = XCreateColormap(x, RootWindow(x, vi->screen), vi->visual, AllocNone);
  Window w = XCreateWindow(x, RootWindow(x, vi->screen), 0, 0, 128, 128, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
  XMapWindow(x, w);
  XSync(x, False);
  GLXContext c = glXCreateContext(x, vi, NULL, True);
  glXMakeCurrent(x, w, c);
  void (*swap_ext)(Display *, GLXDrawable, int) = (void (*)(Display *, GLXDrawable, int))glXGetProcAddress((const GLubyte *)"glXSwapIntervalEXT");
  int (*swap_mesa)(unsigned) = (int (*)(unsigned))glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA");
  for (int i = 0; i < 12; i++) {
    if (swap_ext) swap_ext(x, w, i & 1); else if (swap_mesa) swap_mesa(i & 1);
    glClearColor(i & 1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(x, w);
  }
  glFinish();
  printf("swapint: done (%s)\n", swap_ext ? "glXSwapIntervalEXT" : swap_mesa ? "glXSwapIntervalMESA" : "no swap interval call");
  return 0;
}
