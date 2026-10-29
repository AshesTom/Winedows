/* w7-dwm: the Windows 7 session's desktop compositor, for Aero glass.
 *
 * Windows 7's desktop window manager draws every window off screen and blurs
 * what is behind glass. Wine marks its glass windows with _WIN4WINE_GLASS
 * (patches 0152 and 0153): ARGB X windows that already hold the window color
 * and the glass's reflection under their pixels. This program redirects the
 * screen's top-level windows (Composite), draws them with OpenGL ES 2 through
 * EGL, blurs behind each glass window (dual Kawase: a few downsampling and
 * upsampling passes, cheap on a GPU) and honours _NET_WM_WINDOW_OPACITY
 * (Aero Peek) and the windows' bounding shape (rounded corners), and casts the
 * soft shadow Windows 7 gives the windows with an Aero frame, and animates
 * the application windows as Windows 7's DWM does: they open and close with a
 * slight 3D tilt and fade, and minimize into the taskbar and come back from
 * it. Maximized or restored, a window changes at once, as on Windows 7.
 *
 * A window's pixmap becomes a texture without copying when EGL can import X
 * pixmaps (a GPU X server: Xorg, rootful or rootless Xwayland); otherwise
 * (Xephyr, Xvfb) its damaged part is read with MIT-SHM and uploaded.
 *
 * Usage: w7-dwm [--blur LEVELS] [--offset PIXELS]
 *   W7DWM_DEBUG=1 prints the path it took and frame times. */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/shape.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#define MAX_LEVELS 5
#define FRAME_NS (1000000000LL / 60)

typedef struct win
{
    Window id;
    int x, y, width, height, border;
    int depth;
    Visual *visual;
    int mapped;
    Damage damage;
    int damaged;
    /* the window's contents */
    Pixmap pixmap;
    EGLImageKHR image;
    GLuint texture;
    int texture_width, texture_height;
    int stale;          /* the pixmap must be named again (mapped or resized) */
    /* MIT-SHM upload when EGL cannot import the pixmap */
    XShmSegmentInfo shm;
    int shm_size;
    XRectangle upload;  /* damaged area still to upload, in pixmap coordinates */
    int upload_all;
    /* how it is drawn */
    int glass;
    int shell_kind; /* 1: Start menu, 2: taskbar, 3: Start button */
    int override;       /* override-redirect: menus, the Start button, tooltips */
    double opacity;
    XRectangle *rects;  /* bounding shape, relative to the outer corner */
    int nrects;
    /* animation */
    int anim;               /* ANIM_*, or 0 */
    long long anim_start;
    long long anim_length;
    int minimized;          /* unmapped by the window manager's minimize */
    float button[4];        /* its taskbar button (_NET_WM_ICON_GEOMETRY), left top right bottom */
    int has_button;
    int destroyed;          /* destroyed while it animated: freed at its end */
    /* its contents when it last stayed unchanged a moment: Wine shapes and
     * draws a window for its new size before the window manager resizes it,
     * and it shows as it was until then */
    GLuint old_texture;
    int settled;            /* the snapshot has its contents since its last damage */
    int animates;           /* animatable(), as of its mapping */
    long long last_damage;
    long long frozen;       /* shown as its snapshot from then until it is resized */
} win;

enum
{
    ANIM_OPEN = 1,
    ANIM_CLOSE,
    ANIM_MINIMIZE,
    ANIM_RESTORE,   /* from minimized */
};

/* Windows 7's durations, about: the window opens in a quarter of a second
 * and closes a little faster. */
#define OPEN_NS (250 * 1000000LL)
#define CLOSE_NS (200 * 1000000LL)
#define MINIMIZE_NS (250 * 1000000LL)

static Display *dpy;
static Window root, overlay;
static int screen_width, screen_height;
static int damage_event, shape_event;
static win **wins;          /* bottom to top */
static int nwins, capacity;
static int dirty = 1;
static int debug;
/* About a Gaussian blur of 3 pixels' deviation, Windows 7's (the value
 * OpenGlass's documentation gives for its Direct3D renderer). */
static int levels = 2;
static float offset = 1.5f;
static volatile sig_atomic_t quit;

static Atom atom_glass, atom_opacity, atom_rootpmap, atom_esetroot, atom_class;
static Atom atom_wm_state, atom_icon_geometry;
static Atom atom_type, atom_type_normal, atom_type_dialog;

static EGLDisplay egl;
static EGLSurface surface;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture;
static int import_pixmaps;  /* EGL_KHR_image_pixmap works on this server */

static GLuint draw_program, down_program, up_program;
static GLint draw_screen, draw_flip, draw_alpha, draw_opaque, draw_swap, draw_clamp;
static GLint down_screen, down_half, up_screen, up_half;
static GLuint mask_program;
static GLint mask_screen, mask_half, mask_size;
static GLuint shadow_program;
static GLuint anim_program;
static GLint anim_screen, anim_alpha, anim_opaque, anim_swap;
static GLint shadow_screen, shadow_box;
static GLuint scene_fbo, scene_texture;
static GLuint level_fbo[MAX_LEVELS + 1], level_texture[MAX_LEVELS + 1];
static int level_width[MAX_LEVELS + 1], level_height[MAX_LEVELS + 1];

static Pixmap root_pixmap;
static win background;  /* the root pixmap (_XROOTPMAP_ID) as a texture */

static void trace(const char *format, ...)
{
    va_list args;
    if (!debug) return;
    va_start(args, format);
    fprintf(stderr, "w7-dwm: ");
    vfprintf(stderr, format, args);
    va_end(args);
}

static int ignore_errors(Display *display, XErrorEvent *event)
{
    (void)display;
    trace("X error %d (request %d.%d) on 0x%lx\n", event->error_code, event->request_code,
          event->minor_code, event->resourceid);
    return 0;
}

static long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- OpenGL ---- */

static const char *vertex_source =
    "attribute vec2 pos;\n"
    "attribute vec2 uv;\n"
    "uniform vec2 screen;\n"
    "uniform float flip;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    v = uv;\n"
    "    gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, flip * (pos.y / screen.y * 2.0 - 1.0), 0.0, 1.0);\n"
    "}\n";

static const char *draw_source =
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform float alpha;\n"
    "uniform float opaque;\n"
    "uniform float swap;\n"
    "uniform float clamp_alpha;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    vec4 c = texture2D(tex, v);\n"
    "    if (swap > 0.5) c = c.bgra;\n"
    "    if (opaque > 0.5) c.a = 1.0;\n"
    "    if (clamp_alpha > 0.5) c.rgb = min(c.rgb, vec3(c.a));\n"
    "    gl_FragColor = c * alpha;\n"
    "}\n";

static const char *down_source =
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 spread;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    vec4 s = texture2D(tex, v) * 4.0;\n"
    "    s += texture2D(tex, v - spread);\n"
    "    s += texture2D(tex, v + spread);\n"
    "    s += texture2D(tex, v + vec2(spread.x, -spread.y));\n"
    "    s += texture2D(tex, v - vec2(spread.x, -spread.y));\n"
    "    gl_FragColor = s / 8.0;\n"
    "}\n";

