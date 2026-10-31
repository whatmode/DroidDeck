/* xshot: how much of the X screen is not black (a presented frame shows) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
int main(void) {
  Display *d = XOpenDisplay(NULL);
  if (!d) { printf("xshot: no display\n"); return 2; }
  Window root = DefaultRootWindow(d);
  XWindowAttributes wa;
  XGetWindowAttributes(d, root, &wa);
  XImage *img = XGetImage(d, root, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
  if (!img) { printf("xshot: no image\n"); return 2; }
  long lit = 0;
  for (int y = 0; y < wa.height; y += 2)
    for (int x = 0; x < wa.width; x += 2)
      if (XGetPixel(img, x, y) & 0xffffff) lit++;
  long total = (long)((wa.width + 1) / 2) * ((wa.height + 1) / 2);
  printf("xshot: %ld of %ld sampled pixels lit (%.1f%%)\n", lit, total, 100.0 * lit / total);
  return 0;
}
