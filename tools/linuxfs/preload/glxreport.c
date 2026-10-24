#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* BL_STEAM_GLX_REPORT=1: when Steam's client process ("steam", not its update UI child) exits
 * with a failure, first report from inside it what GL it had: the GL/Vulkan/X libraries it
 * loaded, the GL-related environment at that moment, and a GLX check run in the same process
 * (display, GLX extension, server and client strings, FB config count, glXChooseVisual on plain
 * attribute sets). The updated client exits on "glXChooseVisual failed" while a separate GL
 * program and its own update UI find visuals; the difference is in that process, so look there.
 * Everything goes to stderr (session.log) as "glxreport:" lines. */

extern char **environ;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
  char buf[1024];
  int n = snprintf(buf, sizeof(buf), "glxreport[%d]: ", (int)getpid());
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n - 1, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  size_t len = (size_t)n + ((size_t)m < sizeof(buf) - (size_t)n - 2 ? (size_t)m : sizeof(buf) - (size_t)n - 2);
  buf[len++] = '\n';
  if (write(2, buf, len) < 0) { }
}

static int is_client(void) {
  const char *want = getenv("BL_STEAM_GLX_REPORT");
  if (!want || strcmp(want, "1")) return 0;
  if (!program_invocation_short_name || strcmp(program_invocation_short_name, "steam")) return 0;
  int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 1;
  char buf[8192];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return 1;
  buf[n] = 0;
  for (char *p = buf; p < buf + n; p += strlen(p) + 1)
    if (!strcmp(p, "-child-update-ui")) return 0;
  return 1;
}

static void report_env(void) {
  static const char *prefixes[] = {"LIBGL_", "MESA_", "GALLIUM_", "ZINK_", "VK_", "__GLX_", "__EGL_", "DISPLAY=",
                                   "LD_LIBRARY_PATH=", "LD_PRELOAD=", "XDG_SESSION_TYPE=", "WAYLAND_DISPLAY=",
                                   "GAMESCOPE", "ENABLE_GAMESCOPE", "SDL_VIDEODRIVER=", NULL};
  for (char **e = environ; e && *e; e++)
    for (int i = 0; prefixes[i]; i++)
      if (!strncmp(*e, prefixes[i], strlen(prefixes[i]))) { say("env %.900s", *e); break; }
}

static void report_maps(void) {
  FILE *f = fopen("/proc/self/maps", "re");
  if (!f) return;
  char line[1024], seen[64][256];
  int nseen = 0;
  static const char *keys[] = {"libGL", "libGLX", "libGLdispatch", "libOpenGL", "libEGL", "gallium", "_dri.so", "libvulkan",
                               "libX11", "libxcb", "libXext", "libdrm", "vulkan_", "virtio", "panvk", "gamescope", NULL};
  while (fgets(line, sizeof(line), f)) {
    char *path = strchr(line, '/');
    if (!path) continue;
    path[strcspn(path, "\n")] = 0;
    int hit = 0;
    for (int i = 0; keys[i]; i++) if (strstr(path, keys[i])) { hit = 1; break; }
    if (!hit) continue;
    int dup = 0;
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], path)) { dup = 1; break; }
    if (dup || nseen >= 64) continue;
    snprintf(seen[nseen++], sizeof(seen[0]), "%s", path);
    say("loaded %s", path);
  }
  fclose(f);
}

typedef void *(*open_display_t)(const char *);
typedef int (*close_display_t)(void *);
typedef int (*default_screen_t)(void *);
typedef int (*glx_query_ext_t)(void *, int *, int *);
typedef const char *(*glx_server_string_t)(void *, int, int);
typedef const char *(*glx_client_string_t)(void *, int);
typedef void **(*glx_get_fbconfigs_t)(void *, int, int *);
typedef void *(*glx_choose_visual_t)(void *, int, int *);
typedef int (*xfree_t)(void *);

