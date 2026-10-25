/* retire: Steam's UI pattern under gamescope. A main GL window presents every frame (Steam's
 * compositor); now and then a second GL window opens, presents a few frames and goes away
 * (Steam's notification toasts and overlay popups). Each time the main window's swapchain is
 * made again (Zink logs "droiddeck kopper: new swapchain"), the UI showed a flash.
 * Prints the main window's colour count and "retire: done". */
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Display *x;
static XVisualInfo *vi;

static Window make_window(int w, int h, int override) {
  XSetWindowAttributes swa = {0};
  swa.colormap = XCreateColormap(x, RootWindow(x, vi->screen), vi->visual, AllocNone);
  swa.override_redirect = override;
  Window win = XCreateWindow(x, RootWindow(x, vi->screen), override ? 100 : 0, override ? 100 : 0, w, h, 0, vi->depth,
                             InputOutput, vi->visual, CWColormap | CWOverrideRedirect, &swa);
  XMapWindow(x, win);
  XSync(x, False);
  return win;
}

static void frame(Window w, GLXContext c, float r, float g, float b) {
  glXMakeCurrent(x, w, c);
  glClearColor(r, g, b, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glXSwapBuffers(x, w);
}

int main(int argc, char **argv) {
  int rounds = argc > 1 ? atoi(argv[1]) : 4;
  x = XOpenDisplay(NULL);
  if (!x) { printf("retire: no display\n"); return 2; }
  int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
  vi = glXChooseVisual(x, DefaultScreen(x), attr);
  if (!vi) { printf("retire: no visual\n"); return 1; }
  Window main_win = make_window(1280, 720, 0);
  GLXContext main_ctx = glXCreateContext(x, vi, NULL, True);
  GLXContext pop_ctx = glXCreateContext(x, vi, main_ctx, True);
  for (int i = 0; i < 30; i++) frame(main_win, main_ctx, 0.1f, 0.3f, 0.6f);
  fprintf(stderr, "retire: main window up\n");
  for (int r = 0; r < rounds; r++) {
    fprintf(stderr, "retire: round %d: popup opens\n", r);
    Window pop = make_window(254, 102, r & 1);
    for (int i = 0; i < 20; i++) {
      frame(main_win, main_ctx, 0.1f, 0.3f, 0.6f);
      frame(pop, pop_ctx, 0.8f, 0.8f, 0.2f);
    }
    glXMakeCurrent(x, None, NULL);
    XDestroyWindow(x, pop);
    XSync(x, False);
    fprintf(stderr, "retire: round %d: popup closed\n", r);
    for (int i = 0; i < 20; i++) frame(main_win, main_ctx, 0.1f, 0.3f, 0.6f);
  }
  glFinish();
  printf("retire: done\n");
  return 0;
}
