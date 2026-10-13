/* SPDX-License-Identifier: Apache-2.0 */
/* _POSIX_C_SOURCE for sigaction + clock_gettime.
 *
 * _DEFAULT_SOURCE as well, and it is NOT optional: defining
 * _POSIX_C_SOURCE explicitly SUPPRESSES glibc's default feature set, and
 * M_PI lives in the XSI/BSD half of math.h rather than the POSIX half. With
 * only the line above, every vendored hack that does trigonometry fails to
 * compile with M_PI undeclared -- glmatrix.c hits it twice (auto_track,
 * init_matrix). The vendored sources are upstream's and must not be edited
 * to work around our own feature-test macros, so the fix belongs here in
 * the shim they include first. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

/*
 * xscreensaver_compat.h — the PUBLIC shim surface that vendored xscreensaver
 * GL hacks see when compiled into ncz-screensavers.
 *
 * Background: xscreensaver's GL hacks (the C files under hacks/glx/ in the
 * xscreensaver
 * source tree) are written against a header chain that abstracts the GL
 * context + windowing + per-frame dispatch behind a small API:
 *
 *     typedef struct ModeInfo ModeInfo;
 *     void init_<hack>(ModeInfo *);
 *     void draw_<hack>(ModeInfo *);
 *     void reshape_<hack>(ModeInfo *, int, int);
 *     Bool <hack>_handle_event(ModeInfo *, XEvent *);
 *     void free_<hack>(ModeInfo *);
 *     GLXContext *init_GL(ModeInfo *);
 *
 * plus per-frame calls to `glXMakeCurrent` / `glXSwapBuffers`, and a
 * `ModeSpecOpt` table of X resource defaults.
 *
 * On real X11 the implementation is xlockmore.c + xlock-gl-utils.c — they
 * create an X window, a GLX context, and dispatch X events to the hack.
 *
 * In THIS project, we are NOT running X11 or GLX. The window is a
 * wlr-layer-shell surface, the GL context is EGL/GLES2 (or GLES3.2),
 * and the only "events" we care about are key presses (quit) and
 * mouse-button toggles (which glmatrix handles).
 *
 * This header exposes a *subset* of the xlockmore API that
 * (a) compiles a real, vendored xscreensaver GL hack as-is (with
 *     only header-path edits), AND
 * (b) implements the calls via the EGL/Wayland foundation in
 *     wl-screenhack.c — making the hack's GL1 fixed-function calls
 *     flow through GL4ES into our existing EGL context.
 *
 * LICENSING: Vendored xscreensaver hack source files retain their
 * original MIT-style X Consortium "Permission Notice" copyright.
 * This shim is part of ncz-screensavers, Apache-2.0.
 *
 * See PORTING.md §4 for the design rationale.
 */

#ifndef NCZ_HACK_SHIM_H
#define NCZ_HACK_SHIM_H

#include <sys/time.h>
#include <stdint.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>      /* xscreensaver hacks call strlen/strcpy/memcpy etc */
#include <strings.h>     /* strcasecmp (POSIX, in <strings.h> not <string.h>) */
#include <stdlib.h>      /* calloc/free/malloc used by shim + hack calls */
#include <time.h>        /* time/localtime/strftime (dead-code-gated but linked) */
#include <math.h>        /* sin/cos/sqrt used by gluPerspective/gluLookAt shims */
#include <unistd.h>      /* usleep (flurry) */

#include "ncz_gl.h"

/* Anisotropy constants are an extension, not core GLES3. */
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#  define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

/* ----------------------------------------------------------------------- */
/* Section 1 — opaque X11 types (declared, never dereferenced)             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks reference Display, Window, Visual, Drawable, GC, Colormap,
 * Screen, Pixmap, XColor, XGCValues, XWindowAttributes — but only as
 * pointer types or as plain struct fields read via the MI_* macros. They
 * never call any X11 function on them (except via ximage-loader.c, which
 * we replace). Forward-declaring as opaque structs is sufficient.
 *
 * EXCEPTION: hacks call XGetPixel(xi, x, y) and XPutPixel(xi, x, y, pixel)
 * directly on XImage* (line 742, 744, 767, 774 of glmatrix.c). We DO
 * implement those — see section 6.
 */

typedef struct _Display   Display;
typedef unsigned long     Window;          /* XID -- an integer, not a pointer/struct; matches real Xlib.h */
typedef struct _Visual    Visual;
typedef unsigned long     Drawable;      /* XID, as in real Xlib. It was an
                                           incomplete struct, which CONFLICTS with
                                           X11/X.h if anything drags the real headers
                                           in: "conflicting types for 'Drawable';
                                           have 'XID'". */