static const char *up_source =
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 spread;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    vec4 s = texture2D(tex, v + vec2(-spread.x * 2.0, 0.0));\n"
    "    s += texture2D(tex, v + vec2(-spread.x, spread.y)) * 2.0;\n"
    "    s += texture2D(tex, v + vec2(0.0, spread.y * 2.0));\n"
    "    s += texture2D(tex, v + vec2(spread.x, spread.y)) * 2.0;\n"
    "    s += texture2D(tex, v + vec2(spread.x * 2.0, 0.0));\n"
    "    s += texture2D(tex, v + vec2(spread.x, -spread.y)) * 2.0;\n"
    "    s += texture2D(tex, v + vec2(0.0, -spread.y * 2.0));\n"
    "    s += texture2D(tex, v + vec2(-spread.x, -spread.y)) * 2.0;\n"
    "    gl_FragColor = s / 12.0;\n"
    "}\n";

/* The blur's last pass, into the scene under a glass window: the blurred
 * pixels only where the window has any (its transparent rounded corners
 * keep what is behind them as it is). */
static const char *mask_source =
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D mask;\n"
    "uniform vec2 spread;\n"
    "uniform vec2 size;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    vec2 u = gl_FragCoord.xy / size;\n"
    "    vec4 s = texture2D(tex, u + vec2(-spread.x * 2.0, 0.0));\n"
    "    s += texture2D(tex, u + vec2(-spread.x, spread.y)) * 2.0;\n"
    "    s += texture2D(tex, u + vec2(0.0, spread.y * 2.0));\n"
    "    s += texture2D(tex, u + vec2(spread.x, spread.y)) * 2.0;\n"
    "    s += texture2D(tex, u + vec2(spread.x * 2.0, 0.0));\n"
    "    s += texture2D(tex, u + vec2(spread.x, -spread.y)) * 2.0;\n"
    "    s += texture2D(tex, u + vec2(0.0, -spread.y * 2.0));\n"
    "    s += texture2D(tex, u + vec2(-spread.x, -spread.y)) * 2.0;\n"
    "    float w = smoothstep(0.0, 0.06, texture2D(mask, v).a);\n"
    "    gl_FragColor = vec4(s.rgb / 12.0, 1.0) * w;\n"
    "}\n";

/* The shadow around a window's rounded rectangle (box: left, top, right,
 * bottom, in pixels): a Gaussian falloff from its edge, none under it. */
static const char *shadow_source =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "uniform vec4 box;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    vec2 c = (box.xy + box.zw) * 0.5, h = (box.zw - box.xy) * 0.5;\n"
    "    vec2 q = abs(gl_FragCoord.xy - c) - (h - 6.0);\n"
    "    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 6.0;\n"
    "    if (d < 0.0) discard;\n"
    "    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.45 * exp(-0.5 * d * d / 30.0));\n"
    "}\n";

/* An animated window: its corners' screen positions with the depth of
 * their perspective, which the texture follows (draw_source colours it). */
static const char *anim_vertex_source =
    "attribute vec3 pos;\n"
    "attribute vec2 uv;\n"
    "uniform vec2 screen;\n"
    "varying vec2 v;\n"
    "void main() {\n"
    "    v = uv;\n"
    "    gl_Position = vec4((pos.x / screen.x * 2.0 - 1.0) * pos.z, (pos.y / screen.y * 2.0 - 1.0) * pos.z,\n"
    "                       0.0, pos.z);\n"
    "}\n";

static GLuint shader(GLenum type, const char *source)
{
    GLuint s = glCreateShader(type);
    GLint ok;
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "w7-dwm: shader: %s\n", log);
        exit(1);
    }
    return s;
}

static GLuint program_with(const char *vertex, const char *fragment)
{
    GLuint p = glCreateProgram();
    GLint ok;
    glAttachShader(p, shader(GL_VERTEX_SHADER, vertex));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fragment));
    glBindAttribLocation(p, 0, "pos");
    glBindAttribLocation(p, 1, "uv");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        fprintf(stderr, "w7-dwm: shader program does not link\n");
        exit(1);
    }
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    return p;
}

static GLuint program(const char *fragment)
{
    return program_with(vertex_source, fragment);
}

static GLuint new_texture(void)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void target(GLuint *fbo, GLuint *texture, int width, int height)
{
    if (!*texture) *texture = new_texture();
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (!*fbo) glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        fprintf(stderr, "w7-dwm: incomplete framebuffer %dx%d\n", width, height);
        exit(1);
    }
}

