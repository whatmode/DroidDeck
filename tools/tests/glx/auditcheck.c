/* auditcheck <dlsym|gpa> <screen>: looks up glXChooseVisual the way a program might and asks
 * for a multisampled visual no config has; built as "steam" to test glxaudit.c. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef void *(*cv)(void*,int,int*);
int main(int c, char **v) {
  void *x = dlopen("libX11.so.6", RTLD_NOW|RTLD_GLOBAL);
  void *gl = dlopen("libGL.so.1", RTLD_NOW);
  void *(*od)(const char*) = dlsym(x, "XOpenDisplay");
  void *d = od(NULL);
  cv f;
  if (!strcmp(v[1], "dlsym")) f = (cv)dlsym(gl, "glXChooseVisual");
  else { void *(*g)(const unsigned char*) = dlsym(gl, "glXGetProcAddressARB"); f = (cv)g((const unsigned char*)"glXChooseVisual"); }
  int attr[] = {4, 5, 8, 8, 9, 8, 10, 8, 0x186a0 /*GLX_SAMPLE_BUFFERS*/, 1, 0x186a1, 16, 0};
  void *r = f(d, atoi(v[2]), attr);
  printf("program got %s\n", r ? "a visual" : "NULL");
  return 0;
}