typedef struct _GC        GC;
/* X11 keysym constants (values from X11/keysymdef.h). Hacks compare the KeySym
 * returned by XLookupString against these to implement arrow-key navigation.
 * Our XLookupString always returns 0, so no branch is ever taken -- but the
 * constants must EXIST for the comparison to compile. */
#define XK_BackSpace  0xff08
#define XK_Tab        0xff09
#define XK_Return     0xff0d
#define XK_Escape     0xff1b
#define XK_Home       0xff50
#define XK_Left       0xff51
#define XK_Up         0xff52
#define XK_Right      0xff53
#define XK_Down       0xff54
#define XK_Prior      0xff55
#define XK_Page_Up    0xff55
#define XK_Next       0xff56
#define XK_Page_Down  0xff56
#define XK_End        0xff57
#define XK_Begin      0xff58
#define XK_Delete     0xffff
#define XK_space      0x0020
#define XK_plus       0x002b
#define XK_minus      0x002d
#define XK_equal      0x003d
#define XK_less       0x003c
#define XK_greater    0x003e
/* Shift keysyms — used by hypertorus's event handler to detect
 * shift-modified drag. Like all our XK_* constants, never actually
 * compared because the shim's XLookupString returns 0, but must
 * exist for the comparison to compile. */
#define XK_Shift_L    0xffe1
#define XK_Shift_R    0xffe2

typedef unsigned long     KeySym;        /* XID. Hacks declare `KeySym keysym` in
                                           their event handlers; they compare it
                                           against XK_* constants and otherwise
                                           treat it as an opaque integer. */
typedef unsigned long     Colormap;      /* XID, like Window above -- real Xlib makes this an integer, not a struct. Passing it by value (as the hacks do) needs a complete type. */
typedef struct _Screen     Screen;
typedef struct _Pixmap     Pixmap;
typedef struct _GLXContext *GLXContext;
/* XColor is DEFINED, not forward-declared.
 *
 * The xlockmore GL hacks do not treat XColor as opaque: they calloc arrays of
 * it (so they need sizeof) and read .red/.green/.blue to feed glColor3f. An
 * incomplete type compiles right up until first use and then fails with
 * "invalid use of undefined type" / "invalid application of sizeof", which is
 * what blocked dangerball, cubestack, cubestorm, glknots, hexstrut and
 * hypnowheel together.
 *
 * Layout and semantics match Xlib: the colour channels are 16-bit, 0..65535,
 * which is why the hacks divide by 65536.0 to get a 0..1 float. `pixel` and
 * `flags` are carried for source compatibility only -- there is no X server
 * here and nothing consumes them. */
typedef struct _XColor {
    unsigned long  pixel;
    unsigned short red, green, blue;
    char           flags;
    char           pad;
} XColor;
typedef struct {
    short x, y;
} XPoint;
typedef struct {
    short x, y;
    unsigned short width, height;
} XRectangle;
/* XChar2b — 2-byte packed character. Used by utf8wc.h and by hacks
 * (gltext, starwars, etc.) that render Latin1 via the XChar2b array API.
 * The layout matches Xlib: low byte first, high byte second. We carry
 * the struct as-is so the utf8_to_XChar2b / XChar2b_to_utf8 helpers in
 * xscreensaver_compat.c can shuffle bytes. */
typedef struct {
    unsigned char byte1;
    unsigned char byte2;
} XChar2b;
typedef struct _XGCValues  XGCValues;
/* XImage is fully defined in section 6 below. */
/* XEvent — see union definition below in this section. We use a union of
 * minimal structs; the hack only reads xany.type (offset 0) and
 * xbutton.button (a fixed offset we control). The harness synthesizes
 * events and writes to the same struct definitions, so layout is
 * self-consistent regardless of whether it matches real Xlib. */
typedef struct _wl_display wl_display;

typedef struct {
    int type;
    /* Pad so the named fields land at offsets we control. The harness writes
     * through these same definitions, so the layout only has to be
     * self-consistent -- it does not have to match real Xlib.
     *
     * x/y are REQUIRED, not decorative: hacks that implement drag or
     * click-to-focus read event->xbutton.x, and without them the port fails
     * with "'XButtonEvent' has no member named 'x'". */
    unsigned char _pad_to_button[72];
    unsigned int button;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned char _rest[192 - 76 - 20];
} XButtonEvent;

