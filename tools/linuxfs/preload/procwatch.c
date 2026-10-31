#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* BL_WEBHELPER_WATCH=1: how a steamwebhelper process ends, written to its stderr (with
 * BL_WEBHELPER_STDERR, steam/webhelper_stderr.txt) before it goes. Steam's web UI process
 * has been exiting within a second of starting, before it writes a line of its own, and Steam
 * starts another one every ~10 s. Covered: a crash or kill signal (with every thread's call
 * stack), exit() and _exit() (with the caller's call stack; glxreport.c calls in). Only
 * logging: each signal then does what it would have done anyway. */

static int watching = -1;

static int is_webhelper(void) {
  return program_invocation_short_name && !strcmp(program_invocation_short_name, "steamwebhelper");
}

__attribute__((visibility("hidden"))) int bl_webhelper_watching(void) {
  if (watching < 0) {
    const char *want = getenv("BL_WEBHELPER_WATCH");
    watching = want && !strcmp(want, "1") && is_webhelper();
  }
  return watching;
}

static void put(const char *s) { if (write(2, s, strlen(s)) < 0) { } }

static void put_num(long v) {
  char buf[24];
  int i = sizeof(buf) - 1;
  int neg = v < 0;
  unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
  buf[i] = 0;
  do { buf[--i] = (char)('0' + u % 10); u /= 10; } while (u && i > 1);
  if (neg) buf[--i] = '-';
  put(buf + i);
}

static void put_thread_name(long tid) {
  char path[64], name[32];
  snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  ssize_t n = read(fd, name, sizeof(name) - 1);
  close(fd);
  if (n <= 0) return;
  name[n] = 0;
  name[strcspn(name, "\n")] = 0;
  put(" (");
  put(name);
  put(")");
}

static void put_stack(void) {
  void *frames[48];
  int n = backtrace(frames, 48);
  backtrace_symbols_fd(frames, n, 2);
}

static int dump_signal;

/* Another thread asked: write this thread's call stack. */
static void on_dump(int sig) {
  (void)sig;
  int saved = errno;
  long tid = syscall(SYS_gettid);
  put("==== webhelper thread ");
  put_num(tid);
  put_thread_name(tid);
  put(":\n");
  put_stack();
  errno = saved;
}

static void dump_other_threads(void) {
  long self = syscall(SYS_gettid);
  int fd = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return;
  char buf[4096];
  long n;
  int sent = 0;
  while ((n = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0) {
    for (long off = 0; off < n;) {
      struct { unsigned long long ino; long long off; unsigned short reclen; unsigned char type; char name[]; } *d =
          (void *)(buf + off);
      long tid = atol(d->name);
      if (tid > 0 && tid != self && sent < 64) {
        syscall(SYS_tgkill, getpid(), tid, dump_signal);
        sent++;
      }
      off += d->reclen;
    }
  }
  close(fd);
  struct timespec ts = {0, 300 * 1000 * 1000};
  nanosleep(&ts, NULL);
}

static const char *signal_name(int sig) {
  switch (sig) {
  case SIGSEGV: return "SIGSEGV (bad memory access)";
  case SIGBUS: return "SIGBUS";
  case SIGILL: return "SIGILL";
  case SIGFPE: return "SIGFPE";
  case SIGABRT: return "SIGABRT (abort)";
  case SIGTRAP: return "SIGTRAP (Chromium's CHECK failures end here)";
  case SIGTERM: return "SIGTERM (asked to stop)";
  case SIGINT: return "SIGINT";
  case SIGHUP: return "SIGHUP";
  default: return "signal";
  }
}

static void on_fatal(int sig, siginfo_t *info, void *ctx) {
  (void)ctx;
  long tid = syscall(SYS_gettid);
  put("==== webhelper pid ");
  put_num(getpid());
  put(" ends on ");
  put(signal_name(sig));
  put(" in thread ");
  put_num(tid);
  put_thread_name(tid);
  if (info) {
    put(", from pid ");
    put_num(info->si_pid);
    put(", code ");
    put_num(info->si_code);
    put(", address 0x");
    char hex[20];
    unsigned long a = (unsigned long)info->si_addr;
    int i = sizeof(hex) - 1;
    hex[i] = 0;
    do { hex[--i] = "0123456789abcdef"[a & 15]; a >>= 4; } while (a && i > 0);
    put(hex + i);
  }
  put("\n");
  put_stack();
  dump_other_threads();
  put("==== end of webhelper threads\n");
  signal(sig, SIG_DFL);
  raise(sig);
}

__attribute__((visibility("hidden"))) void bl_webhelper_note_exit(const char *how, int status) {
  if (!bl_webhelper_watching()) return;
  int saved = errno;
  long tid = syscall(SYS_gettid);
  put("==== webhelper pid ");
  put_num(getpid());
  put(" calls ");
  put(how);
  put("(");
  put_num(status);
  put(") in thread ");
  put_num(tid);
  put_thread_name(tid);
  put(":\n");
  put_stack();
  errno = saved;
}

/* The session preload is also loaded as an audit library (LD_AUDIT, glxaudit.c), in a
 * namespace of its own that is unloaded again outside Steam's client: signal handlers set from
 * there would point at unmapped code. */
static int in_base_namespace(void) {
  Dl_info info;
  struct link_map *map = NULL;
  if (!dladdr1((void *)in_base_namespace, &info, (void **)&map, RTLD_DL_LINKMAP) || !map) return 1;
  Lmid_t lmid = LM_ID_BASE;
  if (dlinfo(map, RTLD_DI_LMID, &lmid) != 0) return 1;
  return lmid == LM_ID_BASE;
}

__attribute__((constructor)) static void bl_webhelper_watch(void) {
  if (!bl_webhelper_watching() || !in_base_namespace()) return;
  /* backtrace() loads libgcc on its first call; do that now, not in a signal handler. */
  void *warm[2];
  backtrace(warm, 2);
  static char altstack[64 * 1024];
  stack_t ss = {.ss_sp = altstack, .ss_size = sizeof(altstack), .ss_flags = 0};
  sigaltstack(&ss, NULL);
  dump_signal = SIGRTMIN + 7;
  struct sigaction dump = {0};
  dump.sa_handler = on_dump;
  dump.sa_flags = SA_RESTART;
  sigemptyset(&dump.sa_mask);
  sigaction(dump_signal, &dump, NULL);
  int fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGTERM, SIGINT, SIGHUP};
  for (unsigned i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++) {
    struct sigaction sa = {0};
    sa.sa_sigaction = on_fatal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(fatal[i], &sa, NULL);
  }
  put("==== webhelper pid ");
  put_num(getpid());
  put(": watching how it ends\n");
}
