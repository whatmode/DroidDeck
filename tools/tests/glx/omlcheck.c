/* omlcheck: GLX_OML_sync_control as Chromium (Steam's UI, through ANGLE) uses it for its frame
 * clock: two glXGetSyncValuesOML 100 ms apart on a presented GL window. Prints the vblank rate
 * they imply and how long each call took (it must not block). */
#define GLX_GLXEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

int main(void) {
  Display *x = XOpenDisplay(NULL);
  if (!x) return 2;
  int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, None};
  XVisualInfo *vi = glXChooseVisual(x, DefaultScreen(x), attr);
  XSetWindowAttributes swa = {0};
  swa.colormap = XCreateColormap(x, RootWindow(x, vi->screen), vi->visual, AllocNone);
  Window w = XCreateWindow(x, RootWindow(x, vi->screen), 0, 0, 640, 360, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
  XMapWindow(x, w);
  XSync(x, False);
  GLXContext c = glXCreateContext(x, vi, NULL, True);
  glXMakeCurrent(x, w, c);
  const char *exts = glXQueryExtensionsString(x, DefaultScreen(x));
  printf("omlcheck: GLX_OML_sync_control %s\n", strstr(exts, "GLX_OML_sync_control") ? "listed" : "MISSING");
  PFNGLXGETSYNCVALUESOMLPROC get = (PFNGLXGETSYNCVALUESOMLPROC)glXGetProcAddress((const GLubyte *)"glXGetSyncValuesOML");
  PFNGLXWAITFORMSCOMLPROC waitmsc = (PFNGLXWAITFORMSCOMLPROC)glXGetProcAddress((const GLubyte *)"glXWaitForMscOML");
  for (int i = 0; i < 20; i++) { glClear(GL_COLOR_BUFFER_BIT); glXSwapBuffers(x, w); }
  int64_t ust1, msc1, sbc1, ust2, msc2, sbc2;
  double t0 = ms();
  Bool ok1 = get(x, w, &ust1, &msc1, &sbc1);
  double t1 = ms();
  usleep(100000);
  double t2 = ms();
  Bool ok2 = get(x, w, &ust2, &msc2, &sbc2);
  double t3 = ms();
  double hz = (ust2 > ust1) ? (msc2 - msc1) * 1e6 / (double)(ust2 - ust1) : 0;
  printf("omlcheck: calls %d %d, took %.2f ms and %.2f ms, msc %lld -> %lld, %.1f Hz\n", ok1, ok2, t1 - t0, t3 - t2,
         (long long)msc1, (long long)msc2, hz);
  int64_t u, m, s;
  double t4 = ms();
  Bool ok3 = waitmsc(x, w, msc2 + 6, 0, 0, &u, &m, &s);
  double t5 = ms();
  printf("omlcheck: wait for 6 vblanks: %d, %.1f ms, msc %lld\n", ok3, t5 - t4, (long long)m);
  printf("omlcheck: done\n");
  return 0;
}
