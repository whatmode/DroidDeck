#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

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
  if (!real) real = (int (*)(void *))dlsym(RTLD_NEXT, "xcb_connection_has_error");
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