static void report_glx(void) {
  void *x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
  void *gl_loaded = dlopen("libGL.so.1", RTLD_NOW | RTLD_NOLOAD);
  void *gl = gl_loaded ? gl_loaded : dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
  say("libGL.so.1 %s in the process before this check", gl_loaded ? "was already" : "was NOT");
  if (!x11 || !gl) { say("cannot open libX11 (%p) or libGL (%p): %s", x11, gl, dlerror()); return; }
  glx_choose_visual_t choose = (glx_choose_visual_t)dlsym(gl, "glXChooseVisual");
  Dl_info info;
  if (choose && dladdr((void *)choose, &info)) say("glXChooseVisual comes from %s", info.dli_fname);
  void *global = dlsym(RTLD_DEFAULT, "glXChooseVisual");
  if (global && dladdr(global, &info)) say("glXChooseVisual by global lookup comes from %s", info.dli_fname);
  open_display_t open_display = (open_display_t)dlsym(x11, "XOpenDisplay");
  close_display_t close_display = (close_display_t)dlsym(x11, "XCloseDisplay");
  default_screen_t default_screen = (default_screen_t)dlsym(x11, "XDefaultScreen");
  xfree_t xfree = (xfree_t)dlsym(x11, "XFree");
  glx_query_ext_t query_ext = (glx_query_ext_t)dlsym(gl, "glXQueryExtension");
  glx_server_string_t server_string = (glx_server_string_t)dlsym(gl, "glXQueryServerString");
  glx_client_string_t client_string = (glx_client_string_t)dlsym(gl, "glXGetClientString");
  glx_get_fbconfigs_t get_fbconfigs = (glx_get_fbconfigs_t)dlsym(gl, "glXGetFBConfigs");
  if (!open_display || !default_screen || !query_ext || !server_string || !client_string || !get_fbconfigs || !choose) {
    say("missing X11/GLX entry points");
    return;
  }
  void *dpy = open_display(NULL);
  if (!dpy) { say("XOpenDisplay(%s) failed", getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)"); return; }
  int scr = default_screen(dpy), eb = 0, ev = 0;
  say("display %s, screen %d, GLX extension %s", getenv("DISPLAY") ? getenv("DISPLAY") : "?", scr,
      query_ext(dpy, &eb, &ev) ? "present" : "MISSING");
  const char *sv = server_string(dpy, scr, 1 /* GLX_VENDOR */);
  const char *ss = server_string(dpy, scr, 2 /* GLX_VERSION */);
  const char *cv = client_string(dpy, 1);
  const char *cs = client_string(dpy, 2);
  say("GLX server %s %s, client %s %s", sv ? sv : "?", ss ? ss : "?", cv ? cv : "?", cs ? cs : "?");
  int n = 0;
  void **cfgs = get_fbconfigs(dpy, scr, &n);
  say("glXGetFBConfigs: %d config(s)", cfgs ? n : 0);
  if (cfgs && xfree) xfree(cfgs);
  /* GLX_RGBA 4, GLX_DOUBLEBUFFER 5, GLX_RED_SIZE 8, GLX_GREEN_SIZE 9, GLX_BLUE_SIZE 10,
   * GLX_ALPHA_SIZE 11, GLX_DEPTH_SIZE 12, GLX_STENCIL_SIZE 13 */
  int sets[][16] = {
      {4, 5, 8, 8, 9, 8, 10, 8, 12, 24, 13, 8, 0},
      {4, 5, 8, 8, 9, 8, 10, 8, 11, 8, 0},
      {4, 5, 0},
      {4, 0},
  };
  const char *names[] = {"rgb888 depth24 stencil8 double", "rgba8888 double", "rgba double", "rgba"};
  for (int i = 0; i < 4; i++) {
    void *v = choose(dpy, scr, sets[i]);
    say("glXChooseVisual(%s): %s", names[i], v ? "visual" : "NULL");
    if (v && xfree) xfree(v);
  }
  if (close_display) close_display(dpy);
}

static int reported;

static void report(int status) {
  if (reported || status == 0 || !is_client()) return;
  reported = 1;
  int saved = errno;
  /* The check below opens its own display, so Mesa's loader reads these afresh. */
  setenv("LIBGL_DEBUG", "verbose", 1);
  setenv("VK_LOADER_DEBUG", "error,warn", 0);
  say("Steam client exiting with status %d: GL state of this process", status);
  report_env();
  report_maps();
  report_glx();
  say("end of report");
  errno = saved;
}

void exit(int status) {
  static void (*real)(int);
  if (!real) real = (void (*)(int))dlsym(RTLD_NEXT, "exit");
  report(status);
  real(status);
  __builtin_unreachable();
}

void _exit(int status) {
  report(status);
  syscall(SYS_exit_group, status);
  __builtin_unreachable();
}