/* The off-screen scene and the blur's smaller copies, for the screen's size. */
static void targets(void)
{
    int i;
    target(&scene_fbo, &scene_texture, screen_width, screen_height);
    level_width[0] = screen_width;
    level_height[0] = screen_height;
    for (i = 1; i <= levels; i++)
    {
        level_width[i] = (level_width[i - 1] + 1) / 2;
        level_height[i] = (level_height[i - 1] + 1) / 2;
        target(&level_fbo[i], &level_texture[i], level_width[i], level_height[i]);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static int has_extension(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    while (list && (p = strstr(p, name)))
    {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1;
        p += n;
    }
    return 0;
}

static void init_gl(void)
{
    static const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
    static const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform_display;
    EGLConfig config;
    EGLContext context;
    EGLint count, major, minor;
    const char *extensions;

    platform_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    egl = platform_display ? platform_display(EGL_PLATFORM_X11_KHR, dpy, NULL) : eglGetDisplay(dpy);
    if (egl == EGL_NO_DISPLAY || !eglInitialize(egl, &major, &minor))
    {
        fprintf(stderr, "w7-dwm: no EGL for this display\n");
        exit(1);
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    if (!eglChooseConfig(egl, config_attributes, &config, 1, &count) || !count)
    {
        fprintf(stderr, "w7-dwm: no EGL configuration\n");
        exit(1);
    }
    context = eglCreateContext(egl, config, EGL_NO_CONTEXT, context_attributes);
    surface = eglCreateWindowSurface(egl, config, (EGLNativeWindowType)overlay, NULL);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE || !eglMakeCurrent(egl, surface, surface, context))
    {
        fprintf(stderr, "w7-dwm: cannot draw on the overlay window (EGL error 0x%x)\n", eglGetError());
        exit(1);
    }
    eglSwapInterval(egl, 1);

    extensions = eglQueryString(egl, EGL_EXTENSIONS);
    create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
    destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
    image_target_texture = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    import_pixmaps = has_extension(extensions, "EGL_KHR_image_pixmap")
        && has_extension((const char *)glGetString(GL_EXTENSIONS), "GL_OES_EGL_image")
        && create_image && destroy_image && image_target_texture
        && !getenv("W7DWM_NO_IMPORT");
    trace("EGL %d.%d, %s, %s\n", major, minor, (const char *)glGetString(GL_RENDERER),
          import_pixmaps ? "pixmaps imported" : "pixmaps copied (MIT-SHM)");

    draw_program = program(draw_source);
    draw_screen = glGetUniformLocation(draw_program, "screen");
    draw_flip = glGetUniformLocation(draw_program, "flip");
    draw_alpha = glGetUniformLocation(draw_program, "alpha");
    draw_opaque = glGetUniformLocation(draw_program, "opaque");
    draw_swap = glGetUniformLocation(draw_program, "swap");
    draw_clamp = glGetUniformLocation(draw_program, "clamp_alpha");
    down_program = program(down_source);
    down_screen = glGetUniformLocation(down_program, "screen");
    down_half = glGetUniformLocation(down_program, "spread");
    glUniform1f(glGetUniformLocation(down_program, "flip"), 1.0f);
    up_program = program(up_source);
    up_screen = glGetUniformLocation(up_program, "screen");
    up_half = glGetUniformLocation(up_program, "spread");
    glUniform1f(glGetUniformLocation(up_program, "flip"), 1.0f);
    mask_program = program(mask_source);
    mask_screen = glGetUniformLocation(mask_program, "screen");
    mask_half = glGetUniformLocation(mask_program, "spread");
    mask_size = glGetUniformLocation(mask_program, "size");
    glUniform1f(glGetUniformLocation(mask_program, "flip"), 1.0f);
    glUniform1i(glGetUniformLocation(mask_program, "mask"), 1);
    shadow_program = program(shadow_source);
    shadow_screen = glGetUniformLocation(shadow_program, "screen");
    shadow_box = glGetUniformLocation(shadow_program, "box");
    glUniform1f(glGetUniformLocation(shadow_program, "flip"), 1.0f);
    anim_program = program_with(anim_vertex_source, draw_source);
    anim_screen = glGetUniformLocation(anim_program, "screen");
    anim_alpha = glGetUniformLocation(anim_program, "alpha");
    anim_opaque = glGetUniformLocation(anim_program, "opaque");
    anim_swap = glGetUniformLocation(anim_program, "swap");
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    targets();
}

/* ---- window contents ---- */

static void release_old(win *w)
{
    if (w->old_texture) glDeleteTextures(1, &w->old_texture);
    w->old_texture = 0;
    w->settled = 0;
}

static void release(win *w)
{
    if (w->image) destroy_image(egl, w->image);
    w->image = NULL;
    if (w->texture) glDeleteTextures(1, &w->texture);
    w->texture = 0;
    if (w->pixmap && w->pixmap != root_pixmap) XFreePixmap(dpy, w->pixmap);
    w->pixmap = None;
    if (w->shm.shmaddr)
    {
        XShmDetach(dpy, &w->shm);
        shmdt(w->shm.shmaddr);
        w->shm.shmaddr = NULL;
    }
    w->shm_size = 0;
}

/* Reads the pixmap's area R into the texture through MIT-SHM. */
static void upload(win *w, XRectangle r)
{
    XImage *image;
    int size = r.width * r.height * 4;
    if (!r.width || !r.height) return;
    if (size > w->shm_size)
    {
        if (w->shm.shmaddr)
        {
            XShmDetach(dpy, &w->shm);
            shmdt(w->shm.shmaddr);
        }
        size = w->texture_width * w->texture_height * 4;
        w->shm.shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
        if (w->shm.shmid < 0) return;
        w->shm.shmaddr = shmat(w->shm.shmid, NULL, 0);
        shmctl(w->shm.shmid, IPC_RMID, NULL);
        if (w->shm.shmaddr == (void *)-1)
        {
            w->shm.shmaddr = NULL;
            return;
        }
        w->shm.readOnly = False;
        XShmAttach(dpy, &w->shm);
        w->shm_size = size;
    }
    image = XShmCreateImage(dpy, w->visual, w->depth, ZPixmap, w->shm.shmaddr, &w->shm, r.width, r.height);
    if (!image) return;
    if (image->bits_per_pixel == 32 && XShmGetImage(dpy, w->pixmap, image, r.x, r.y, AllPlanes))
    {
        glBindTexture(GL_TEXTURE_2D, w->texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, image->data);
    }
    else
        w->stale = 1; /* resized since it was named: name it again next frame */
    image->data = NULL;
    XDestroyImage(image);
}

/* Names the window's pixmap again after it was mapped or resized. */
static void bind(win *w)
{
    static const EGLint attributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    Window r;
    int x, y;
    unsigned int width, height, border, depth;
    release(w);
    w->stale = 0;
    w->pixmap = w == &background ? root_pixmap : XCompositeNameWindowPixmap(dpy, w->id);
    /* The pixmap's own size: the window may have been resized since the last
     * ConfigureNotify this program read. */
    if (!w->pixmap || !XGetGeometry(dpy, w->pixmap, &r, &x, &y, &width, &height, &border, &depth))
    {
        if (w->pixmap && w != &background) XFreePixmap(dpy, w->pixmap);
        w->pixmap = None;
        return;
    }
    w->texture_width = width;
    w->texture_height = height;
    w->texture = new_texture();
    if (import_pixmaps)
    {
        w->image = create_image(egl, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR,
                                (EGLClientBuffer)(uintptr_t)w->pixmap, attributes);
        if (w->image)
        {
            image_target_texture(GL_TEXTURE_2D, w->image);
            return;
        }
        trace("EGL cannot import the pixmap of 0x%lx (0x%x); copying it\n", w->id, eglGetError());
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w->texture_width, w->texture_height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    w->upload_all = 1;
}

/* ---- window list ---- */

static int find(Window id)
{
    int i;
    for (i = 0; i < nwins; i++)
        if (wins[i]->id == id) return i;
    return -1;
}

static void read_shape(win *w)
{
    int count, ordering, i, n = 0;
    int left = -w->border, top = -w->border;
    int right = w->width + w->border, bottom = w->height + w->border;
    XRectangle *rects;
    XFree(w->rects);
    w->rects = NULL;
    w->nrects = 0;
    rects = XShapeGetRectangles(dpy, w->id, ShapeBounding, &count, &ordering);
    if (!rects) return;
    /* An unshaped window's bounding shape is its whole outer rectangle. */
    if (count == 1 && rects[0].x == left && rects[0].y == top
        && rects[0].width == right - left && rects[0].height == bottom - top)
    {
        XFree(rects);
        return;
    }
    /* The server shows the shape inside the window only; the rectangles a
     * program set may reach past it (Windows 7's user picture). */
    for (i = 0; i < count; i++)
    {
        int x0 = rects[i].x, y0 = rects[i].y;
        int x1 = x0 + rects[i].width, y1 = y0 + rects[i].height;
        if (x0 < left) x0 = left;
        if (y0 < top) y0 = top;
        if (x1 > right) x1 = right;
        if (y1 > bottom) y1 = bottom;
        if (x1 <= x0 || y1 <= y0) continue;
        rects[n].x = x0;
        rects[n].y = y0;
        rects[n].width = x1 - x0;
        rects[n].height = y1 - y0;
        n++;
    }
    w->rects = rects;
    w->nrects = n;
    if (!n)
    {
        /* nothing of the window shows; one empty rectangle says so */
        w->nrects = 1;
        rects[0].width = rects[0].height = 0;
    }
}

static unsigned long cardinal(Window id, Atom property, unsigned long fallback)
{
    Atom type;
    int format;
    unsigned long count, after, value = fallback;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, id, property, 0, 1, False, XA_CARDINAL, &type, &format, &count, &after, &data)
        == Success && data && format == 32 && count)
        value = *(unsigned long *)data;
    if (data) XFree(data);
    return value;
}

static void read_properties(win *w)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *name = NULL;
    w->shell_kind = 0;
    if (XGetWindowProperty(dpy, w->id, atom_class, 0, 64, False, XA_STRING,
                           &type, &format, &count, &after, &name) == Success && name)
    {
        if (!strcmp((char *)name, "DV2ControlHost")) w->shell_kind = 1;
        else if (!strcmp((char *)name, "Shell_TrayWnd")) w->shell_kind = 2;
        else if (!strcmp((char *)name, "Win4WineStartButton")) w->shell_kind = 3;
    }
    if (name) XFree(name);
    w->glass = cardinal(w->id, atom_glass, 0) != 0;
    w->opacity = (double)(cardinal(w->id, atom_opacity, 0xffffffffUL) & 0xffffffffUL) / 0xffffffffUL;
}

/* Whether the atom list PROPERTY of window ID holds VALUE (or, with VALUE
 * None, holds anything). */
