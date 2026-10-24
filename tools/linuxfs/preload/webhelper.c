#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#ifndef WEBHELPER_DEFAULT
#define WEBHELPER_DEFAULT "--no-zygote --in-process-gpu -AudioServiceOutOfProcess"
#endif

static int is_webhelper_browser(const char *path, char *const argv[]) {
  if (!path || !argv) return 0;
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if (strcmp(base, "steamwebhelper")) return 0;
  for (int i = 1; argv[i]; i++)
    if (!strncmp(argv[i], "--type=", 7)) return 0;
  return 1;
}

static char *join_features(const char *have, const char *add) {
  size_t a = have ? strlen(have) : 0, b = strlen(add);
  char *out = malloc(a + b + 2);
  if (!out) return NULL;
  out[0] = 0;
  if (a) strcat(strcat(out, have), ",");
  return strcat(out, add);
}

__attribute__((visibility("hidden"))) char *const *bl_webhelper_argv(const char *path, char *const argv[]) {
  if (!is_webhelper_browser(path, argv)) return argv;
  const char *spec = getenv("BL_WEBHELPER_ARGS");
  if (!spec || !*spec) spec = WEBHELPER_DEFAULT;
  if (!strcmp(spec, "stock")) return argv;

  int argc = 0;
  while (argv[argc]) argc++;
  char *tokens = strdup(spec);
  if (!tokens) return argv;
  int extra = 0;
  for (char *c = tokens; *c; c++)
    if (*c == ' ') extra++;
  char **out = calloc(argc + extra + 4, sizeof *out);
  if (!out) return argv;

  const char *enable = NULL, *disable = NULL;
  int n = 0;
  for (int i = 0; i < argc; i++) {
    if (i && !strncmp(argv[i], "--enable-features=", 18)) enable = argv[i] + 18;
    else if (i && !strncmp(argv[i], "--disable-features=", 19)) disable = argv[i] + 19;
    else out[n++] = argv[i];
  }
  char *add_enable = NULL, *add_disable = NULL, *save = NULL;
  for (char *t = strtok_r(tokens, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
    if (t[0] == '+' && t[1]) {
      char *joined = join_features(add_enable, t + 1);
      free(add_enable);
      add_enable = joined;
    } else if (t[0] == '-' && t[1] && t[1] != '-') {
      char *joined = join_features(add_disable, t + 1);
      free(add_disable);
      add_disable = joined;
    } else if (!strncmp(t, "--", 2)) {
      out[n++] = t;
    }
  }
  char *merged;
  if (add_enable && (merged = join_features(enable, add_enable))) enable = merged;
  if (add_disable && (merged = join_features(disable, add_disable))) disable = merged;
  char *flag;
  if (enable && (flag = malloc(strlen(enable) + 19))) out[n++] = strcat(strcpy(flag, "--enable-features="), enable);
  if (disable && (flag = malloc(strlen(disable) + 20))) out[n++] = strcat(strcpy(flag, "--disable-features="), disable);
  out[n] = NULL;
  return out;
}

/* Steam's launcher exports LIBGL_KOPPER_DISABLE=true for steamwebhelper. Without Kopper, Zink
 * presents to X11 only through DRI3, which a glamor-less Xwayland (-shm, the Venus bridge's
 * one-driver mode) lacks: the webhelper's GL composer gets no swapchain ("zink: could not
 * create swapchain") and dies, and Steam restarts it every ~10 s. With Kopper, Zink presents
 * through Vulkan WSI, which the gamescope WSI layer hands to gamescope directly.
 * BL_WEBHELPER_KOPPER=1 drops the variable inside the webhelper. */
__attribute__((constructor)) static void bl_webhelper_kopper(void) {
  const char *want = getenv("BL_WEBHELPER_KOPPER");
  if (!want || strcmp(want, "1")) return;
  if (!program_invocation_short_name || strcmp(program_invocation_short_name, "steamwebhelper")) return;
  unsetenv("LIBGL_KOPPER_DISABLE");
}

/* BL_WEBHELPER_STDERR=<file>: every steamwebhelper process appends its stderr there. Steam
 * drops the webhelper's output, so a webhelper that dies at start (Mesa/Zink errors, Chromium's
 * own crash report) otherwise leaves nothing behind. */
__attribute__((constructor)) static void bl_webhelper_stderr(void) {
  const char *file = getenv("BL_WEBHELPER_STDERR");
  if (!file || !*file) return;
  if (!program_invocation_short_name || strcmp(program_invocation_short_name, "steamwebhelper")) return;
  int fd = open(file, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) return;
  char head[256];
  char type[64] = "browser";
  int in = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
  if (in >= 0) {
    char buf[4096];
    ssize_t n = read(in, buf, sizeof(buf) - 1);
    close(in);
    if (n > 0) {
      buf[n] = 0;
      for (char *p = buf; p < buf + n; p += strlen(p) + 1)
        if (!strncmp(p, "--type=", 7)) { snprintf(type, sizeof(type), "%s", p + 7); break; }
    }
  }
  int len = snprintf(head, sizeof(head), "==== steamwebhelper pid %d (%s) started at %ld\n", (int)getpid(), type, (long)time(NULL));
  if (len > 0 && write(fd, head, (size_t)len) < 0) { }
  dup2(fd, 2);
  close(fd);
}
