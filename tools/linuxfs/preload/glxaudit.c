#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Loaded as LD_AUDIT (the session preload doubles as an audit library) for the Steam client
 * in the Venus bridge's one-driver mode. The client's vgui2 surface exits on "glXChooseVisual
 * failed", yet the same call made from inside that process right after (glxreport.c) finds
 * visuals, and the client never got as far as loading Mesa's Vulkan driver. So see Steam's
 * own call, however it looks the function up (linked, dlsym or glXGetProcAddress): its
 * display, screen and attribute list, and the result. When it fails, retry on the display's
 * screen 0 and with plain attribute lists, and hand the first visual found back to Steam
 * ("rescued"), so Steam's UI can start while the report says what it asked for.
 *
 * Only the client's main process audits (not its update UI child, not other programs). */

static int audit_on;
/* Handles to the program's own libraries: dlmopen in the base namespace (a link map pointer
 * from la_objopen is not usable with dlsym from here). */
static void *base_lib(const char *name) { return dlmopen(LM_ID_BASE, name, RTLD_LAZY | RTLD_NOLOAD); }
#define x11_map base_lib("libX11.so.6")
#define libc_map base_lib("libc.so.6")
#define gl_map base_lib("libGL.so.1")

static void out(const char *s) { if (write(2, s, strlen(s)) < 0) { } }

static int read_file(const char *path, char *buf, size_t size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = read(fd, buf, size - 1);
  close(fd);
  if (n < 0) return -1;
  buf[n] = 0;
  return (int)n;
}

unsigned int la_version(unsigned int version) {
  char cmd[8192];
  int n = read_file("/proc/self/cmdline", cmd, sizeof(cmd));
  if (n <= 0) return 0;
  const char *base = strrchr(cmd, '/');
  base = base ? base + 1 : cmd;
  if (strcmp(base, "steam")) return 0;
  for (char *p = cmd; p < cmd + n; p += strlen(p) + 1)
    if (!strcmp(p, "-child-update-ui")) return 0;
  audit_on = 1;
  return version < LAV_CURRENT ? version : LAV_CURRENT;
}

static int wanted_target(const char *name) {
  if (!name) return 0;
  const char *b = strrchr(name, '/');
  b = b ? b + 1 : name;
  return !strncmp(b, "libGL.so", 8) || !strncmp(b, "libGLX.so", 9) || !strncmp(b, "libGLX_mesa", 11) ||
         !strncmp(b, "libX11.so", 9);
}

unsigned int la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie) {
  *cookie = (uintptr_t)map;
  if (!audit_on) return 0;
  return LA_FLG_BINDFROM | (wanted_target(map->l_name) ? LA_FLG_BINDTO : 0);
}

typedef void *(*choose_t)(void *, int, int *);
typedef void *(*gpa_t)(const unsigned char *);
typedef int (*int_dpy_t)(void *);
typedef char *(*str_dpy_t)(void *);

static choose_t real_choose;
static gpa_t real_gpa, real_gpa_arb;
static int_dpy_t x_screen_count, x_default_screen;
static str_dpy_t x_display_string;

static void log_attribs(const char *what, void *dpy, int screen, const int *attr, void *result) {
  /* A link map is a dlopen handle; look up what the program itself did not bind. */
  if (x11_map) {
    if (!x_screen_count) x_screen_count = (int_dpy_t)dlsym(x11_map, "XScreenCount");
    if (!x_default_screen) x_default_screen = (int_dpy_t)dlsym(x11_map, "XDefaultScreen");
    if (!x_display_string) x_display_string = (str_dpy_t)dlsym(x11_map, "XDisplayString");
  }
  char buf[1536];
  int n = snprintf(buf, sizeof(buf), "glxaudit[%d]: %s(dpy %p \"%s\", screen %d of %d, default %d, attribs",
                   (int)getpid(), what, dpy, dpy && x_display_string ? x_display_string(dpy) : "?", screen,
                   dpy && x_screen_count ? x_screen_count(dpy) : -1, dpy && x_default_screen ? x_default_screen(dpy) : -1);
  for (int i = 0; attr && attr[i] && i < 60 && n < (int)sizeof(buf) - 64; i++)
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, " 0x%x", attr[i]);
  snprintf(buf + n, sizeof(buf) - (size_t)n, "): %s\n", result ? "visual" : "NULL");
  out(buf);
}