static int has_atom(Window id, Atom property, Atom value)
{
    Atom type, *atoms = NULL;
    int format, found = 0;
    unsigned long count = 0, after, i;
    if (XGetWindowProperty(dpy, id, property, 0, 32, False, XA_ATOM, &type, &format, &count, &after,
                           (unsigned char **)&atoms) == Success && atoms && format == 32)
        for (i = 0; i < count && !found; i++) found = value == None || atoms[i] == value;
    if (atoms) XFree(atoms);
    return found;
}

/* The window's ICCCM WM_STATE (3: iconic, as the window manager minimized
 * it), or -1 without one. */
static int wm_state(Window id)
{
    Atom type;
    int format, state = -1;
    unsigned long count, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, id, atom_wm_state, 0, 2, False, atom_wm_state, &type, &format, &count, &after,
                           &data) == Success && data && format == 32 && count)
        state = (int)*(unsigned long *)data;
    if (data) XFree(data);
    return state;
}

/* Windows 7 animates the application windows and their dialogs: not menus,
 * tooltips, the taskbar, the Start menu or the desktop. */
static int animatable(win *w)
{
    if (w->override || w->shell_kind || w == &background) return 0;
    if (w->x <= 0 && w->y <= 0 && w->width + 2 * w->border >= screen_width
        && w->height + 2 * w->border >= screen_height)
        return 0;
    return !has_atom(w->id, atom_type, None) || has_atom(w->id, atom_type, atom_type_normal)
        || has_atom(w->id, atom_type, atom_type_dialog);
}

/* Its taskbar button, from _NET_WM_ICON_GEOMETRY, which the WM sets */
static void read_button(win *w)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = NULL;
    w->has_button = 0;
    if (XGetWindowProperty(dpy, w->id, atom_icon_geometry, 0, 4, False, XA_CARDINAL, &type, &format, &count, &after,
                           &data) == Success && data && format == 32 && count == 4)
    {
        unsigned long *v = (unsigned long *)data;
        if (v[2] && v[3])
        {
            w->button[0] = (int)v[0];
            w->button[1] = (int)v[1];
            w->button[2] = (int)v[0] + (int)v[2];
            w->button[3] = (int)v[1] + (int)v[3];
            w->has_button = 1;
        }
    }
    if (data) XFree(data);
}

static void animate(win *w, int kind, long long length)
{
    trace("0x%lx: animation %d\n", w->id, kind);
    if (kind == ANIM_MINIMIZE || kind == ANIM_RESTORE) read_button(w);
    w->anim = kind;
    w->anim_start = now_ns();
    w->anim_length = length;
    dirty = 1;
}

static void add(Window id, Window above)
{
    XWindowAttributes attributes;
    win *w;
    int i, at = 0;
    if (id == overlay || find(id) >= 0) return;
    if (!XGetWindowAttributes(dpy, id, &attributes) || attributes.class == InputOnly) return;
    w = calloc(1, sizeof(*w));
    w->id = id;
    w->x = attributes.x;
    w->y = attributes.y;
    w->width = attributes.width;
    w->height = attributes.height;
    w->border = attributes.border_width;
    w->depth = attributes.depth;
    w->visual = attributes.visual;
    w->mapped = attributes.map_state == IsViewable;
    w->override = attributes.override_redirect;
    w->stale = 1;
    w->damage = XDamageCreate(dpy, id, XDamageReportNonEmpty);
    XSelectInput(dpy, id, PropertyChangeMask);
    XShapeSelectInput(dpy, id, ShapeNotifyMask);
    read_properties(w);
    read_shape(w);
    w->animates = w->mapped && animatable(w);
    if (nwins == capacity)
    {
        capacity = capacity ? capacity * 2 : 64;
        wins = realloc(wins, capacity * sizeof(*wins));
    }
    if (above != None && (i = find(above)) >= 0) at = i + 1;
    else if (above == (Window)-1) at = nwins;
    memmove(wins + at + 1, wins + at, (nwins - at) * sizeof(*wins));
    wins[at] = w;
    nwins++;
    dirty = 1;
}

static void remove_at(int i, int destroyed)
{
    win *w = wins[i];
    release(w);
    release_old(w);
    if (!destroyed) XDamageDestroy(dpy, w->damage);
    XFree(w->rects);
    free(w);
    memmove(wins + i, wins + i + 1, (nwins - i - 1) * sizeof(*wins));
    nwins--;
    dirty = 1;
}

/* Moves window I right above ABOVE (None: to the bottom, -1: to the top). */
static void restack(int i, Window above)
{
    win *w = wins[i];
    int at;
    memmove(wins + i, wins + i + 1, (nwins - i - 1) * sizeof(*wins));
    nwins--;
    if (above == None) at = 0;
    else if (above == (Window)-1 || (at = find(above) + 1) == 0) at = nwins;
    memmove(wins + at + 1, wins + at, (nwins - at) * sizeof(*wins));
    wins[at] = w;
    nwins++;
}

static void read_root_pixmap(void)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = NULL;
    Pixmap pixmap = None;
    Atom atoms[2] = { atom_rootpmap, atom_esetroot };
    int i;
    for (i = 0; i < 2 && !pixmap; i++)
    {
        if (XGetWindowProperty(dpy, root, atoms[i], 0, 1, False, XA_PIXMAP, &type, &format, &count, &after, &data)
            == Success && data && count)
            pixmap = *(Pixmap *)data;
        if (data) XFree(data);
        data = NULL;
    }
    background.pixmap = None; /* not ours to free */
    release(&background);
    root_pixmap = pixmap;
    if (pixmap)
    {
        Window r;
        unsigned int width, height, border, depth;
        int x, y;
        if (!XGetGeometry(dpy, pixmap, &r, &x, &y, &width, &height, &border, &depth))
        {
            root_pixmap = None;
            return;
        }
        background.width = width;
        background.height = height;
        background.depth = depth;
        background.visual = DefaultVisual(dpy, DefaultScreen(dpy));
        background.stale = 1;
    }
    dirty = 1;
}

/* ---- painting ---- */

typedef struct { float x, y, u, v; } vertex;

static vertex *vertices;
static int nvertices, vertex_capacity;

static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    vertex q[6] = { { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x0, y1, u0, v1 },
                    { x1, y0, u1, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 } };
    if (nvertices + 6 > vertex_capacity)
    {
        vertex_capacity = vertex_capacity ? vertex_capacity * 2 : 384;
        vertices = realloc(vertices, vertex_capacity * sizeof(*vertices));
    }
    memcpy(vertices + nvertices, q, sizeof(q));
    nvertices += 6;
}

static void flush(void)
{
    if (!nvertices) return;
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), &vertices[0].x);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), &vertices[0].u);
    glDrawArrays(GL_TRIANGLES, 0, nvertices);
    nvertices = 0;
}

/* Copies W's contents into its snapshot (old_texture), as they are now. */
static void snapshot(win *w)
{
    GLuint fbo = 0;
    release_old(w);
    if (!w->texture) return;
    target(&fbo, &w->old_texture, w->texture_width, w->texture_height);
    glViewport(0, 0, w->texture_width, w->texture_height);
    glDisable(GL_BLEND);
    glUseProgram(draw_program);
    glUniform2f(draw_screen, w->texture_width, w->texture_height);
    glUniform1f(draw_flip, 1.0f);
    glUniform1f(draw_alpha, 1.0f);
    glUniform1f(draw_opaque, 0.0f);
    glUniform1f(draw_swap, !w->image);
    glBindTexture(GL_TEXTURE_2D, w->texture);
    quad(0, 0, w->texture_width, w->texture_height, 0, 0, 1, 1);
    flush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    w->settled = 1;
}

