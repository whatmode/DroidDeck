/* auditkopper [driver]: sets LIBGL_KOPPER_DISABLE=true in its own environment after start, as
 * the Steam client does, then asks for the visual Steam asks for; built as "steam" to test
 * glxaudit.c. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
typedef void *(*cv)(void*,int,int*);
int main(int c, char **v) {
  void *x = dlopen("libX11.so.6", RTLD_NOW|RTLD_GLOBAL);
  void *gl = dlopen("libGL.so.1", RTLD_NOW);
  void *(*od)(const char*) = dlsym(x, "XOpenDisplay");
  void *d = od(NULL);
  /* like Steam: set after start, in the program's own environment */
  setenv("LIBGL_KOPPER_DISABLE", "true", 1);
  if (c > 1) setenv("MESA_LOADER_DRIVER_OVERRIDE", v[1], 1);
  cv f = (cv)dlsym(gl, "glXChooseVisual");
  int attr[] = {4, 8, 1, 9, 1, 10, 1, 5, 12, 1, 0};
  void *r = f(d, 0, attr);
  printf("program got %s, LIBGL_KOPPER_DISABLE now %s\n", r ? "a visual" : "NULL", getenv("LIBGL_KOPPER_DISABLE") ? "set" : "unset");
  return 0;
}
