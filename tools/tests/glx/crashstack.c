/* crashstack: a GL program that sets up GLX, puts in its own crash handler afterwards (as
 * Steam's crash reporter does), closes its display and then corrupts the heap, as Steam's
 * client did at exit ("free(): invalid size"). The driver's GLX prints the call stack and
 * then hands over to the program's handler. */
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void own(int sig) {
  static const char msg[] = "crashstack: own handler ran\n";
  if (write(2, msg, sizeof(msg) - 1) < 0) {}
  _exit(3);
}

int main(void) {
  Display *x = XOpenDisplay(NULL);
  if (!x) return 2;
  int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
  XVisualInfo *vi = glXChooseVisual(x, DefaultScreen(x), attr);
  printf("crashstack: visual %s\n", vi ? "found" : "missing");
  fflush(stdout);
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = own;
  sigaction(SIGABRT, &sa, NULL);
  XCloseDisplay(x);
  char *p = malloc(64);
  free(p + 16); /* invalid pointer: glibc aborts */
  return 0;
}