/* The window's shape as quads; with TEXTURE the window's own texture
 * coordinates, otherwise the screen's (UV_WIDTH x UV_HEIGHT normalised). */
static void shape_quads(win *w, int texture)
{
    float ox = w->x + w->border, oy = w->y + w->border;
    float tw = w->texture_width, th = w->texture_height;
    XRectangle whole = { -w->border, -w->border, w->width + 2 * w->border, w->height + 2 * w->border };
    XRectangle *rects = w->nrects ? w->rects : &whole;
    int i, count = w->nrects ? w->nrects : 1;
    for (i = 0; i < count; i++)
    {
        float x0 = ox + rects[i].x, y0 = oy + rects[i].y;
        float x1 = x0 + rects[i].width, y1 = y0 + rects[i].height;
        if (texture)
        {
            float u0 = (rects[i].x + w->border) / tw, v0 = (rects[i].y + w->border) / th;
            quad(x0, y0, x1, y1, u0, v0, u0 + rects[i].width / tw, v0 + rects[i].height / th);
        }
        else
            quad(x0, y0, x1, y1, x0 / screen_width, y0 / screen_height, x1 / screen_width, y1 / screen_height);
    }
}

static void scissor(int level, int x0, int y0, int x1, int y1)
{
    int s = 1 << level;
    x0 = x0 / s - 1;
    y0 = y0 / s - 1;
    x1 = (x1 + s - 1) / s + 1;
    y1 = (y1 + s - 1) / s + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    glScissor(x0, y0, x1 - x0, y1 - y0);
}

/* Blurs the scene behind W and draws the blurred pixels under its shape. */
static void blur_behind(win *w)
{
    int margin = (int)(offset * 2 * (1 << levels)) + 2;
    int x0 = w->x - margin, y0 = w->y - margin;
    int x1 = w->x + w->width + 2 * w->border + margin, y1 = w->y + w->height + 2 * w->border + margin;
    int i;
    if (x1 <= 0 || y1 <= 0 || x0 >= screen_width || y0 >= screen_height) return;
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glUseProgram(down_program);
    for (i = 1; i <= levels; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, level_fbo[i]);
        glViewport(0, 0, level_width[i], level_height[i]);
        scissor(i, x0, y0, x1, y1);
        glBindTexture(GL_TEXTURE_2D, i == 1 ? scene_texture : level_texture[i - 1]);
        glUniform2f(down_screen, level_width[i], level_height[i]);
        glUniform2f(down_half, offset * 0.5f / level_width[i - 1], offset * 0.5f / level_height[i - 1]);
        quad(0, 0, level_width[i], level_height[i], 0, 0, 1, 1);
        flush();
    }
    glUseProgram(up_program);
    for (i = levels - 1; i >= 1; i--)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, level_fbo[i]);
        glViewport(0, 0, level_width[i], level_height[i]);
        scissor(i, x0, y0, x1, y1);
        glBindTexture(GL_TEXTURE_2D, level_texture[i + 1]);
        glUniform2f(up_screen, level_width[i], level_height[i]);
        glUniform2f(up_half, offset * 0.5f / level_width[i + 1], offset * 0.5f / level_height[i + 1]);
        quad(0, 0, level_width[i], level_height[i], 0, 0, 1, 1);
        flush();
    }
    glDisable(GL_SCISSOR_TEST);
    /* The last pass goes straight into the scene, under the window's shape
     * and where its pixels are not fully transparent. */
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glViewport(0, 0, screen_width, screen_height);
    glUseProgram(mask_program);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, w->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, level_texture[1]);
    glUniform2f(mask_screen, screen_width, screen_height);
    glUniform2f(mask_size, screen_width, screen_height);
    glUniform2f(mask_half, offset * 0.5f / level_width[1], offset * 0.5f / level_height[1]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    shape_quads(w, 1);
    flush();
}

/* Windows 7's shadow, under a window with an Aero frame: a glass window the
 * window manager manages, but not one as wide as the screen (the taskbar). */
#define SHADOW 16

static void draw_shadow(win *w)
{
    int width = w->width + 2 * w->border;
    int left = -w->border, top = -w->border;
    int right = w->width + w->border, bottom = w->height + w->border;
    float x0, y0, x1, y1;
    int i;
    /* Windows 7 casts none under the Start menu */
    if (!w->glass || w->override || w->shell_kind == 1 || width >= screen_width || w->opacity < 1.0) return;
    if (w->nrects)
    {
        /* The Start menu reserves transparent space beyond its bounding
         * shape. Its shadow must follow the visible menu, not that space. */
        left = right; top = bottom; right = -w->border; bottom = -w->border;
        for (i = 0; i < w->nrects; i++)
        {
            XRectangle *r = &w->rects[i];
            if (!r->width || !r->height) continue;
            if (r->x < left) left = r->x;
            if (r->y < top) top = r->y;
            if (r->x + r->width > right) right = r->x + r->width;
            if (r->y + r->height > bottom) bottom = r->y + r->height;
        }
        if (right <= left || bottom <= top) return;
    }
    /* A little lower than the window, as Windows 7's falls. */
    x0 = w->x + w->border + left; y0 = w->y + w->border + top + 2;
    x1 = w->x + w->border + right; y1 = w->y + w->border + bottom + 2;
    glUseProgram(shadow_program);
    glUniform2f(shadow_screen, screen_width, screen_height);
    glUniform4f(shadow_box, x0, y0, x1, y1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    /* The menu ends at the taskbar; its shadow must not show through the
     * translucent taskbar below it. Scene FBO rows use X's downward Y. */
    if (w->shell_kind == 1)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, screen_width, w->y + w->height - 1);
    }
    quad(x0 - SHADOW, y0 - SHADOW, x1 + SHADOW, y1 + SHADOW, 0, 0, 0, 0);
    flush();
    glDisable(GL_SCISSOR_TEST);
}

/* ---- animations ---- */

/* 4x4 matrices on row vectors, as Direct2D's: A * B applies A, then B. */
typedef struct { float m[4][4]; } mat4;

static mat4 identity(void)
{
    mat4 r = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
    return r;
}

static mat4 mul(mat4 a, mat4 b)
{
    mat4 r;
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            r.m[i][j] = 0;
            for (k = 0; k < 4; k++) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    return r;
}

static mat4 translation(float x, float y, float z)
{
    mat4 r = identity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}

static mat4 scaling(float x, float y, float z)
{
    mat4 r = identity();
    r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
    return r;
}

static mat4 rotation(int axis, float degrees)
{
    float a = degrees * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    mat4 r = identity();
    if (axis == 0) { r.m[1][1] = c; r.m[1][2] = s; r.m[2][1] = -s; r.m[2][2] = c; }
    else if (axis == 1) { r.m[0][0] = c; r.m[0][2] = -s; r.m[2][0] = s; r.m[2][2] = c; }
    else { r.m[0][0] = c; r.m[0][1] = s; r.m[1][0] = -s; r.m[1][1] = c; }
    return r;
}

static mat4 perspective(float depth)
{
    mat4 r = identity();
    if (depth > 0) r.m[2][3] = -1.0f / depth;
    return r;
}

/* Where W goes into the taskbar: its button (the DWM gets it from the
 * shell); without one, the taskbar's height under the window's middle. */