typedef struct {
    int type;
    unsigned char _rest[192 - sizeof(int)];
} XAnyEvent;

typedef struct {
    int type;
    /* Same padding discipline as XButtonEvent: the union members are all 192
     * bytes so every member's fields sit at offsets we control, and the
     * harness writes through these same definitions. Only `type` and
     * `keycode` are ever read. */
    unsigned char _pad_to_keycode[72];
    unsigned int  keycode;
    int x, y;
    unsigned int state;
    unsigned char _rest[192 - 76 - 12];
} XKeyEvent;

typedef struct {
    int type;
    /* Same 192-byte discipline as the other members. Hacks that track the
     * pointer read xmotion.x/.y; ours are never written to by anything, since
     * the input surface is inert, but the fields must exist to compile. */
    unsigned char _pad_to_xy[72];
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned char _rest[192 - 72 - 20];
} XMotionEvent;

typedef union {
    int type;
    XAnyEvent    xany;
    XButtonEvent xbutton;
    XKeyEvent    xkey;
    XMotionEvent xmotion;
    unsigned char _pad[192];   /* round to real XEvent size */
} XEvent;

/* XEvent type constants used by glmatrix. The full X11 event type set
 * would be a lot more — we only define what vendored hacks actually
 * switch on. */
#define ButtonPress    4
#define ButtonRelease  5
#define KeyPress       2
#define KeyRelease     3
#define MotionNotify   6
#define ConfigureNotify 22
#define Expose         12
#define ClientMessage  33

#ifndef None
#define None 0L
#endif
#define ZPixmap 2

#define Button1   1
#define Button2   2
#define Button3   3
#define Button4   4
#define Button5   5
#define Button6   6
#define Button7   7

/* ----------------------------------------------------------------------- */
/* Section 2 — Bool / True / False                                         */
/* ----------------------------------------------------------------------- */

typedef int Bool;
#ifndef True
#define True  1
#endif
#ifndef False
#define False 0
#endif
/* Mapscroller.c uses the all-caps Xlib spellings. */
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* ----------------------------------------------------------------------- */
/* Section 3 — ModeInfo struct                                             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks access fields through the MI_* macros defined below.
 * Each MI_* macro expands to a member access on ModeInfo*. The struct
 * below is what those macros deref. Fields used by glmatrix:
 *   - dpy                : Display*
 *   - window             : Window
 *   - xgwa.{visual, width, height}  : for image loading
 *   - polygon_count      : FPS bookkeeping (we stub)
 *   - fps_p              : FPS display toggle (we stub)
 *   - glx_context        : GLXContext* (typedef'd to our egl_data*)
 *
 * We use HAVE_EGL semantics — glx_context is repurposed as a pointer to
 * our internal EGL context wrapper. See xscreensaver_compat.c.
 */
struct _XWindowAttributes {
    int     depth;        /* only referenced via MI_DEPTH (we don't use) */
    void   *visual;       /* MI_VISUAL → xgwa.visual */
    Screen *screen;       /* grab-ximage callers pass this back unchanged */
    Colormap colormap;    /* MI_WIN_COLORMAP -> xgwa.colormap */
    int     width;        /* MI_WIN_WIDTH */
    int     height;       /* MI_WIN_HEIGHT */
};
typedef struct _XWindowAttributes XWindowAttributes;

struct ModeInfo {
    /* Fields used directly by glmatrix.c */
    Display            *dpy;
    Window              window;
    XWindowAttributes   xgwa;
    int                 screen_number;
    unsigned long       polygon_count;   /* we ignore, fps_p is off */
    int                 fps_p;           /* we ignore */
    void               *glx_context;     /* pointer to our egl_data_t */
    /* Fields accessed by MI_* macros that other hacks might use. We
     * define them with safe defaults; glmatrix never touches most. */
    int                 npixels;
    unsigned long      *pixels;
    XColor             *colors;
    Bool                writable_p;
    unsigned long       white, black;
    void               *gc;
    long                pause;
    Bool                fullrandom;
    long                cycles, batchcount, size;
    Bool                threed;
    long                threed_left_color, threed_right_color;
    long                threed_both_color, threed_none_color;
    long                threed_delta;
    Bool                wireframe_p;
    /* Reached for directly by some hacks (geodesic and friends) rather than
     * through an MI_* accessor, so it has to be a real field. Recursion/
     * subdivision depth for the generated geometry. */
    int                 recursion_depth;
    Bool                is_drawn;
    Bool                root_p;
    void               *eraser;
    Bool                needs_clear;
    void               *fpst;
    /* Per-hack private state slot. Vendored hacks that need to
     * store arbitrary per-screen state (e.g. hyprsaver's GL
     * program/VBO cache) set this in their init callback and
     * free it in their free callback. NULL is the initial
     * value; the harness never touches it. */
    void               *data;
};
typedef struct ModeInfo ModeInfo;

