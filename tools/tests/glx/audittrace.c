/* audittrace: a few GLX calls through dlsym and glXGetProcAddress, then a context; built as
 * "steam" to test glxaudit.c call tracing. */
#include <dlfcn.h>
#include <stdio.h>
typedef void *(*cv)(void*,int,int*);
int main(void) {
  void *x = dlopen("libX11.so.6", RTLD_NOW|RTLD_GLOBAL);
  void *gl = dlopen("libGL.so.1", RTLD_NOW);
  void *(*od)(const char*) = dlsym(x, "XOpenDisplay");
  void *d = od(NULL);
  int (*qv)(void*,int*,int*) = dlsym(gl, "glXQueryVersion");
  const char *(*qs)(void*,int,int) = dlsym(gl, "glXQueryServerString");
  void *(*gpa)(const char*) = dlsym(gl, "glXGetProcAddress");
  void **(*fb)(void*,int,int*) = gpa("glXGetFBConfigs");
  int a,b,n; qv(d,&a,&b); qs(d,0,1); fb(d,0,&n);
  cv f = (cv)dlsym(gl, "glXChooseVisual");
  int attr[] = {4, 5, 0};
  void *(*cc)(void*,void*,void*,int) = dlsym(gl, "glXCreateContext");
  void *vi = f(d, 0, attr);
  void *ctx = cc(d, vi, 0, 1);
  printf("program: version %d.%d, %d configs, visual %p ctx %p\n", a, b, n, vi, ctx);
  return 0;
}