static void minimize_rect(win *w, float rect[4])
{
    float bar = screen_height - 40, height = 40, middle = w->x + (w->width + 2 * w->border) / 2.0f;
    int i;
    if (w->has_button)
    {
        memcpy(rect, w->button, sizeof(w->button));
        return;
    }
    for (i = 0; i < nwins; i++)
        if (wins[i]->mapped && wins[i]->shell_kind == 2 && wins[i]->width >= screen_width / 2)
        {
            bar = wins[i]->y;
            height = wins[i]->height + 2 * wins[i]->border;
        }
    if (middle < 60) middle = 60;
    if (middle > screen_width - 60) middle = screen_width - 60;
    rect[0] = middle - 30;
    rect[1] = bar;
    rect[2] = middle + 30;
    rect[3] = bar + height;
}

/* The window's transform at progress T and its opacity, as Windows 7's DWM
 * moves it (the curves of the "DWM 3D Transforms" Windhawk mod, which
 * restores them on later Windows): opening, it tilts back up from 5 degrees
 * and comes forward while it fades in; closing, it tilts away and fades; it
 * shrinks with a slight turn into its taskbar button and grows back out of
 * it. */
static mat4 transform(win *w, float t, float *opacity)
{
    float left = w->x, top = w->y;
    float width = w->width + 2 * w->border, height = w->height + 2 * w->border;
    mat4 model = identity(), camera = identity(), offset = identity();

    *opacity = 1.0f;
    if (w->anim == ANIM_OPEN || w->anim == ANIM_CLOSE)
    {
        int open = w->anim == ANIM_OPEN;
        float ease = open ? sqrtf(t) : 1.0f - sqrtf(1.0f - t), inverse = 1.0f - ease;
        float pitch = open ? inverse * 5.0f : ease * -5.0f, yaw = open ? 0.0f : ease * -2.0f;
        float pivot = open ? height * 0.5f : height;
        float z = open ? inverse * height * 0.1f : 0.0f, y = open ? inverse * height * -0.017f : 0.0f;
        *opacity = open ? ease : 1.0f - t;
        model = mul(mul(mul(translation(0, -pivot, 0), rotation(1, yaw)), rotation(0, pitch)),
                    translation(0, pivot + y, z));
        camera = mul(mul(mul(translation(-width / 2, -height / 2, 0), scaling(1, 1, -1)),
                         perspective(fmaxf(height, 1.0f) * 0.8f)), translation(width / 2, height / 2, 0));
    }
    else if (w->anim == ANIM_MINIMIZE || w->anim == ANIM_RESTORE)
    {
        int minimize = w->anim == ANIM_MINIMIZE;
        float button[4], cv = minimize ? 1.0f - t : 1.0f - sqrtf(1.0f - t), v = minimize ? t : 1.0f - cv;
        float final_width, final_height, final_x, final_y, x, y, cw, ch, depth = screen_width;
        minimize_rect(w, button);
        final_width = (button[2] - button[0]) * 0.6f;
        final_height = final_width * height / width;
        final_x = button[0] + final_width * 0.3f;
        final_y = button[1] - final_height;
        cw = (1 - v) * width + v * final_width;
        ch = (1 - v) * height + v * final_height;
        x = (1 - v) * left + v * final_x;
        y = (1 - v) * top + v * final_y;
        *opacity = minimize ? cv * 0.85f : cv;
        model = mul(mul(mul(mul(mul(mul(scaling(1 / width, 1 / height, 1), translation(0, -1, 0)),
                                    rotation(2, v * -1.0f)), rotation(0, v * -5.0f)), rotation(1, v * -8.0f)),
                        translation(0, 1, 0)), scaling(cw, ch, depth));
        camera = mul(mul(mul(translation(-width / 2, -height / 2, 0), scaling(1, 1, -1)), perspective(depth)),
                     translation(width / 2, height / 2, 0));
        offset = translation(x - left, y - top, 0);
    }
    return mul(mul(mul(mul(translation(-left, -top, 0), model), camera), offset), translation(left, top, 0));
}

static float progress(win *w)
{
    float t = (float)(now_ns() - w->anim_start) / (float)w->anim_length;
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

typedef struct { float x, y, w, u, v; } avertex;

static void draw_animated(win *w)
{
    static avertex *buffer;
    static int capacity_;
    float opacity, tw = w->texture_width, th = w->texture_height;
    mat4 m = transform(w, progress(w), &opacity);
    float ox = w->x + w->border, oy = w->y + w->border;
    XRectangle whole = { -w->border, -w->border, w->width + 2 * w->border, w->height + 2 * w->border };
    XRectangle *rects = w->nrects ? w->rects : &whole;
    int i, j, n = 0, count = w->nrects ? w->nrects : 1;
    if (!w->texture || opacity * w->opacity <= 0.0f) return;
    if (capacity_ < count * 6)
    {
        capacity_ = count * 6;
        buffer = realloc(buffer, capacity_ * sizeof(*buffer));
    }
    for (i = 0; i < count; i++)
    {
        float x0 = ox + rects[i].x, y0 = oy + rects[i].y, x1 = x0 + rects[i].width, y1 = y0 + rects[i].height;
        float u0 = (rects[i].x + w->border) / tw, v0 = (rects[i].y + w->border) / th;
        float u1 = u0 + rects[i].width / tw, v1 = v0 + rects[i].height / th;
        float corners[6][4] = { { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x0, y1, u0, v1 },
                                { x1, y0, u1, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 } };
        for (j = 0; j < 6; j++)
        {
            float x = corners[j][0], y = corners[j][1];
            float X = x * m.m[0][0] + y * m.m[1][0] + m.m[3][0];
            float Y = x * m.m[0][1] + y * m.m[1][1] + m.m[3][1];
            float W = x * m.m[0][3] + y * m.m[1][3] + m.m[3][3];
            if (W < 0.01f) W = 0.01f;
            buffer[n].x = X / W;
            buffer[n].y = Y / W;
            buffer[n].w = W;
            buffer[n].u = corners[j][2];
            buffer[n].v = corners[j][3];
            n++;
        }
    }
    glUseProgram(anim_program);
    glUniform2f(anim_screen, screen_width, screen_height);
    glUniform1f(anim_alpha, opacity * w->opacity);
    glUniform1f(anim_opaque, w->depth != 32);
    glUniform1f(anim_swap, !w->image);
    glBindTexture(GL_TEXTURE_2D, w->texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(avertex), &buffer[0].x);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(avertex), &buffer[0].u);
    glDrawArrays(GL_TRIANGLES, 0, n);
}

/* Ends the animations that are over: a window closed or minimized is let go
 * of then (and freed if it was destroyed meanwhile). */
static void finish_animations(void)
{
    int i;
    for (i = nwins - 1; i >= 0; i--)
    {
        win *w = wins[i];
        if (w->frozen && now_ns() - w->frozen > 200 * 1000000LL)
        {
            /* not resized after all */
            w->frozen = 0;
            read_shape(w);
            dirty = 1;
        }
        if (!w->anim) continue;
        if (now_ns() - w->anim_start < w->anim_length)
        {
            dirty = 1;
            continue;
        }
        w->anim = 0;
        dirty = 1;
        if (w->destroyed) remove_at(i, 1);
        else if (!w->mapped)
        {
            release(w);
            release_old(w);
        }
    }
}

