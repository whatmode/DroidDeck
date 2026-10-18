/* mali-gl: which GL contexts can an X11 client get?  Probes GLX and EGL,
 * compatibility/core/ES profiles and 24/32-bit (transparent) windows, the way
 * SDL does for Steam's GL composer. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display *x;
static int x_error;

static int
on_x_error(Display *d, XErrorEvent *e)
{
   (void)d;
   x_error = e->error_code;
   return 0;
}

static Window
make_window(Visual *visual, int depth)
{
   XSetWindowAttributes a = {0};
   a.colormap = XCreateColormap(x, DefaultRootWindow(x), visual, AllocNone);
   a.border_pixel = 0;
   a.background_pixel = 0;
   Window w = XCreateWindow(x, DefaultRootWindow(x), 0, 0, 256, 256, 0, depth,
                            InputOutput, visual,
                            CWColormap | CWBorderPixel | CWBackPixel, &a);
   XMapWindow(x, w);
   XSync(x, False);
   return w;
}

static void
glx_probe(int alpha_visual)
{
   PFNGLXCREATECONTEXTATTRIBSARBPROC create =
      (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB(
         (const GLubyte *)"glXCreateContextAttribsARB");
   int attr[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                 GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                 GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, alpha_visual ? 8 : 0,
                 GLX_DOUBLEBUFFER, True, None};
   int n = 0;
   GLXFBConfig *cfgs = glXChooseFBConfig(x, DefaultScreen(x), attr, &n);
   GLXFBConfig cfg = NULL;
   XVisualInfo *vi = NULL;
   for (int i = 0; i < n; i++) {
      XVisualInfo *v = glXGetVisualFromFBConfig(x, cfgs[i]);
      if (!v)
         continue;
      if (!alpha_visual || v->depth == 32) {
         cfg = cfgs[i];
         vi = v;
         break;
      }
      XFree(v);
   }
   printf("GLX %s window: %d fbconfigs, %s\n", alpha_visual ? "32-bit" : "24-bit",
          n, cfg ? "matching visual found" : "NO matching visual");
   if (!cfg)
      return;
   Window w = make_window(vi->visual, vi->depth);

   static const struct {
      const char *name;
      int attrs[9];
   } tries[] = {
      {"legacy (glXCreateNewContext)", {0}},
      {"compat 2.1", {GLX_CONTEXT_MAJOR_VERSION_ARB, 2, GLX_CONTEXT_MINOR_VERSION_ARB, 1, None}},
      {"compat 3.0", {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 0, None}},
      {"core 3.2", {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 2,
                    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None}},
      {"core 3.3", {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None}},
      {"core 4.1", {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 1,
                    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None}},
      {"ES 2.0", {GLX_CONTEXT_MAJOR_VERSION_ARB, 2, GLX_CONTEXT_MINOR_VERSION_ARB, 0,
                  GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_ES2_PROFILE_BIT_EXT, None}},
      {"ES 3.0", {GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 0,
                  GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_ES2_PROFILE_BIT_EXT, None}},
   };
   for (unsigned i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
      x_error = 0;
      GLXContext c = i == 0 ? glXCreateNewContext(x, cfg, GLX_RGBA_TYPE, NULL, True)
                            : (create ? create(x, cfg, NULL, True, tries[i].attrs) : NULL);
      XSync(x, False);
      int cur = c && glXMakeCurrent(x, w, c);
      const char *ver = cur ? (const char *)glGetString(GL_VERSION) : NULL;
      printf("  GLX %-30s %s%s%s (X error %d)\n", tries[i].name,
             c ? "context" : "NO CONTEXT", cur ? ", current: " : "",
             ver ? ver : "", x_error);
      if (cur)
         glXMakeCurrent(x, None, NULL);
      if (c)
         glXDestroyContext(x, c);
   }
   XDestroyWindow(x, w);
   XFree(vi);
}

static void
egl_probe(int alpha_visual)
{
   PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
   EGLDisplay d = gpd(EGL_PLATFORM_X11_KHR, x, NULL);
   if (!eglInitialize(d, NULL, NULL)) {
      printf("EGL: eglInitialize failed 0x%x\n", eglGetError());
      return;
   }
   static const struct {
      const char *name;
      EGLenum api;
      EGLint bit;
      EGLint attrs[7];
   } tries[] = {
      {"GL compat", EGL_OPENGL_API, EGL_OPENGL_BIT, {EGL_NONE}},
      {"GL core 3.2", EGL_OPENGL_API, EGL_OPENGL_BIT,
       {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE}},
      {"GL core 3.3", EGL_OPENGL_API, EGL_OPENGL_BIT,
       {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE}},
      {"GLES 2", EGL_OPENGL_ES_API, EGL_OPENGL_ES2_BIT, {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_NONE}},
      {"GLES 3", EGL_OPENGL_ES_API, EGL_OPENGL_ES3_BIT, {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE}},
   };
   for (unsigned i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
      EGLint cattr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, tries[i].bit,
                        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                        EGL_ALPHA_SIZE, alpha_visual ? 8 : 0, EGL_NONE};
      EGLConfig cfgs[64];
      EGLint n = 0;
      eglChooseConfig(d, cattr, cfgs, 64, &n);
      EGLConfig cfg = NULL;
      XVisualInfo *vi = NULL;
      for (int k = 0; k < n; k++) {
         EGLint vid = 0;
         eglGetConfigAttrib(d, cfgs[k], EGL_NATIVE_VISUAL_ID, &vid);
         XVisualInfo tmpl = {.visualid = (VisualID)vid};
         int cnt = 0;
         XVisualInfo *v = XGetVisualInfo(x, VisualIDMask, &tmpl, &cnt);
         if (v && (!alpha_visual || v->depth == 32)) {
            cfg = cfgs[k];
            vi = v;
            break;
         }
         if (v)
            XFree(v);
      }
      if (!cfg) {
         printf("  EGL %-14s %s window: %d configs, NO matching visual\n", tries[i].name,
                alpha_visual ? "32-bit" : "24-bit", n);
         continue;
      }
      Window w = make_window(vi->visual, vi->depth);
      eglBindAPI(tries[i].api);
      EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, tries[i].attrs);
      EGLint cerr = eglGetError();
      EGLSurface s = c ? eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)w, NULL) : EGL_NO_SURFACE;
      EGLint serr = eglGetError();
      int cur = s != EGL_NO_SURFACE && eglMakeCurrent(d, s, s, c);
      const char *ver = cur ? (const char *)glGetString(GL_VERSION) : NULL;
      printf("  EGL %-14s %s window: %s, %s%s%s (errors 0x%x/0x%x)\n", tries[i].name,
             alpha_visual ? "32-bit" : "24-bit", c ? "context" : "NO CONTEXT",
             s != EGL_NO_SURFACE ? "surface" : "NO SURFACE", cur ? ", current: " : "",
             ver ? ver : "", cerr, serr);
      eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (s)
         eglDestroySurface(d, s);
      if (c)
         eglDestroyContext(d, c);
      XDestroyWindow(x, w);
      XFree(vi);
   }
   eglTerminate(d);
}

int
main(void)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   x = XOpenDisplay(NULL);
   if (!x) {
      printf("no X display\n");
      return 1;
   }
   XSetErrorHandler(on_x_error);
   printf("PAN_MESA_DEBUG=%s\n", getenv("PAN_MESA_DEBUG") ? getenv("PAN_MESA_DEBUG") : "");
   printf("GLX server: %s %s\nGLX client: %s\n",
          glXQueryServerString(x, DefaultScreen(x), GLX_VENDOR),
          glXQueryServerString(x, DefaultScreen(x), GLX_VERSION),
          glXGetClientString(x, GLX_VERSION));
   glx_probe(0);
   glx_probe(1);
   egl_probe(0);
   egl_probe(1);
   printf("glprobe done\n");
   return 0;
}