/* The program's own environment (its libc, not this audit namespace's copy, which does not
 * see later setenv calls). */
static char ***main_environ(void) { return libc_map ? (char ***)dlsym(libc_map, "environ") : NULL; }

static void log_env(void) {
  static const char *prefixes[] = {"LIBGL_", "MESA_", "GALLIUM_", "ZINK_", "VK_", "__GLX_", "__EGL_", "DISPLAY=",
                                   "XAUTHORITY=", "LD_LIBRARY_PATH=", NULL};
  char ***env = main_environ();
  if (!env || !*env) { out("glxaudit: program environment not found\n"); return; }
  for (char **e = *env; *e; e++)
    for (int i = 0; prefixes[i]; i++)
      if (!strncmp(*e, prefixes[i], strlen(prefixes[i]))) {
        char buf[1100];
        snprintf(buf, sizeof(buf), "glxaudit[%d]: env at the call: %.1000s\n", (int)getpid(), *e);
        out(buf);
        break;
      }
}

typedef int (*setenv_t)(const char *, const char *, int);
typedef int (*unsetenv_t)(const char *);
typedef char *(*getenv_t)(const char *);

/* GLX as seen on the program's own display. */
static void log_glx(void *dpy, int screen) {
  if (!gl_map) return;
  int (*query_ext)(void *, int *, int *) = (int (*)(void *, int *, int *))dlsym(gl_map, "glXQueryExtension");
  int (*query_ver)(void *, int *, int *) = (int (*)(void *, int *, int *))dlsym(gl_map, "glXQueryVersion");
  const char *(*server_string)(void *, int, int) = (const char *(*)(void *, int, int))dlsym(gl_map, "glXQueryServerString");
  void **(*get_fbconfigs)(void *, int, int *) = (void **(*)(void *, int, int *))dlsym(gl_map, "glXGetFBConfigs");
  int eb = 0, ev = 0, maj = 0, min = 0, n = 0;
  int ext = query_ext ? query_ext(dpy, &eb, &ev) : -1;
  int ver = query_ver ? query_ver(dpy, &maj, &min) : -1;
  const char *vendor = server_string ? server_string(dpy, screen, 1) : NULL;
  void **cfgs = get_fbconfigs ? get_fbconfigs(dpy, screen, &n) : NULL;
  char buf[512];
  snprintf(buf, sizeof(buf), "glxaudit[%d]: on this display: GLX extension %d, version %d (%d.%d), server vendor %s, %d FB config(s)\n",
           (int)getpid(), ext, ver, maj, min, vendor ? vendor : "(none)", cfgs ? n : 0);
  out(buf);
}