static void draw_window(win *w)
{
    float alpha = w->opacity;
    if (!w->texture) return;
    glUseProgram(draw_program);
    glUniform2f(draw_screen, screen_width, screen_height);
    glUniform1f(draw_flip, 1.0f);
    glUniform1f(draw_alpha, alpha);
    glUniform1f(draw_opaque, w->depth != 32);
    /* a snapshot has its colours in GL's order already */
    glUniform1f(draw_swap, !w->image && w->texture != w->old_texture);
    glBindTexture(GL_TEXTURE_2D, w->texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (w->shell_kind == 1)
    {
        /* The menu's bottom highlight would form a white seam over Start. */
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, screen_width, w->y + w->height - 1);
    }
    /* The orb's top row is not premultiplied: over the taskbar's light
     * edge, which runs behind it, its colour would add up to white */
    glUniform1f(draw_clamp, w->shell_kind == 3);
    shape_quads(w, 1);
    flush();
    glDisable(GL_SCISSOR_TEST);
}

/* Brings W's texture up to date: names its pixmap again if needed, and
 * copies its damaged part when its pixmap is not imported. */
static void refresh(win *w)
{
    if (w->stale) bind(w);
    if (w->damaged && w != &background)
    {
        XserverRegion parts = None;
        if (!w->image)
        {
            XRectangle bounds;
            int count;
            XRectangle *rects;
            parts = XFixesCreateRegion(dpy, NULL, 0);
            XDamageSubtract(dpy, w->damage, None, parts);
            rects = XFixesFetchRegionAndBounds(dpy, parts, &count, &bounds);
            if (rects) XFree(rects);
            XFixesDestroyRegion(dpy, parts);
            if (count)
            {
                /* the window's coordinates are those of its pixmap, past the border */
                int x0 = bounds.x + w->border, y0 = bounds.y + w->border;
                int x1 = x0 + bounds.width, y1 = y0 + bounds.height;
                if (w->upload.width)
                {
                    if (w->upload.x < x0) x0 = w->upload.x;
                    if (w->upload.y < y0) y0 = w->upload.y;
                    if (w->upload.x + w->upload.width > x1) x1 = w->upload.x + w->upload.width;
                    if (w->upload.y + w->upload.height > y1) y1 = w->upload.y + w->upload.height;
                }
                w->upload.x = x0;
                w->upload.y = y0;
                w->upload.width = x1 - x0;
                w->upload.height = y1 - y0;
            }
        }
        else
            XDamageSubtract(dpy, w->damage, None, None);
        w->damaged = 0;
    }
    if (!w->image && w->texture)
    {
        XRectangle r = w->upload;
        if (w->upload_all)
        {
            r.x = r.y = 0;
            r.width = w->texture_width;
            r.height = w->texture_height;
        }
        if (r.x < 0) { r.width += r.x; r.x = 0; }
        if (r.y < 0) { r.height += r.y; r.y = 0; }
        if (r.x + r.width > w->texture_width) r.width = w->texture_width - r.x;
        if (r.y + r.height > w->texture_height) r.height = w->texture_height - r.y;
        if ((short)r.width > 0 && (short)r.height > 0) upload(w, r);
        memset(&w->upload, 0, sizeof(w->upload));
        w->upload_all = 0;
    }
}

static void paint(void)
{
    static long long frames, total;
    long long start = now_ns();
    int i;

    for (i = 0; i < nwins; i++)
        if (wins[i]->mapped) refresh(wins[i]);
    if (root_pixmap) refresh(&background);
    for (i = 0; i < nwins; i++)
    {
        win *w = wins[i];
        if (!w->mapped || !w->animates || w->settled || w->anim || w->frozen) continue;
        if (w->texture && !w->stale && now_ns() - w->last_damage > 80 * 1000000LL)
            snapshot(w);
        else
            dirty = 1;  /* to take it once it settles */
    }

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glViewport(0, 0, screen_width, screen_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (root_pixmap && background.texture)
    {
        background.x = background.y = 0;
        draw_window(&background);
    }
    for (i = 0; i < nwins; i++)
    {
        win *w = wins[i];
        if (!(w->mapped || w->anim) || !w->texture || w->opacity <= 0.0) continue;
        if (w->anim)
        {
            /* no shadow or blur while it moves, briefly */
            draw_animated(w);
            continue;
        }
        if (w->frozen && w->old_texture)
        {
            /* its snapshot, with the shape it had */
            GLuint live = w->texture;
            w->texture = w->old_texture;
            draw_shadow(w);
            if (w->glass && w->depth == 32) blur_behind(w);
            draw_window(w);
            w->texture = live;
            continue;
        }
        if (w->x >= screen_width || w->y >= screen_height
            || w->x + w->width + 2 * w->border <= 0 || w->y + w->height + 2 * w->border <= 0)
            continue;
        draw_shadow(w);
        if (w->glass && w->depth == 32) blur_behind(w);
        draw_window(w);
    }

    /* The scene on the screen (GL's rows go up, X's down). */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_width, screen_height);
    glDisable(GL_BLEND);
    glUseProgram(draw_program);
    glUniform2f(draw_screen, screen_width, screen_height);
    glUniform1f(draw_flip, -1.0f);
    glUniform1f(draw_alpha, 1.0f);
    glUniform1f(draw_opaque, 1.0f);
    glUniform1f(draw_swap, 0.0f);
    glBindTexture(GL_TEXTURE_2D, scene_texture);
    quad(0, 0, screen_width, screen_height, 0, 0, 1, 1);
    flush();
    eglSwapBuffers(egl, surface);
    finish_animations();

    if (debug)
    {
        total += now_ns() - start;
        if (++frames == 120)
        {
            trace("%.2f ms per frame\n", total / frames / 1e6);
            frames = total = 0;
        }
    }
}

/* ---- events ---- */

