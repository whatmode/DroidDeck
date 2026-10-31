#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

void *bl_next_symbol(const char *library, const char *name) __attribute__((visibility("hidden")));

/* BL_XCB_TRACE=1: the first time an X connection reports an error in a process, print the
 * xcb error code and a backtrace to stderr. Steam's webhelper dies with "Fatal IO error 11
 * (Resource temporarily unavailable) on X server :0", which hides the real cause: xcb shuts
 * the connection for a reason (request too long, parse error, fd passing, server closed) and
 * Xlib only reports the errno left over. */
static const char *xcb_error_name(int e) {
  switch (e) {
  case 1: return "socket, pipe or stream error";
  case 2: return "extension not supported";
  case 3: return "out of memory";
  case 4: return "request longer than the server accepts";
  case 5: return "could not parse the server's reply";
  case 6: return "no such screen";
  case 7: return "file descriptor passing failed";
  default: return "unknown";
  }
}

int xcb_connection_has_error(void *c) {
  static int (*real)(void *);
  static int reported;
  if (!real) real = (int (*)(void *))bl_next_symbol("libxcb.so.1", "xcb_connection_has_error");
  int saved = errno;
  int r = real ? real(c) : 0;
  if (r && !reported) {
    const char *want = getenv("BL_XCB_TRACE");
    if (want && !strcmp(want, "1")) {
      reported = 1;
      fprintf(stderr, "xcb: X connection %p failed: %d (%s), errno %d (%s), pid %d tid %ld (%s)\n",
              c, r, xcb_error_name(r), saved, strerror(saved), (int)getpid(), (long)syscall(SYS_gettid),
              program_invocation_short_name ? program_invocation_short_name : "?");
      void *frames[48];
      int n = backtrace(frames, 48);
      backtrace_symbols_fd(frames, n, 2);
    }
  }
  errno = saved;
  return r;
}

/* The connection failure above is "extension not supported": xcb closes the whole connection
 * when a request for an extension the server lacks is sent (Xwayland -shm has no DRI3, for
 * one). It is only noticed later, in whichever thread reads events next, so also catch the
 * request itself: before each extension request, name the missing extension and the caller. */
typedef struct { const char *name; int global_id; } bl_xcb_extension_t;
typedef struct { size_t count; bl_xcb_extension_t *ext; unsigned char opcode; unsigned char isvoid; } bl_xcb_protocol_request_t;
typedef struct { unsigned char response_type, pad0; unsigned short sequence; unsigned int length;
                 unsigned char present, major_opcode, first_event, first_error; } bl_xcb_query_extension_reply_t;

static void check_extension(void *c, const bl_xcb_protocol_request_t *req) {
  static int enabled = -1;
  if (enabled < 0) { const char *w = getenv("BL_XCB_TRACE"); enabled = w && !strcmp(w, "1"); }
  if (!enabled || !req || !req->ext) return;
  static const bl_xcb_query_extension_reply_t *(*get_ext)(void *, bl_xcb_extension_t *);
  if (!get_ext) get_ext = (const bl_xcb_query_extension_reply_t *(*)(void *, bl_xcb_extension_t *))bl_next_symbol("libxcb.so.1", "xcb_get_extension_data");
  if (!get_ext) return;
  const bl_xcb_query_extension_reply_t *r = get_ext(c, req->ext);
  if (r && r->present) return;
  static int reported;
  if (reported++ >= 4) return;
  int saved = errno;
  fprintf(stderr, "xcb: request %u of missing extension %s on connection %p (this closes it), pid %d tid %ld (%s)\n",
          req->opcode, req->ext->name ? req->ext->name : "?", c, (int)getpid(), (long)syscall(SYS_gettid),
          program_invocation_short_name ? program_invocation_short_name : "?");
  void *frames[48];
  int n = backtrace(frames, 48);
  backtrace_symbols_fd(frames, n, 2);
  errno = saved;
}

unsigned int xcb_send_request(void *c, int flags, void *vector, const bl_xcb_protocol_request_t *req) {
  static unsigned int (*real)(void *, int, void *, const bl_xcb_protocol_request_t *);
  if (!real) real = (unsigned int (*)(void *, int, void *, const bl_xcb_protocol_request_t *))bl_next_symbol("libxcb.so.1", "xcb_send_request");
  check_extension(c, req);
  return real ? real(c, flags, vector, req) : 0;
}

unsigned long long xcb_send_request64(void *c, int flags, void *vector, const bl_xcb_protocol_request_t *req) {
  static unsigned long long (*real)(void *, int, void *, const bl_xcb_protocol_request_t *);
  if (!real) real = (unsigned long long (*)(void *, int, void *, const bl_xcb_protocol_request_t *))bl_next_symbol("libxcb.so.1", "xcb_send_request64");
  check_extension(c, req);
  return real ? real(c, flags, vector, req) : 0;
}

unsigned int xcb_send_request_with_fds(void *c, int flags, void *vector, const bl_xcb_protocol_request_t *req,
                                       unsigned int num_fds, int *fds) {
  static unsigned int (*real)(void *, int, void *, const bl_xcb_protocol_request_t *, unsigned int, int *);
  if (!real) real = (unsigned int (*)(void *, int, void *, const bl_xcb_protocol_request_t *, unsigned int, int *))bl_next_symbol("libxcb.so.1", "xcb_send_request_with_fds");
  check_extension(c, req);
  return real ? real(c, flags, vector, req, num_fds, fds) : 0;
}

unsigned long long xcb_send_request_with_fds64(void *c, int flags, void *vector, const bl_xcb_protocol_request_t *req,
                                               unsigned int num_fds, int *fds) {
  static unsigned long long (*real)(void *, int, void *, const bl_xcb_protocol_request_t *, unsigned int, int *);
  if (!real) real = (unsigned long long (*)(void *, int, void *, const bl_xcb_protocol_request_t *, unsigned int, int *))bl_next_symbol("libxcb.so.1", "xcb_send_request_with_fds64");
  check_extension(c, req);
  return real ? real(c, flags, vector, req, num_fds, fds) : 0;
}