/* ----------------------------------------------------------------------- */
/* Section 4 — MI_* accessor macros                                        */
/* ----------------------------------------------------------------------- */
/*
 * xscreensaver's xlockmore.h defines these. We replicate exactly what
 * glmatrix uses; the rest are harmless identity macros that resolve to
 * safe struct fields.
 */
#define MI_DISPLAY(MI)             ((MI)->dpy)
#define MI_WINDOW(MI)              ((MI)->window)
#define MI_NUM_SCREENS(MI)         (1)             /* always single-screen */
#define MI_SCREEN(MI)              ((MI)->screen_number)
#define MI_SCREENPTR(MI)           ((MI)->xgwa.visual)  /* unused */
#define MI_WIN_WHITE_PIXEL(MI)     ((MI)->white)
#define MI_WIN_BLACK_PIXEL(MI)     ((MI)->black)
#define MI_NPIXELS(MI)             ((MI)->npixels)
#define MI_PIXEL(MI, N)            ((MI)->pixels[(N)])
#define MI_WIN_WIDTH(MI)           ((MI)->xgwa.width)
#define MI_WIN_HEIGHT(MI)          ((MI)->xgwa.height)
#define MI_WIN_DEPTH(MI)           ((MI)->xgwa.depth)
#define MI_DEPTH(MI)               ((MI)->xgwa.depth)
#define MI_WIN_COLORMAP(MI)        ((MI)->xgwa.colormap)
#define MI_COLORMAP(MI)            MI_WIN_COLORMAP(MI)
#define MI_VISUAL(MI)              ((MI)->xgwa.visual)
#define MI_GC(MI)                  ((MI)->gc)
#define MI_PAUSE(MI)               ((MI)->pause)
#define MI_DELAY(MI)               ((MI)->pause)
#define MI_WIN_IS_FULLRANDOM(MI)   ((MI)->fullrandom)
#define MI_WIN_IS_VERBOSE(MI)      (0)
#define MI_WIN_IS_INSTALL(MI)      (0)
#define MI_WIN_IS_MONO(MI)         (0)
#define MI_WIN_IS_INROOT(MI)       ((MI)->root_p)
#define MI_WIN_IS_INWINDOW(MI)     (!((MI)->root_p))
#define MI_WIN_IS_ICONIC(MI)       (0)
#define MI_WIN_IS_WIREFRAME(MI)    ((MI)->wireframe_p)
/* Upstream xlockmore exposes BOTH spellings and the hacks use them
 * interchangeably -- glmatrix.c calls MI_IS_WIREFRAME in three places. Without
 * the alias it compiles as an implicit function declaration and then fails to
 * link, which reads as a missing symbol rather than a missing macro. */
#define MI_IS_WIREFRAME(MI)        MI_WIN_IS_WIREFRAME(MI)
#define MI_WIN_IS_USE3D(MI)        ((MI)->threed)
#define MI_LEFT_COLOR(MI)          ((MI)->threed_left_color)
#define MI_RIGHT_COLOR(MI)         ((MI)->threed_right_color)
#define MI_BOTH_COLOR(MI)          ((MI)->threed_both_color)
#define MI_NONE_COLOR(MI)          ((MI)->threed_none_color)
#define MI_DELTA3D(MI)             ((MI)->threed_delta)
#define MI_CYCLES(MI)              ((MI)->cycles)
#define MI_BATCHCOUNT(MI)          ((MI)->batchcount)
#define MI_SIZE(MI)                ((MI)->size)
#define MI_IS_DRAWN(MI)            ((MI)->is_drawn)
#define MI_IS_FPS(MI)              ((MI)->fps_p)
#define MI_NCOLORS(MI)             ((MI)->npixels)
#define MI_NAME(MI)                (progname)
/* Aliases */
#define MI_WIDTH(MI)               (MI_WIN_WIDTH((MI)))
#define MI_HEIGHT(MI)              (MI_WIN_HEIGHT((MI)))
#define MI_COUNT(MI)               (MI_BATCHCOUNT((MI)))
#define MI_BLACK_PIXEL(MI)         (MI_WIN_BLACK_PIXEL((MI)))
#define MI_WHITE_PIXEL(MI)         (MI_WIN_WHITE_PIXEL((MI)))
#define MI_IS_FULLRANDOM(MI)       (MI_WIN_IS_FULLRANDOM((MI)))
#define MI_IS_VERBOSE(MI)          (MI_WIN_IS_VERBOSE((MI)))
#define MI_IS_INSTALL(MI)          (MI_WIN_IS_INSTALL((MI)))
#define MI_IS_DEBUG(MI)            (0)
/* xlockmore's per-option description table. Hacks declare a static array of
 * these alongside their ModeSpecOpt; nothing here reads it, but it must be a
 * complete type for the declaration to compile. */