static void handle(XEvent *event)
{
    int i;
    win *w;
    switch (event->type)
    {
    case CreateNotify:
        if (event->xcreatewindow.parent == root) add(event->xcreatewindow.window, (Window)-1);
        break;
    case DestroyNotify:
        if ((i = find(event->xdestroywindow.window)) < 0) break;
        if (wins[i]->anim)
        {
            /* its closing goes on with its last picture */
            wins[i]->destroyed = 1;
            wins[i]->id = None;
        }
        else
            remove_at(i, 1);
        break;
    case ReparentNotify:
        if (event->xreparent.parent == root) add(event->xreparent.window, (Window)-1);
        else if ((i = find(event->xreparent.window)) >= 0) remove_at(i, 0);
        break;
    case MapNotify:
        if ((i = find(event->xmap.window)) < 0) break;
        w = wins[i];
        w->mapped = 1;
        w->stale = 1;
        w->damaged = 1;
        read_properties(w);
        read_shape(w);
        w->animates = animatable(w);
        if (w->minimized) animate(w, ANIM_RESTORE, MINIMIZE_NS);
        else if (w->animates) animate(w, ANIM_OPEN, OPEN_NS);
        else w->anim = 0;
        w->minimized = 0;
        dirty = 1;
        break;
    case UnmapNotify:
        if ((i = find(event->xunmap.window)) < 0) break;
        w = wins[i];
        /* already unmapped: the synthetic UnmapNotify of an ICCCM withdrawal */
        if (!w->mapped) break;
        if (w->mapped && w->texture && animatable(w))
        {
            /* the window manager minimizes a window by unmapping it, iconic
             * (WM_STATE may follow: see PropertyNotify) */
            w->minimized = wm_state(w->id) == 3;
            animate(w, w->minimized ? ANIM_MINIMIZE : ANIM_CLOSE, w->minimized ? MINIMIZE_NS : CLOSE_NS);
        }
        else
        {
            w->anim = 0;
            release(w);
            release_old(w);
        }
        w->mapped = 0;
        dirty = 1;
        break;
    case ConfigureNotify:
        if (event->xconfigure.window == root)
        {
            if (event->xconfigure.width != screen_width || event->xconfigure.height != screen_height)
            {
                screen_width = event->xconfigure.width;
                screen_height = event->xconfigure.height;
                targets();
                dirty = 1;
            }
            break;
        }
        if ((i = find(event->xconfigure.window)) < 0) break;
        w = wins[i];
        if (w->width != event->xconfigure.width || w->height != event->xconfigure.height
            || w->border != event->xconfigure.border_width)
            w->stale = 1;
        w->x = event->xconfigure.x;
        w->y = event->xconfigure.y;
        w->width = event->xconfigure.width;
        w->height = event->xconfigure.height;
        w->border = event->xconfigure.border_width;
        if (w->stale || w->frozen) read_shape(w);
        /* resized: what Wine drew for its new size shows */
        if (w->stale) w->frozen = 0;
        restack(i, event->xconfigure.above);
        dirty = 1;
        break;
    case CirculateNotify:
        if ((i = find(event->xcirculate.window)) < 0) break;
        restack(i, event->xcirculate.place == PlaceOnTop ? (Window)-1 : None);
        dirty = 1;
        break;
    case PropertyNotify:
        if (event->xproperty.window == root)
        {
            if (event->xproperty.atom == atom_rootpmap || event->xproperty.atom == atom_esetroot)
                read_root_pixmap();
            break;
        }
        if ((i = find(event->xproperty.window)) < 0) break;
        w = wins[i];
        if (event->xproperty.atom == atom_glass || event->xproperty.atom == atom_opacity ||
            event->xproperty.atom == atom_class)
        {
            read_properties(w);
            dirty = 1;
        }
        else if (event->xproperty.atom == atom_wm_state && !w->mapped && w->anim == ANIM_CLOSE
                 && wm_state(w->id) == 3)
        {
            /* iconic after its unmapping: minimized, not closed */
            w->minimized = 1;
            w->anim = ANIM_MINIMIZE;
            w->anim_length = MINIMIZE_NS;
            read_button(w);
        }
        break;
    default:
        if (event->type == damage_event + XDamageNotify)
        {
            XDamageNotifyEvent *damage = (XDamageNotifyEvent *)event;
            if ((i = find(damage->drawable)) >= 0)
            {
                wins[i]->damaged = 1;
                wins[i]->last_damage = now_ns();
                wins[i]->settled = 0;
            }
            else XDamageSubtract(dpy, damage->damage, None, None);
            dirty = 1;
        }
        else if (event->type == shape_event + ShapeNotify)
        {
            XShapeEvent *shape = (XShapeEvent *)event;
            if (shape->kind == ShapeBounding && (i = find(shape->window)) >= 0)
            {
                w = wins[i];
                if (w->mapped && !w->anim && !w->frozen && w->old_texture && animatable(w) && shape->shaped
                    && (abs((int)shape->width - (w->width + 2 * w->border)) > 64
                        || abs((int)shape->height - (w->height + 2 * w->border)) > 64))
                {
                    /* Wine shapes and paints its window for its new size
                     * (maximized, restored) before it is resized: until then
                     * it shows as it was */
                    w->frozen = now_ns();
                }
                else if (!w->frozen)
                    read_shape(w);
                dirty = 1;
            }
        }
        break;
    }
}

static void stop(int signal)
{
    (void)signal;
    quit = 1;
}

/* Takes _NET_WM_CM_S<screen>, which tells programs a compositor runs. */
static void claim_selection(void)
{
    char name[32];
    Atom selection;
    Window owner;
    snprintf(name, sizeof(name), "_NET_WM_CM_S%d", DefaultScreen(dpy));
    selection = XInternAtom(dpy, name, False);
    if (XGetSelectionOwner(dpy, selection) != None)
    {
        fprintf(stderr, "w7-dwm: another compositor runs on this display\n");
        exit(1);
    }
    owner = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    Xutf8SetWMProperties(dpy, owner, "w7-dwm", "w7-dwm", NULL, 0, NULL, NULL, NULL);
    XSetSelectionOwner(dpy, selection, owner, CurrentTime);
}

int main(int argc, char **argv)
{
    int event_base, error_base, major, minor, i;
    Window root_return, parent, *children;
    unsigned int count;
    XserverRegion empty;
    XWindowAttributes attributes;
    long long last = 0;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--blur") && i + 1 < argc) levels = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--offset") && i + 1 < argc) offset = atof(argv[++i]);
        else
        {
            fprintf(stderr, "usage: w7-dwm [--blur LEVELS] [--offset PIXELS]\n");
            return 2;
        }
    }
    if (levels < 1) levels = 1;
    if (levels > MAX_LEVELS) levels = MAX_LEVELS;
    debug = getenv("W7DWM_DEBUG") && *getenv("W7DWM_DEBUG") == '1';

    if (!(dpy = XOpenDisplay(NULL)))
    {
        fprintf(stderr, "w7-dwm: cannot open display\n");
        return 1;
    }
    root = DefaultRootWindow(dpy);
    if (!XCompositeQueryExtension(dpy, &event_base, &error_base)
        || (XCompositeQueryVersion(dpy, &major, &minor), major == 0 && minor < 2)
        || !XDamageQueryExtension(dpy, &damage_event, &error_base)
        || !XFixesQueryExtension(dpy, &event_base, &error_base)
        || !XShapeQueryExtension(dpy, &shape_event, &error_base))
    {
        fprintf(stderr, "w7-dwm: the X server lacks Composite 0.2, Damage, XFixes or SHAPE\n");
        return 1;
    }
    XSetErrorHandler(ignore_errors);
    atom_glass = XInternAtom(dpy, "_WIN4WINE_GLASS", False);
    atom_class = XInternAtom(dpy, "_WIN4WINE_CLASS", False);
    atom_opacity = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    atom_rootpmap = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    atom_esetroot = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);
    atom_wm_state = XInternAtom(dpy, "WM_STATE", False);
    atom_icon_geometry = XInternAtom(dpy, "_NET_WM_ICON_GEOMETRY", False);
    atom_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atom_type_normal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    atom_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    claim_selection();

    XGetWindowAttributes(dpy, root, &attributes);
    screen_width = attributes.width;
    screen_height = attributes.height;

    XGrabServer(dpy);
    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
    XSelectInput(dpy, root, SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask);
    overlay = XCompositeGetOverlayWindow(dpy, root);
    /* Clicks go through the overlay to the windows under it. */
    empty = XFixesCreateRegion(dpy, NULL, 0);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, empty);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeBounding, 0, 0, None);
    XFixesDestroyRegion(dpy, empty);
    if (XQueryTree(dpy, root, &root_return, &parent, &children, &count))
    {
        for (i = 0; i < (int)count; i++) add(children[i], (Window)-1);
        XFree(children);
    }
    XUngrabServer(dpy);

    init_gl();
    read_root_pixmap();

    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    while (!quit)
    {
        XEvent event;
        struct pollfd fd = { ConnectionNumber(dpy), POLLIN, 0 };
        int timeout = -1;
        while (XPending(dpy))
        {
            XNextEvent(dpy, &event);
            handle(&event);
        }
        if (dirty)
        {
            long long wait = last + FRAME_NS - now_ns();
            if (wait <= 0)
            {
                dirty = 0;
                paint();
                last = now_ns();
                continue;
            }
            timeout = (int)(wait / 1000000) + 1;
        }
        if (poll(&fd, 1, timeout) < 0 && errno != EINTR) break;
    }
    XCompositeUnredirectSubwindows(dpy, root, CompositeRedirectManual);
    XCompositeReleaseOverlayWindow(dpy, root);
    XCloseDisplay(dpy);
    return 0;
}
