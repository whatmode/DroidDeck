/* Opens the X libraries RTLD_LOCAL, as Steam's client does, then sends one core request with a
 * reply and one extension request through xcb: the preload's xcb wrappers must pass them on. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
typedef struct { unsigned int sequence; } cookie_t;
int main(void) {
  void *x = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  void *xx = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_LOCAL);
  void *xcb = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!x || !xx || !xcb) { printf("dlopen failed\n"); return 2; }
  void *(*od)(const char *) = dlsym(x, "XOpenDisplay");
  void *(*gx)(void *) = dlsym(xx, "XGetXCBConnection");
  cookie_t (*focus)(void *) = dlsym(xcb, "xcb_get_input_focus");
  void *(*focus_reply)(void *, cookie_t, void **) = dlsym(xcb, "xcb_get_input_focus_reply");
  cookie_t (*qe)(void *, uint16_t, const char *) = dlsym(xcb, "xcb_query_extension");
  void *(*qe_reply)(void *, cookie_t, void **) = dlsym(xcb, "xcb_query_extension_reply");
  /* An extension library opened locally, as Mesa's GLX library is (glvnd dlopens it): its
   * requests go through xcb_send_request by global lookup, which is where the preload sits. */
  void *glx = dlopen("libxcb-glx.so.0", RTLD_NOW | RTLD_LOCAL);
  cookie_t (*gqv)(void *, uint32_t, uint32_t) = glx ? dlsym(glx, "xcb_glx_query_version") : NULL;
  void *(*gqv_reply)(void *, cookie_t, void **) = glx ? dlsym(glx, "xcb_glx_query_version_reply") : NULL;
  int (*chprop)(void *, unsigned long, unsigned long, unsigned long, int, int, const unsigned char *, int) = dlsym(x, "XChangeProperty");
  void *d = od(NULL);
  if (!d) { printf("no display\n"); return 2; }
  void *c = gx(d);
  cookie_t ck = focus(c);
  void *r = focus_reply(c, ck, NULL);
  cookie_t qk = qe(c, 3, "GLX");
  void *q = qe_reply(c, qk, NULL);
  cookie_t vk = gqv ? gqv(c, 1, 4) : (cookie_t){0};
  void *vr = gqv ? gqv_reply(c, vk, NULL) : NULL;
  printf("GLX QueryVersion through libxcb-glx: request %u, %s\n", vk.sequence, vr ? "reply" : "NO reply");
  printf("GetInputFocus: request %u, %s; QueryExtension: request %u, %s\n", ck.sequence, r ? "reply" : "NO reply",
         qk.sequence, q ? "reply" : "NO reply");
  return r && q && vr ? 0 : 1;
}
