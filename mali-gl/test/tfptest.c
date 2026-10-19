/* mali-gl: GLX_EXT_texture_from_pixmap the way Steam's GL composer uses it:
 * a client window's Composite backing pixmap is named, wrapped in a GLX
 * pixmap and bound as a texture, then drawn and read back.
 * Every X error is printed with the extension it belongs to.  The error
 * handler must not talk to the server (Xlib deadlocks), so extension names
 * are looked up once at start. */
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static Display *x;
static int errors;
static char ext_names[256][32];
static const char *step = "start";

static void
on_alarm(int sig)
{
   (void)sig;
   char buf[160];
   int n = snprintf(buf, sizeof(buf), "  HANG: no progress for 20 s at step '%s'\n", step);
   write(1, buf, n);
   _exit(3);
}

static int
on_error(Display *d, XErrorEvent *e)
{
   char text[128];
   XGetErrorText(d, e->error_code, text, sizeof(text));
   printf("  X error at '%s': %s (code %d), request %d (%s) minor %d, resource 0x%lx\n", step,
          text, e->error_code, e->request_code,
          e->request_code < 128 ? "core" : ext_names[e->request_code], e->minor_code,
          e->resourceid);
   errors++;
   return 0;
}

#define STEP(s) do { step = (s); alarm(20); } while (0)

static void
fill(Window w, int depth)
{
   GC gc = XCreateGC(x, w, 0, NULL);
   XSetForeground(x, gc, depth == 32 ? 0xff00ff00 : 0x00ff00);
   XFillRectangle(x, w, gc, 0, 0, 64, 64);
   XFreeGC(x, gc);
}

static const char *
map_state(Window w)
{
   XWindowAttributes wa;
   if (!XGetWindowAttributes(x, w, &wa))
      return "?";
   return wa.map_state == IsViewable ? "viewable" : wa.map_state == IsUnviewable ? "unviewable" : "unmapped";
}

static void
wait_viewable(Window w)
{
   for (int i = 0; i < 40; i++) {
      XWindowAttributes wa;
      if (XGetWindowAttributes(x, w, &wa) && wa.map_state == IsViewable)
         return;
      usleep(50000);
   }
}

/* mode 0: top-level, rely on gamescope's redirect; 1: top-level + our
 * automatic redirect; 2: child of a top-level + our automatic redirect;
 * 3: override-redirect top-level (as CEF popups) */
