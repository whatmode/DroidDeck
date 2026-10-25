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
#include <string.h>
#include <unistd.h>
#include <time.h>

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
  const char *mode = argc > 2 ? argv[2] : "popups";
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
  if (!strncmp(mode, "menu", 4)) {
    /* Steam's side menu: a second window (358 px wide, at the left) over the main one, both
     * drawn every frame, the menu from another X connection ("menu2") or the same ("menu1").
     * The main window is blue, the menu yellow; screenshots tell whether both show. */
    XResizeWindow(x, main_win, 1600, 720);
    Display *x2 = !strcmp(mode, "menu2") ? XOpenDisplay(NULL) : x;
    XVisualInfo *vi2 = glXChooseVisual(x2, DefaultScreen(x2), attr);
    XSetWindowAttributes swa = {0};
    swa.colormap = XCreateColormap(x2, RootWindow(x2, vi2->screen), vi2->visual, AllocNone);
    swa.override_redirect = True;
    Window menu = XCreateWindow(x2, RootWindow(x2, vi2->screen), 0, 0, 358, 720, 0, vi2->depth, InputOutput, vi2->visual,
                                CWColormap | CWOverrideRedirect, &swa);
    XMapRaised(x2, menu);
    XSync(x2, False);
    GLXContext c2 = glXCreateContext(x2, vi2, NULL, True);
    fprintf(stderr, "retire: menu window up\n");
    for (int i = 0; i < rounds * 60; i++) {
      frame(main_win, main_ctx, 0.0f, 0.0f, 1.0f);
      glXMakeCurrent(x2, menu, c2);
      glClearColor(1.0f, 1.0f, 0.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glXSwapBuffers(x2, menu);
      usleep(16000);
    }
    glFinish();
    printf("retire: done\n");
    return 0;
  }
  if (!strncmp(mode, "sidemenu", 8)) {
    /* Steam's layout from its logs: Chromium's toplevel (not drawn by GL) holds plain windows,
     * each holding a GL window: the main one (blue, whole size) and the side menu (yellow,
     * 358 px at the left, stacked above), both drawn every frame from another connection.
     * "sidemenustill": as Steam with its menu open, the main view stops drawing after a few
     * frames and Chromium draws its toplevel (red) now and then; X still shows main + menu. */
    int still = !strcmp(mode, "sidemenustill");
    /* "sidemenulate": the menu comes up only after the main view went still (Steam's webhelper
     * crashed there reading the main view's released swapchain image) */
    int late = !strcmp(mode, "sidemenulate");
    if (late) still = 1;
    glXMakeCurrent(x, None, NULL);
    XUnmapWindow(x, main_win);
    XSync(x, False);
    XSetWindowAttributes tswa = {0};
    tswa.colormap = XCreateColormap(x, RootWindow(x, vi->screen), vi->visual, AllocNone);
    Window top = XCreateWindow(x, RootWindow(x, vi->screen), 0, 0, 1600, 720, 0, vi->depth, InputOutput, vi->visual,
                               CWColormap, &tswa);
    XMapWindow(x, top);
    XSync(x, False);
    Display *x2 = XOpenDisplay(NULL);
    XVisualInfo *vi2 = glXChooseVisual(x2, DefaultScreen(x2), attr);
    XSetWindowAttributes swa = {0};
    swa.colormap = XCreateColormap(x2, RootWindow(x2, vi2->screen), vi2->visual, AllocNone);
    swa.override_redirect = True;
    unsigned long m = CWColormap | CWOverrideRedirect;
    Window h1 = XCreateWindow(x2, top, 0, 0, 1600, 720, 0, vi2->depth, InputOutput, vi2->visual, CWColormap, &swa);
    Window g1 = XCreateWindow(x2, h1, 0, 0, 1600, 720, 0, vi2->depth, InputOutput, vi2->visual, m, &swa);
    Window h2 = XCreateWindow(x2, top, 0, 0, 358, 720, 0, vi2->depth, InputOutput, vi2->visual, CWColormap, &swa);
    Window g2 = XCreateWindow(x2, h2, 0, 0, 358, 720, 0, vi2->depth, InputOutput, vi2->visual, m, &swa);
    XMapWindow(x2, g1); XMapWindow(x2, h1); XMapWindow(x2, g2);
    if (!late) XMapWindow(x2, h2);
    XSync(x2, False);
    GLXContext c1 = glXCreateContext(x2, vi2, NULL, True), c2 = glXCreateContext(x2, vi2, c1, True);
    fprintf(stderr, "retire: toplevel 0x%lx with main 0x%lx and menu 0x%lx\n", (unsigned long)top, (unsigned long)g1, (unsigned long)g2);
    GLXContext c0 = glXCreateContext(x, vi, NULL, True);
    for (int i = 0; i < rounds * 60; i++) {
      if (!still || i < 30) {
        glXMakeCurrent(x2, g1, c1);
        glClearColor(0.0f, 0.0f, 1.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glXSwapBuffers(x2, g1);
      }
      if (still && i % 60 == 45) {
        glXMakeCurrent(x, top, c0);
        glClearColor(1.0f, 0.0f, 0.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glXSwapBuffers(x, top);
      }
      if (late && i == 60) { XMapWindow(x2, h2); XSync(x2, False); fprintf(stderr, "retire: menu up over the still main view\n"); }
      if (late && i < 60) { usleep(16000); continue; }
      glXMakeCurrent(x2, g2, c2);
      glClearColor(1.0f, 1.0f, 0.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glXSwapBuffers(x2, g2);
      usleep(16000);
    }
    glFinish();
    printf("retire: done\n");
    return 0;
  }
  if (!strncmp(mode, "child", 5)) {
    /* A GL child window the size of the main one, drawn from another X connection (as
     * Chromium and ANGLE make one inside Steam's window): left unmapped ("childhidden",
     * X shows the main window: blue) or mapped over it ("childshown", X shows the child:
     * yellow). Both are drawn every frame; screenshots tell which one gamescope shows. */
    XResizeWindow(x, main_win, 1600, 720);
    XSync(x, False);
    int shown = strcmp(mode, "childhidden") != 0;
    int wrapped = !strcmp(mode, "childwrapped");
    Display *x2 = XOpenDisplay(NULL);
    XVisualInfo *vi2 = glXChooseVisual(x2, DefaultScreen(x2), attr);
    XSetWindowAttributes swa = {0};
    swa.colormap = XCreateColormap(x2, RootWindow(x2, vi2->screen), vi2->visual, AllocNone);
    /* "childwrapped": as in Steam, the GL window sits in a plain child (no GL) of the main one. */
    Window holder = main_win;
    if (wrapped) {
      holder = XCreateWindow(x2, main_win, 0, 0, 1600, 720, 0, vi2->depth, InputOutput, vi2->visual, CWColormap, &swa);
      XMapWindow(x2, holder);
    }
    Window child = XCreateWindow(x2, holder, 0, 0, 1600, 720, 0, vi2->depth, InputOutput, vi2->visual, CWColormap, &swa);
    if (shown) XMapWindow(x2, child);
    XSync(x2, False);
    GLXContext c2 = glXCreateContext(x2, vi2, NULL, True);
    fprintf(stderr, "retire: child window 0x%lx %s\n", (unsigned long)child, shown ? "mapped" : "unmapped");
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < rounds * 60; i++) {
      frame(main_win, main_ctx, 0.0f, 0.0f, 1.0f);
      glXMakeCurrent(x2, child, c2);
      glClearColor(1.0f, 1.0f, 0.0f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glXSwapBuffers(x2, child);
      usleep(16000);
    }
    glFinish();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    fprintf(stderr, "retire: %d frame pairs in %.0f ms (%.1f ms each, 16 ms of it sleeping)\n", rounds * 60, ms, ms / (rounds * 60));
    printf("retire: done\n");
    return 0;
  }
  if (!strcmp(mode, "twodisplays") || !strcmp(mode, "twocontexts")) {
    /* The same window drawn from a second GL context: on another X connection (Steam's
     * client holds several), or on the same one. */
    Display *x2 = !strcmp(mode, "twodisplays") ? XOpenDisplay(NULL) : x;
    XVisualInfo *vi2 = glXChooseVisual(x2, DefaultScreen(x2), attr);
    GLXContext c2 = glXCreateContext(x2, vi2, NULL, True);
    for (int r = 0; r < rounds; r++) {
      for (int i = 0; i < 10; i++) frame(main_win, main_ctx, 0.1f, 0.3f, 0.6f);
      glXMakeCurrent(x, None, NULL);
      glXMakeCurrent(x2, main_win, c2);
      for (int i = 0; i < 10; i++) { glClearColor(0.6f, 0.3f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT); glXSwapBuffers(x2, main_win); }
      glXMakeCurrent(x2, None, NULL);
      fprintf(stderr, "retire: round %d: drawn from the second context\n", r);
    }
    glFinish();
    printf("retire: done\n");
    return 0;
  }
  for (int r = 0; r < rounds; r++) {
    /* Steam's windows also change size (created 1280x800, made fullscreen, back). */
    if (r == 1) { XResizeWindow(x, main_win, 1600, 720); XSync(x, False); fprintf(stderr, "retire: main window resized\n"); }
    if (r == 2) { XUnmapWindow(x, main_win); XSync(x, False); for (int i = 0; i < 3; i++) frame(main_win, main_ctx, 0.1f, 0.3f, 0.6f);
                  XMapWindow(x, main_win); XSync(x, False); fprintf(stderr, "retire: main window unmapped and mapped\n"); }
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
