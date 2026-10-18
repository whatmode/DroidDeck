/* mali-gl: GLX_EXT_texture_from_pixmap the way Steam's GL composer uses it:
 * a client window is redirected with Composite, its backing pixmap named,
 * wrapped in a GLX pixmap and bound as a texture, then drawn and read back.
 * Prints every X error with the extension it belongs to. */
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *x;
static int errors;

static const char *
ext_name(int major)
{
   static char buf[64];
   int n = 0;
   char **list = XListExtensions(x, &n);
   for (int i = 0; i < n; i++) {
      int op, ev, er;
      if (XQueryExtension(x, list[i], &op, &ev, &er) && op == major) {
         snprintf(buf, sizeof(buf), "%s", list[i]);
         XFreeExtensionList(list);
         return buf;
      }
   }
   XFreeExtensionList(list);
   return major < 128 ? "core" : "?";
}

static int
on_error(Display *d, XErrorEvent *e)
{
   char text[128];
   XGetErrorText(d, e->error_code, text, sizeof(text));
   printf("  X error: %s (code %d), request %d (%s) minor %d, resource 0x%lx\n", text,
          e->error_code, e->request_code, ext_name(e->request_code), e->minor_code,
          e->resourceid);
   errors++;
   return 0;
}

static void
try_depth(int depth)
{
   printf("### window depth %d\n", depth);
   XVisualInfo tmpl = {.depth = depth, .class = TrueColor};
   int nvi = 0;
   XVisualInfo *vis = XGetVisualInfo(x, VisualDepthMask | VisualClassMask, &tmpl, &nvi);
   if (!vis) {
      printf("  no visual of depth %d\n", depth);
      return;
   }
   XSetWindowAttributes a = {0};
   a.colormap = XCreateColormap(x, DefaultRootWindow(x), vis[0].visual, AllocNone);
   a.background_pixel = 0;
   a.border_pixel = 0;
   Window client = XCreateWindow(x, DefaultRootWindow(x), 0, 0, 64, 64, 0, depth,
                                 InputOutput, vis[0].visual,
                                 CWColormap | CWBackPixel | CWBorderPixel, &a);
   XMapWindow(x, client);
   XSync(x, False);
   GC gc = XCreateGC(x, client, 0, NULL);
   XSetForeground(x, gc, depth == 32 ? 0xff00ff00 : 0x00ff00);
   XFillRectangle(x, client, gc, 0, 0, 64, 64);

   errors = 0;
   XCompositeRedirectWindow(x, client, CompositeRedirectManual);
   XSync(x, False);
   XFillRectangle(x, client, gc, 0, 0, 64, 64);
   XSync(x, False);
   Pixmap pm = XCompositeNameWindowPixmap(x, client);
   XSync(x, False);
   printf("  NameWindowPixmap: 0x%lx (%d errors)\n", pm, errors);

   /* fbconfig that can bind this depth to a texture */
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
      return;

   int pattr[] = {GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT, GLX_TEXTURE_FORMAT_EXT,
                  depth == 32 ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
                  None};
   errors = 0;
   GLXPixmap gp = glXCreatePixmap(x, cfg, pm, pattr);
   XSync(x, False);
   printf("  glXCreatePixmap: 0x%lx (%d errors)\n", gp, errors);

   /* bind and draw it */
   Window out = XCreateSimpleWindow(x, DefaultRootWindow(x), 100, 0, 64, 64, 0, 0, 0);
   XMapWindow(x, out);
   int wattr[] = {GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                  GLX_DOUBLEBUFFER, True, None};
   GLXFBConfig *wc = glXChooseFBConfig(x, DefaultScreen(x), wattr, &n);
   GLXContext ctx = glXCreateNewContext(x, wc[0], GLX_RGBA_TYPE, NULL, True);
   glXMakeCurrent(x, out, ctx);
   PFNGLXBINDTEXIMAGEEXTPROC bind =
      (PFNGLXBINDTEXIMAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
   GLuint tex;
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   errors = 0;
   if (gp && bind)
      bind(x, gp, GLX_FRONT_LEFT_EXT, NULL);
   XSync(x, False);
   printf("  glXBindTexImageEXT: %d errors, GL error 0x%x\n", errors, glGetError());
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
   glXMakeCurrent(x, None, NULL);
}

int
main(void)
{
   setvbuf(stdout, NULL, _IOLBF, 0);
   x = XOpenDisplay(NULL);
   if (!x)
      return 1;
   XSetErrorHandler(on_error);
   int n = 0;
   char **list = XListExtensions(x, &n);
   printf("extensions:");
   for (int i = 0; i < n; i++) {
      int op, ev, er;
      if (XQueryExtension(x, list[i], &op, &ev, &er))
         printf(" %s=%d", list[i], op);
   }
   printf("\nGLX extensions: %s\n", glXQueryExtensionsString(x, DefaultScreen(x)));
   XFreeExtensionList(list);
   try_depth(24);
   try_depth(32);
   printf("tfptest done\n");
   return 0;
}