typedef struct {
    char *opt;
    char *desc;
} OptionStruct;

/* Monochrome / colour-depth predicates. There is no X visual here and the
 * surface is always 32-bit RGBA, so mono is always false and the colour tests
 * are always true. Hacks use these to pick a drawing path; taking the colour
 * path unconditionally is correct for us. */
/* Clear the drawing surface. Upstream expands to an XClearWindow plus a GL
 * clear; we only ever have the GL surface. */
#define MI_CLEARWINDOW(MI)  do { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); } while (0)

#define MI_IS_MONO(MI)      (0)
#define MI_IS_INROOT(MI)    (0)
#define MI_IS_INWINDOW(MI)  (1)
#define MI_IS_ICONIC(MI)    (0)

#define MI_IS_MOUSE(MI)            (0)

/* MI_INIT — allocates per-screen state array. On real xlockmore this
 * is a multi-screen-aware alloc; we always have one screen. */
extern void xlockmore_mi_init(ModeInfo *mi, size_t sz, void **parray);
#define MI_INIT(mi, sa) \
    xlockmore_mi_init((mi), sizeof(*(sa)), (void **)&(sa))
#define MI_ABORT(mi)              abort()
#define FreeAllGL(dpy)             /* no-op on our pipeline */

/* ----------------------------------------------------------------------- */
/* Section 5 — math macros + utility macros                                */
/* ----------------------------------------------------------------------- */

#define SINF(n)   ((float)sin((double)(n)))
#define COSF(n)   ((float)cos((double)(n)))
#define FABSF(n)  ((float)fabs((double)(n)))

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef ABS
#define ABS(a)   ((a) < 0 ? -(a) : (a))
#endif
#ifndef NRAND
#define NRAND(n) ((int)(random() % (long)(n)))
#endif
#ifndef LRAND
#define LRAND() ((long)(random() & 0x7fffffff))
#endif
#ifndef MAXRAND
#define MAXRAND (2147483648.0)
#endif

#define NUMCOLORS 256

/* frand(n) — uniform random in [0, n). Uses libc random(). Deterministic
 * seed at startup so the screensaver is reproducible across runs. */
extern double frand(double n);
#define countof(a) (sizeof(a) / sizeof((a)[0]))


/* image_data_to_ximage — loads a PNG into a freshly allocated XImage.
 * Backed by libpng. The `Display *` and `Visual *` args are IGNORED — we
 * always produce an RGBA8 byte buffer. */
/* Colour ramp generation (utils/colors.c upstream).
 *
 * Fills `colors` with a smooth, cyclic ramp of *ncolorsP entries. The
 * Screen/Visual/Colormap arguments exist for source compatibility and are
 * IGNORED: there is no X server and no palette to allocate into, so
 * allocate_p/writable_pP are likewise inert. The hacks pass 0/0/0/False/0.
 */
/* ------------------------------------------------------------------------- */
/* Input plumbing                                                            */
/*                                                                           */
/* These exist so hacks that offer mouse/keyboard interaction COMPILE and     */
/* LINK. A screensaver drawing a background has no interactive user, so the   */
/* handlers are inert by design rather than unimplemented by accident:        */
/* they consistently report "event not handled", which is exactly what the    */
/* hacks do when a real X server delivers them nothing.                       */
/*                                                                           */
/* If the engine later grows real input (the lock surface will need it), this */
/* is the seam to implement against -- the signatures already match upstream. */
/* ------------------------------------------------------------------------- */

/* Opaque trackball state. Upstream tracks a quaternion built from drag
 * deltas; with no pointer events the rotation is simply identity, so the
 * hack's own idle spin (its `spin`/`wander` options) drives the view. */
typedef struct trackball_state trackball_state;