static void *audit_choose_visual(void *dpy, int screen, int *attr) {
  void *v = real_choose ? real_choose(dpy, screen, attr) : NULL;
  log_attribs("glXChooseVisual", dpy, screen, attr, v);
  if (v || !real_choose || !dpy) return v;
  static int once;
  setenv_t set = libc_map ? (setenv_t)dlsym(libc_map, "setenv") : NULL;
  unsetenv_t unset = libc_map ? (unsetenv_t)dlsym(libc_map, "unsetenv") : NULL;
  getenv_t get = libc_map ? (getenv_t)dlsym(libc_map, "getenv") : NULL;
  if (!once++) {
    log_env();
    log_glx(dpy, screen);
    /* Mesa forgets a display whose GLX set-up failed, so a retry sets it up again: with its
     * loader's messages this time. */
    if (set && get && !get("LIBGL_DEBUG")) {
      set("LIBGL_DEBUG", "verbose", 1);
      v = real_choose(dpy, screen, attr);
      log_attribs("retry with LIBGL_DEBUG=verbose", dpy, screen, attr, v);
      if (unset) unset("LIBGL_DEBUG");
      if (v) { out("glxaudit: rescued by simply retrying\n"); return v; }
    }
  }
  /* Steam sets LIBGL_KOPPER_DISABLE=true for its webhelper; if the client's own GLX sees it,
   * Zink cannot draw on this Xwayland. The webhelper drops it again (webhelper.c). */
  if (get && unset && get("LIBGL_KOPPER_DISABLE")) {
    char saved[64];
    snprintf(saved, sizeof(saved), "%s", get("LIBGL_KOPPER_DISABLE"));
    unset("LIBGL_KOPPER_DISABLE");
    v = real_choose(dpy, screen, attr);
    log_attribs("retry without LIBGL_KOPPER_DISABLE", dpy, screen, attr, v);
    if (v) { out("glxaudit: rescued: LIBGL_KOPPER_DISABLE stays unset in the client\n"); return v; }
    if (set) set("LIBGL_KOPPER_DISABLE", saved, 1);
  }
  /* GLX_RGBA 4, GLX_DOUBLEBUFFER 5, GLX_DEPTH_SIZE 12 */
  static int plain[][8] = {{4, 5, 12, 24, 0}, {4, 5, 0}, {4, 0}};
  int screens[2] = {screen, 0};
  for (int s = 0; s < (screen ? 2 : 1); s++) {
    if (s) { v = real_choose(dpy, 0, attr); log_attribs("retry same attribs on screen 0", dpy, 0, attr, v); if (v) break; }
    for (int i = 0; i < 3 && !v; i++) {
      v = real_choose(dpy, screens[s], plain[i]);
      log_attribs("retry plain", dpy, screens[s], plain[i], v);
    }
    if (v) break;
  }
  if (v) out("glxaudit: rescued: Steam gets the visual found by the retry\n");
  return v;
}

static void *audit_gpa_common(gpa_t real, const unsigned char *name) {
  void *f = real ? (void *)real(name) : NULL;
  if (f && name && !strcmp((const char *)name, "glXChooseVisual")) {
    if (!real_choose) real_choose = (choose_t)f;
    out("glxaudit: glXChooseVisual looked up through glXGetProcAddress\n");
    return (void *)audit_choose_visual;
  }
  return f;
}
static void *audit_gpa(const unsigned char *name) { return audit_gpa_common(real_gpa, name); }
static void *audit_gpa_arb(const unsigned char *name) { return audit_gpa_common(real_gpa_arb, name); }

static uintptr_t audit_bind(uintptr_t value, uintptr_t *refcook, uintptr_t *defcook, const char *name) {
  if (!audit_on || !name) return value;
  if (!strcmp(name, "XScreenCount")) x_screen_count = (int_dpy_t)value;
  else if (!strcmp(name, "XDefaultScreen")) x_default_screen = (int_dpy_t)value;
  else if (!strcmp(name, "XDisplayString")) x_display_string = (str_dpy_t)value;
  else if (!strcmp(name, "glXChooseVisual") || !strcmp(name, "glXGetProcAddress") || !strcmp(name, "glXGetProcAddressARB")) {
    struct link_map *from = (struct link_map *)*refcook, *to = (struct link_map *)*defcook;
    char buf[640];
    snprintf(buf, sizeof(buf), "glxaudit[%d]: %s bound for %s from %s\n", (int)getpid(), name,
             from && from->l_name && *from->l_name ? from->l_name : "the program", to && to->l_name ? to->l_name : "?");
    out(buf);
    if (!strcmp(name, "glXChooseVisual")) { if (!real_choose) real_choose = (choose_t)value; return (uintptr_t)audit_choose_visual; }
    if (!strcmp(name, "glXGetProcAddress")) { real_gpa = (gpa_t)value; return (uintptr_t)audit_gpa; }
    real_gpa_arb = (gpa_t)value;
    return (uintptr_t)audit_gpa_arb;
  }
  return value;
}

#if defined(__LP64__)
uintptr_t la_symbind64(Elf64_Sym *sym, unsigned int ndx, uintptr_t *refcook, uintptr_t *defcook, unsigned int *flags,
                       const char *symname) {
  (void)ndx; (void)flags;
  return audit_bind(sym->st_value, refcook, defcook, symname);
}
#else
uintptr_t la_symbind32(Elf32_Sym *sym, unsigned int ndx, uintptr_t *refcook, uintptr_t *defcook, unsigned int *flags,
                       const char *symname) {
  (void)ndx; (void)flags;
  return audit_bind(sym->st_value, refcook, defcook, symname);
}
#endif
