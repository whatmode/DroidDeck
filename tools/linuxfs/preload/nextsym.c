#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The real function behind one of the preload's wrappers. RTLD_NEXT only searches the global
 * scope; a library a program opened with RTLD_LOCAL (Steam's client loads its X libraries
 * that way) is not in it, and a wrapper that found nothing used to fail every call quietly:
 * Steam's xcb requests were never sent, so GL found no GLX on its displays. So also look in
 * the named library itself when it is loaded. */
__attribute__((visibility("hidden"))) void *bl_next_symbol(const char *library, const char *name) {
  void *fn = dlsym(RTLD_NEXT, name);
  if (fn || !library) return fn;
  void *handle = dlopen(library, RTLD_LAZY | RTLD_NOLOAD);
  if (handle) {
    fn = dlsym(handle, name);
    dlclose(handle);
  }
  if (!fn) {
    char msg[256];
    int n = snprintf(msg, sizeof(msg), "droiddeck preload: no %s to pass %s on to (pid %d)\n", library, name, (int)getpid());
    if (n > 0 && write(2, msg, (size_t)n) < 0) { }
  }
  return fn;
}