extern trackball_state *gltrackball_init(int ignore_device_rotation_p);
extern void gltrackball_rotate(trackball_state *ts);
extern void gltrackball_reset(trackball_state *ts, float x, float y);
extern Bool gltrackball_event_handler(XEvent *event, trackball_state *ts,
                                      int window_width, int window_height,
                                      Bool *button_down_p);
extern void gltrackball_free(trackball_state *ts);
extern void gltrackball_stop(trackball_state *ts);
extern void gltrackball_get_quaternion(trackball_state *ts, float q[4]);
extern void gltrackball_start(trackball_state *ts, int x, int y, int w, int h);
extern void gltrackball_track(trackball_state *ts, int x, int y, int w, int h);
extern void gltrackball_mousewheel(trackball_state *ts, int button, int percent,
                                   int flip_p);
extern double gltrackball_get_x(trackball_state *ts);
extern double gltrackball_get_y(trackball_state *ts);

/* Generic per-hack event dispatch used by the xlockmore GL hacks. */
extern Bool screenhack_event_helper(Display *dpy, Window window, XEvent *event);

/* Xlib keyboard decode. Returns the number of characters written to `buffer`. */
extern int XLookupString(XKeyEvent *event, char *buffer, int nbytes,
                         KeySym *keysym, void *status);

/* HSV <-> RGB in Xlib's 16-bit channel space (utils/colors.c upstream). */
extern void hsv_to_rgb(int h, double s, double v,
                       unsigned short *r, unsigned short *g, unsigned short *b);
extern void rgb_to_hsv(unsigned short r, unsigned short g, unsigned short b,
                       int *h, double *s, double *v);

extern void make_smooth_colormap(Screen *screen, Visual *visual, Colormap cmap,
                                 XColor *colors, int *ncolorsP,
                                 Bool allocate_p, Bool *writable_pP,
                                 Bool verbose_p);
extern void make_random_colormap(Screen *screen, Visual *visual, Colormap cmap,
                                 XColor *colors, int *ncolorsP,
                                 Bool bright_p, Bool allocate_p,
                                 Bool *writable_pP, Bool verbose_p);
extern void make_uniform_colormap(Screen *screen, Visual *visual, Colormap cmap,
                                  XColor *colors, int *ncolorsP,
                                  Bool allocate_p, Bool *writable_pP,
                                  Bool verbose_p);
extern void make_color_ramp(Screen *screen, Visual *visual, Colormap cmap,
                            int h1, double s1, double v1,
                            int h2, double s2, double v2,
                            XColor *colors, int *ncolorsP,
                            Bool closed_p,
                            Bool allocate_p,
                            Bool *writable_pP);
extern void make_color_loop(Screen *screen, Visual *visual, Colormap cmap,
                            int h1, double s1, double v1,
                            int h2, double s2, double v2,
                            int h3, double s3, double v3,
                            XColor *colors, int *ncolorsP,
                            Bool allocate_p,
                            Bool *writable_pP);
extern int XParseColor(Display *dpy, Colormap cmap, const char *spec,
                       XColor *exact_def_return);


/* ----------------------------------------------------------------------- */
/* Section 7 — xrm/X resources                                             */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks declare XrmOptionDescRec opts[] tables (for
 * command-line parsing) and ModeSpecOpt {count, opts, ...} globals.
 * We don't parse command-line options, but we need the TYPES to compile.
 */

#define XrmoptionNoArg  3    /* value not actually used by us */
#define XrmoptionSepArg 1
#define XrmoptionIsArg  2
#define XrmoptionStickyArg 4
#define XrmoptionResArg    5    /* resource name only */
#define XrmoptionSkipArg   7    /* skip this arg */

typedef struct {
    char *option;       /* command-line flag, e.g. "-clock" */
    char *specifier;    /* X resource spec, e.g. ".clock" */
    int   argKind;      /* one of XrmoptionNoArg etc. */
    void *value;        /* string value, e.g. "True" */
} XrmOptionDescRec;

/*
 * xlockmore option-variable declarations.  Despite its historical name,
 * `argtype` is the complete table-entry struct (not the enum): hacks declare
 * `static argtype vars[] = { ... }`.  The final member is the parser tag.
 *
 * glmatrix currently uses t_String, t_Float, and t_Bool.  t_Int is included
 * because it is part of the standard xlockmore declaration machinery and is
 * used by many other hacks.  The table is metadata only in this port for now;
 * ModeSpecOpt retains it for a future argv/resource-compatible consumer.
 */
typedef enum {
    t_String,
    t_Float,
    t_Int,
    t_Bool
} argtype_tag;

