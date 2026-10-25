/* crashstack: a GL program that sets up GLX and then corrupts the heap, as Steam's client
 * did at exit ("free(): invalid size"). The driver's GLX prints the call stack. */
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  Display *x = XOpenDisplay(NULL);
  if (!x) return 2;
  int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, None};
  XVisualInfo *vi = glXChooseVisual(x, DefaultScreen(x), attr);
  printf("crashstack: visual %s\n", vi ? "found" : "missing");
  fflush(stdout);
  char *p = malloc(64);
  free(p + 16); /* invalid pointer: glibc aborts */
  return 0;
}
