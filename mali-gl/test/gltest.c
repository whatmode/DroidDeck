/* mali-gl Phase 1 test: GLES2 through GBM + EGL on /dev/mali0, offscreen.
 * Clears an FBO, draws a triangle, reads pixels back and times a few frames.
 * Exit code 0 = all checks passed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define W 256
#define H 256

static int failures;

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
   GLint ok = 0;
   glShaderSource(s, 1, &src, NULL);
   glCompileShader(s);
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[1024];
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      printf("shader log: %s\n", log);
   }
   return s;
}

static int
pixel_is(const uint8_t *p, int r, int g, int b)
{
   return abs(p[0] - r) < 3 && abs(p[1] - g) < 3 && abs(p[2] - b) < 3 &&
          p[3] > 250;
}

int
main(void)
{
   const char *node = getenv("MESA_KBASE_DEVICE");
   if (!node || !node[0])
      node = "/dev/mali0";

   setvbuf(stdout, NULL, _IOLBF, 0);
   install_crash_handler();
   printf("mali-gl gltest: opening %s\n", node);
   int fd = open(node, O_RDWR | O_CLOEXEC);
   check(fd >= 0, "open GPU node");
   if (fd < 0) {
      perror("open");
      return 1;
   }

   struct gbm_device *gbm = gbm_create_device(fd);
   check(gbm != NULL, "gbm_create_device");
   if (!gbm)
      return 1;
   printf("gbm backend: %s\n", gbm_device_get_backend_name(gbm));

   PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
         "eglGetPlatformDisplayEXT");
   EGLDisplay dpy = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
   EGLint major = 0, minor = 0;
   EGLBoolean init = dpy != EGL_NO_DISPLAY && eglInitialize(dpy, &major, &minor);
   check(init, "eglInitialize on GBM");
   if (!init) {
      printf("eglGetError 0x%x\n", eglGetError());
      return 1;
   }
   printf("EGL %d.%d vendor '%s' version '%s'\n", major, minor,
          eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION));
   const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
   printf("EGL extensions: %s\n", exts);

   eglBindAPI(EGL_OPENGL_ES_API);
   const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
   EGLContext ctx =
      eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
   check(ctx != EGL_NO_CONTEXT, "eglCreateContext (GLES2, no config)");
   if (ctx == EGL_NO_CONTEXT) {
      printf("eglGetError 0x%x\n", eglGetError());
      return 1;
   }
   int current = eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
   check(current, "eglMakeCurrent surfaceless");
   if (!current)
      return 1;

   printf("GL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\n",
          glGetString(GL_VENDOR), glGetString(GL_RENDERER),
          glGetString(GL_VERSION));

   GLuint rb, fb;
   glGenRenderbuffers(1, &rb);
   glBindRenderbuffer(GL_RENDERBUFFER, rb);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, W, H);
   glGenFramebuffers(1, &fb);
   glBindFramebuffer(GL_FRAMEBUFFER, fb);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, rb);
   check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "framebuffer complete");
   glViewport(0, 0, W, H);

   static uint8_t px[W * H * 4];

   /* 1: clear (fragment job only) */
   glClearColor(1, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
   check(glGetError() == GL_NO_ERROR, "clear + readpixels without GL error");
   printf("clear pixel: %u %u %u %u\n", px[0], px[1], px[2], px[3]);
   check(pixel_is(px, 255, 0, 0), "clear colour reads back red");

   /* 2: draw (vertex + tiler + fragment) */
   GLuint prog = glCreateProgram();
   glAttachShader(prog, shader(GL_VERTEX_SHADER,
                               "attribute vec2 p;\n"
                               "void main() { gl_Position = vec4(p, 0.0, 1.0); }\n"));
   glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
                               "precision mediump float;\n"
                               "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n"));
   glBindAttribLocation(prog, 0, "p");
   glLinkProgram(prog);
   GLint linked = 0;
   glGetProgramiv(prog, GL_LINK_STATUS, &linked);
   check(linked, "link shader program");
   glUseProgram(prog);

   static const float tri[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f};
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
   glEnableVertexAttribArray(0);

   glClearColor(0, 0, 1, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   printf("step: glDrawArrays\n");
   glDrawArrays(GL_TRIANGLES, 0, 3);
   printf("step: glReadPixels after draw\n");
   glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
   const uint8_t *centre = px + ((H / 2) * W + W / 2) * 4;
   const uint8_t *corner = px + ((H - 2) * W + 1) * 4;
   printf("centre pixel: %u %u %u %u, corner pixel: %u %u %u %u\n", centre[0],
          centre[1], centre[2], centre[3], corner[0], corner[1], corner[2],
          corner[3]);
   check(pixel_is(centre, 0, 255, 0), "triangle covers the centre (green)");
   check(pixel_is(corner, 0, 0, 255), "corner keeps the clear colour (blue)");

   /* 3: throughput (each frame is submitted and waited for) */
   const int frames = 120;
   double t0 = now_s();
   for (int i = 0; i < frames; i++) {
      glClearColor((i & 1) ? 1 : 0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
   }
   double dt = now_s() - t0;
   printf("%d frames in %.3f s = %.1f fps (%.2f ms/frame)\n", frames, dt,
          frames / dt, dt * 1000.0 / frames);
   check(glGetError() == GL_NO_ERROR, "frame loop without GL error");

   eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   eglDestroyContext(dpy, ctx);
   eglTerminate(dpy);
   gbm_device_destroy(gbm);
   close(fd);

   printf("%s (%d failure%s)\n", failures ? "RESULT: FAIL" : "RESULT: PASS",
          failures, failures == 1 ? "" : "s");
   return failures ? 1 : 0;
}