typedef struct {
    void        *var;        /* pointer to the variable to set */
    char        *name;       /* X resource name */
    char        *classname;  /* X resource class */
    char        *def;        /* default value string */
    argtype_tag  type;       /* how the framework parses def/overrides */
} argtype;

typedef argtype ModeSpecVar;

typedef struct {
    int             numopts;
    XrmOptionDescRec *opts;
    int             numvars;
    ModeSpecVar     *vars;
    void           *dummy;
} ModeSpecOpt;

#define ENTRYPOINT static

/* get_string_resource — we don't parse X resources; return the default
 * string the hack passed in via DEFAULTS or DEF_FOO. The hack's var
 * tables store the default strings, so we just return those. We provide
 * a stub that returns an empty string or the second arg — most hacks
 * don't actually USE these in a Wayland port. */
extern char *get_string_resource(void *ctx, const char *res_name,
                                 const char *res_class);
/* Walk a hack's ModeSpecVar table and write each entry's default through its
 * var pointer. MUST be called before the hack's init_cb: without it every
 * tunable sits at its BSS default and the hack silently misbehaves. */
extern void  xs_compat_apply_var_defaults(ModeSpecOpt *o, const char *defaults_str);
extern void  xs_compat_apply_mode_defaults(ModeInfo *mi);

extern Bool  get_boolean_resource(void *ctx, const char *res_name,
                                  const char *res_class);
extern int   get_integer_resource(void *ctx, const char *res_name,
                                 const char *res_class);
extern double get_float_resource(void *ctx, const char *res_name,
                                 const char *res_class);

/* ----------------------------------------------------------------------- */
/* Section 8 — GLX passthroughs (these are the key bridge to EGL)          */
/* ----------------------------------------------------------------------- */
/*
 * In real xscreensaver, glXMakeCurrent binds a GLXContext to a drawable
 * (X Window). In our pipeline, GL4ES is intercepting all gl* calls and
 * using the EGL context that's already current. So:
 *
 *   - glXMakeCurrent(D, W, C)  →  we record C as the active GL context,
 *                                and (if necessary) eglMakeCurrent it.
 *                                In practice, the EGL context is already
 *                                current from the harness; we no-op.
 *   - glXSwapBuffers(D, W)     →  eglSwapBuffers(egl_display, egl_surface)
 *
 * The vendor hack compiles these calls against the GL/glx.h symbols,
 * which we provide here as wrappers that dispatch into the harness's
 * EGL state via a global pointer (set by the harness before the first
 * call).
 */

extern int  glXMakeCurrent(Display *dpy, Window drawable, GLXContext ctx);
extern void glXSwapBuffers(Display *dpy, Window drawable);
extern void glXDestroyContext(Display *dpy, GLXContext ctx);

/* init_GL — the central "give me a GL context" entry point. On real
 * xlockmore this picks a GLX visual, creates a context, binds it.
 * For us, this is called once per hack from its init_<name> callback.
 * We simply record the ModeInfo* in a global so subsequent glX* calls
 * know which surface/context to use, and return a non-NULL sentinel. */
extern void *init_GL(ModeInfo *mi);

/* clear_gl_error / check_gl_error — diagnostic helpers. Real xscreensaver
 * uses these around GL calls to spot driver bugs. We implement them via
 * glGetError. */
extern void clear_gl_error(void);
extern void check_gl_error(const char *type);

/* current_device_rotation — 0 on non-mobile (X11/Wayland). */
extern double current_device_rotation(void);

/* do_fps — only called if mi->fps_p is set. We stub it; fps_p is always
 * false in our ModeInfo, so this is never reached. */
extern void do_fps(ModeInfo *mi);

/* xlockmore_no_events — used by the daemon to indicate "no input events
 * queued". We don't dispatch real X events to hacks; we just return True. */
extern Bool xlockmore_no_events(ModeInfo *mi, XEvent *event);

