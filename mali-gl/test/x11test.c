/* mali-gl Phase 3 test: an X11 GLES client on Xwayland.
 * Checks that Xwayland offers DRI3, that EGL on X11 picks the Mali GPU,
 * draws a triangle in a window, presents it, and that the X server sees the
 * GPU's pixels (XGetImage).  Exit code 0 = all checks passed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <xcb/dri3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define W 320
#define H 240

static int failures;

static void
check(int ok, const char *what)
{
   printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
   fflush(stdout);
   if (!ok)
      failures++;
}

/* Print a symbolizable backtrace (library + offset) on a crash. */
static void
crash_handler(int sig, siginfo_t *si, void *uc)
{
   void *bt[64];
   int n = backtrace(bt, 64);

   (void)uc;
   fprintf(stderr, "CRASH: signal %d, fault address %p\n", sig, si->si_addr);
   for (int i = 0; i < n; i++) {
      Dl_info d;
      if (dladdr(bt[i], &d) && d.dli_fname)
         fprintf(stderr, "  #%d %s+0x%lx %s\n", i, d.dli_fname,
                 (unsigned long)((char *)bt[i] - (char *)d.dli_fbase),
                 d.dli_sname ? d.dli_sname : "");
      else
         fprintf(stderr, "  #%d %p\n", i, bt[i]);
   }
   fflush(stderr);
   _exit(128 + sig);
}

static void
install_crash_handler(void)
{
   struct sigaction sa = {0};
   sa.sa_sigaction = crash_handler;
   sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
   sigaction(SIGSEGV, &sa, NULL);
   sigaction(SIGBUS, &sa, NULL);
   sigaction(SIGABRT, &sa, NULL);
   sigaction(SIGILL, &sa, NULL);
}

static void
check(int ok, const char *what)
{
   printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
   fflush(stdout);
   if (!ok)
      failures++;
}

static double
now_s(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec + ts.tv_nsec / 1e9;
}

static GLuint
shader(GLenum type, const char *src)
{
   GLuint s = glCreateShader(type);
   glShaderSource(s, 1, &src, NULL);
   glCompileShader(s);
   return s;
}

int
main(void)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   install_crash_handler();
   Display *x = XOpenDisplay(NULL);
   check(x != NULL, "connect to the X server");
   if (!x)
      return 1;
   printf("X server: %s %d\n", ServerVendor(x), VendorRelease(x));

   xcb_connection_t *c = XGetXCBConnection(x);
   const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_dri3_id);
   xcb_dri3_query_version_reply_t *dv = NULL;
   if (ext && ext->present)
      dv = xcb_dri3_query_version_reply(c, xcb_dri3_query_version(c, 1, 2), NULL);
   check(dv != NULL, "X server offers DRI3");
   if (dv) {
      printf("DRI3 %u.%u\n", dv->major_version, dv->minor_version);
      free(dv);
   }

   Window win = XCreateSimpleWindow(x, DefaultRootWindow(x), 0, 0, W, H, 0, 0, 0);
   XStoreName(x, win, "mali-gl x11test");
   XMapWindow(x, win);
   XSync(x, False);

   PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
   EGLDisplay dpy = get_platform_display(EGL_PLATFORM_X11_KHR, x, NULL);
   EGLint maj, min;
   int ok = dpy != EGL_NO_DISPLAY && eglInitialize(dpy, &maj, &min);
   check(ok, "eglInitialize on X11");
   if (!ok) {
      printf("eglGetError 0x%x\n", eglGetError());
      return 1;
   }

   const EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                              EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                              EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
   EGLConfig cfg;
   EGLint n = 0;
   eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
   check(n > 0, "EGL window config");
   if (!n)
      return 1;
   eglBindAPI(EGL_OPENGL_ES_API);
   const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
   EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)win, NULL);
   check(ctx != EGL_NO_CONTEXT && surf != EGL_NO_SURFACE, "context + window surface");
   if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE) {
      printf("eglGetError 0x%x\n", eglGetError());
      return 1;
   }
   eglMakeCurrent(dpy, surf, surf, ctx);
   const char *renderer = (const char *)glGetString(GL_RENDERER);
   printf("GL_RENDERER: %s\nGL_VERSION: %s\n", renderer, glGetString(GL_VERSION));
   check(renderer && strstr(renderer, "Mali"), "GL runs on the Mali GPU");

   GLuint prog = glCreateProgram();
   glAttachShader(prog, shader(GL_VERTEX_SHADER,
                               "attribute vec2 p;\nvoid main(){gl_Position=vec4(p,0.0,1.0);}\n"));
   glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
                               "precision mediump float;\nvoid main(){gl_FragColor=vec4(0.0,1.0,0.0,1.0);}\n"));
   glBindAttribLocation(prog, 0, "p");
   glLinkProgram(prog);
   glUseProgram(prog);
   static const float tri[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f};
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, W, H);

   double t0 = now_s();
   int frames = 0;
   for (; frames < 300 && now_s() - t0 < 5.0; frames++) {
      glClearColor(0, 0, 1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (!eglSwapBuffers(dpy, surf)) {
         printf("eglSwapBuffers failed: 0x%x\n", eglGetError());
         break;
      }
   }
   double dt = now_s() - t0;
   printf("%d presented frames in %.2f s = %.1f fps\n", frames, dt, frames / dt);
   check(frames >= 30, "presenting frames through Xwayland");

   /* One last frame, then ask the X server for the window contents. */
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   eglSwapBuffers(dpy, surf);
   eglWaitGL();
   XSync(x, False);
   usleep(200000);
   XImage *img = XGetImage(x, win, 0, 0, W, H, AllPlanes, ZPixmap);
   check(img != NULL, "XGetImage of the window");
   if (img) {
      unsigned long centre = XGetPixel(img, W / 2, H / 2);
      unsigned long corner = XGetPixel(img, 2, 2);
      printf("X server sees centre 0x%06lx, corner 0x%06lx\n", centre & 0xffffff,
             corner & 0xffffff);
      check((centre & 0xffffff) == 0x00ff00, "X server has the GPU frame (centre green)");
      check((corner & 0xffffff) == 0x0000ff, "X server has the GPU frame (corner blue)");
      XDestroyImage(img);
   }

   printf("%s (%d failure%s)\n", failures ? "RESULT: FAIL" : "RESULT: PASS",
          failures, failures == 1 ? "" : "s");
   printf("step: teardown\n");
   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroySurface(dpy, surf);
   eglDestroyContext(dpy, ctx);
   printf("step: eglTerminate\n");
   eglTerminate(dpy);
   XCloseDisplay(x);
   printf("step: teardown done\n");
   return failures ? 1 : 0;
}