static void
try_case(int depth, int mode)
{
   static const char *names[] = {"top-level (gamescope redirect)", "top-level + own automatic redirect",
                                 "child window + own automatic redirect", "override-redirect top-level"};
   printf("### depth %d, %s\n", depth, names[mode]);
   XVisualInfo tmpl = {.depth = depth, .class = TrueColor};
   int nvi = 0;
   XVisualInfo *vis = XGetVisualInfo(x, VisualDepthMask | VisualClassMask, &tmpl, &nvi);
   if (!vis) {
      printf("  no visual of depth %d\n", depth);
      return;
   }
   STEP("create window");
   errors = 0;
   XSetWindowAttributes a = {0};
   a.colormap = XCreateColormap(x, DefaultRootWindow(x), vis[0].visual, AllocNone);
   a.override_redirect = mode == 3;
   Window parent = DefaultRootWindow(x), top = 0;
   if (mode == 2) {
      top = XCreateSimpleWindow(x, parent, 0, 0, 64, 64, 0, 0, 0);
      XMapWindow(x, top);
      XSync(x, False);
      wait_viewable(top);
      parent = top;
   }
   Window client = XCreateWindow(x, parent, 0, 0, 64, 64, 0, depth, InputOutput, vis[0].visual,
                                 CWColormap | CWBackPixel | CWBorderPixel | CWOverrideRedirect, &a);
   STEP("map window");
   XMapWindow(x, client);
   XSync(x, False);
   wait_viewable(client);
   printf("  window 0x%lx is %s\n", client, map_state(client));
   if (mode == 1 || mode == 2) {
      STEP("XCompositeRedirectWindow(Automatic)");
      XCompositeRedirectWindow(x, client, CompositeRedirectAutomatic);
      XSync(x, False);
   }
   STEP("draw into window");
   fill(client, depth);
   XSync(x, False);
   STEP("XCompositeNameWindowPixmap");
   int before = errors;
   Pixmap pm = XCompositeNameWindowPixmap(x, client);
   XSync(x, False);
   if (errors != before)
      pm = 0;
   printf("  NameWindowPixmap: %s\n", pm ? "ok" : "FAILED");
   if (!pm)
      goto out;

   STEP("glXChooseFBConfig (pixmap)");
   int attr[] = {GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
                 depth == 32 ? GLX_BIND_TO_TEXTURE_RGBA_EXT : GLX_BIND_TO_TEXTURE_RGB_EXT, True,
                 GLX_X_RENDERABLE, True, None};
   int n = 0;
   GLXFBConfig *cfgs = glXChooseFBConfig(x, DefaultScreen(x), attr, &n);
   GLXFBConfig cfg = NULL;
   for (int i = 0; i < n && !cfg; i++) {
      XVisualInfo *v = glXGetVisualFromFBConfig(x, cfgs[i]);
      if (v && v->depth == depth)
         cfg = cfgs[i];
      if (v)
         XFree(v);
   }
   printf("  %d texture-from-pixmap fbconfigs, %s for depth %d\n", n,
          cfg ? "one matches" : "NONE matches", depth);
   if (!cfg)
      goto out;

   STEP("glXCreatePixmap");
   int pattr[] = {GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT, GLX_TEXTURE_FORMAT_EXT,
                  depth == 32 ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT, None};
   before = errors;
   GLXPixmap gp = glXCreatePixmap(x, cfg, pm, pattr);
   XSync(x, False);
   printf("  glXCreatePixmap: %s\n", gp && errors == before ? "ok" : "FAILED");

   STEP("GL context");
   Window out = XCreateSimpleWindow(x, DefaultRootWindow(x), 100, 0, 64, 64, 0, 0, 0);
   XMapWindow(x, out);
   int wattr[] = {GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                  GLX_DOUBLEBUFFER, True, None};
   GLXFBConfig *wc = glXChooseFBConfig(x, DefaultScreen(x), wattr, &n);
   GLXContext ctx = glXCreateNewContext(x, wc[0], GLX_RGBA_TYPE, NULL, True);
   glXMakeCurrent(x, out, ctx);
   PFNGLXBINDTEXIMAGEEXTPROC bind =
      (PFNGLXBINDTEXIMAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
   PFNGLXRELEASETEXIMAGEEXTPROC release =
      (PFNGLXRELEASETEXIMAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");
   GLuint tex;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   STEP("glXBindTexImageEXT");
   before = errors;
   if (gp && bind)
      bind(x, gp, GLX_FRONT_LEFT_EXT, NULL);
   XSync(x, False);
   printf("  glXBindTexImageEXT: %s, GL error 0x%x\n", errors == before ? "ok" : "FAILED",
          glGetError());
   STEP("draw + glReadPixels");
   glViewport(0, 0, 64, 64);
   glClearColor(1, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_TEXTURE_2D);
   glBegin(GL_QUADS);
   glTexCoord2f(0, 0); glVertex2f(-1, -1);
   glTexCoord2f(1, 0); glVertex2f(1, -1);
   glTexCoord2f(1, 1); glVertex2f(1, 1);
   glTexCoord2f(0, 1); glVertex2f(-1, 1);
   glEnd();
   unsigned char px[4] = {0};
   glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
   printf("  drawn texture pixel: %u %u %u %u -> %s\n", px[0], px[1], px[2], px[3],
          (px[1] > 200 && px[0] < 50) ? "PASS (client's green)" : "FAIL");
   STEP("release + teardown");
   if (gp && release)
      release(x, gp, GLX_FRONT_LEFT_EXT);
   glXMakeCurrent(x, None, NULL);
   glXDestroyContext(x, ctx);
   if (gp)
      glXDestroyPixmap(x, gp);
   XDestroyWindow(x, out);
out:
   STEP("destroy window");
   if (pm)
      XFreePixmap(x, pm);
   XDestroyWindow(x, client);
   if (top)
      XDestroyWindow(x, top);
   XSync(x, False);
   alarm(0);
}

int
main(void)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   signal(SIGALRM, on_alarm);
   x = XOpenDisplay(NULL);
   if (!x)
      return 1;
   int n = 0;
   char **list = XListExtensions(x, &n);
   printf("extensions:");
   for (int i = 0; i < n; i++) {
      int op, ev, er;
      if (XQueryExtension(x, list[i], &op, &ev, &er) && op < 256) {
         snprintf(ext_names[op], sizeof(ext_names[op]), "%s", list[i]);
         printf(" %s=%d", list[i], op);
      }
   }
   XFreeExtensionList(list);
   XSetErrorHandler(on_error);
   printf("\nGLX extensions: %s\n", glXQueryExtensionsString(x, DefaultScreen(x)));
   for (int mode = 0; mode < 4; mode++)
      try_case(24, mode);
   for (int mode = 0; mode < 4; mode++)
      try_case(32, mode);
   printf("tfptest done\n");
   return 0;
}