/* ----------------------------------------------------------------------- */
/* Section 9 — registration                                                */
/* ----------------------------------------------------------------------- */
/*
 * The xlockmore framework's XSCREENSAVER_MODULE_2 macro emits two globals:
 *   - NAME##_xlockmore_function_table   (raw function pointers + opts)
 *   - NAME##_xscreensaver_function_table (wires through xlockmore_setup)
 *
 * For our pipeline we ONLY need the xscreensaver_function_table struct,
 * because the harness reads the function pointers out of it directly to
 * drive the hack's lifecycle. We provide a streamlined macro that emits
 * only what our harness needs.
 *
 * Schema of our xscreensaver_function_table (see xscreensaver_compat.c):
 *   - init_cb:      void (*)(ModeInfo *)
 *   - draw_cb:      void (*)(ModeInfo *)
 *   - reshape_cb:   void (*)(ModeInfo *, int, int)
 *   - event_cb:     Bool (*)(ModeInfo *, XEvent *)
 *   - free_cb:      void (*)(ModeInfo *)
 *   - release_cb:   void (*)(ModeInfo *)  -- may be NULL
 *   - opts:         ModeSpecOpt *
 *   - name:         const char *
 *
 * XSCREENSAVER_MODULE_2(CLASS, NAME, PREFIX) wires:
 *   PREFIX##_init    → init_cb
 *   PREFIX##_draw    → draw_cb
 *   PREFIX##_reshape → reshape_cb
 *   PREFIX##_handle_event → event_cb
 *   PREFIX##_free    → free_cb
 *   PREFIX##_release (or 0 if not defined) → release_cb
 *   PREFIX##_opts    → opts (or NULL)
 *
 * The CLASS arg is the user-visible category string (e.g. "GLMatrix");
 * NAME is the function-table struct name (typically the lowercase hack
 * name, e.g. "glmatrix"). PREFIX is the symbol stem (e.g. "matrix"),
 * matching the hack's PREFIX##_init, PREFIX##_draw, etc.
 */
struct xscreensaver_function_table {
    const char  *name;          /* e.g. "glmatrix" */
    const char  *class_;        /* e.g. "GLMatrix" */
    void       (*init_cb)(ModeInfo *);
    void       (*draw_cb)(ModeInfo *);
    void       (*reshape_cb)(ModeInfo *, int, int);
    Bool       (*event_cb)(ModeInfo *, XEvent *);
    void       (*free_cb)(ModeInfo *);
    void       (*release_cb)(ModeInfo *);  /* may be NULL */
    ModeSpecOpt *opts;          /* may be NULL */
    /* Embedded X-resources defaults block (the DEFAULTS macro at the top
     * of the vendored .c file). Lines look like "*name: value\n" and carry
     * the values that were never put into the vars[] table because
     * upstream considered them "defaults, not tunables" -- typically the
     * color and font names a hack reads via get_string_resource(). NULL
     * when the hack has no such block (juggler3d, lockward). */
    const char  *defaults_str;
};

/* Default-empty DEFAULTS for hacks that omit it (juggler3d, lockward).
 * The xscreensaver_function_table defaults_str field will point at this. */
#ifndef DEFAULTS
#define DEFAULTS ""
#endif

#define XSCREENSAVER_MODULE_2(CLASS, NAME, PREFIX)                          \
    struct xscreensaver_function_table                                       \
        NAME##_xscreensaver_function_table = {                               \
            .name        = #NAME,                                            \
            .class_      = (CLASS),                                          \
            .init_cb     = init_##PREFIX,                                    \
            .draw_cb     = draw_##PREFIX,                                    \
            .reshape_cb  = reshape_##PREFIX,                                 \
            .event_cb    = PREFIX##_handle_event,                            \
            .free_cb     = free_##PREFIX,                                    \
            .release_cb  = release_##PREFIX,                                 \
            .opts        = &PREFIX##_opts,                                   \
            .defaults_str = DEFAULTS,                                        \
        };

#define XSCREENSAVER_MODULE(CLASS, PREFIX) \
    XSCREENSAVER_MODULE_2(CLASS, PREFIX, PREFIX)

/* ----------------------------------------------------------------------- */
/* Section 10 — globals                                                    */
/* ----------------------------------------------------------------------- */

extern const char *progname;     /* set by harness to argv[0] or "ncz-screensavers" */
extern Bool         mono_p;      /* we always set to False */

/* ----------------------------------------------------------------------- */
/* Section 11 — harness globals (set by glmatrix_harness.c before init_*)  */
/* ----------------------------------------------------------------------- */
/*
 * The harness (glmatrix_harness.c) owns the Wayland display, the EGL
 * display/surface/context, and the size. These globals expose that state
 * to the shim, which routes glX* calls and other compat APIs into it.
 */
extern void *g_harness_egl_display;   /* EGLDisplay */
extern void *g_harness_egl_surface;   /* EGLSurface */
extern void *g_harness_egl_context;   /* EGLContext */
extern int   g_harness_width;
extern int   g_harness_height;
extern void *g_harness_xscreen;       /* Display* stub for hacks to round-trip */
extern int   g_harness_initialized;   /* 1 once the harness has set the others */

#endif /* NCZ_HACK_SHIM_H */
