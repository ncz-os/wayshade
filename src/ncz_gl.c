/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_gl.c - EGL config choice and diagnostic helpers (see ncz_gl.h). */
#include "ncz_gl.h"
#include <stdio.h>

/* Shared config attribute template.  Kept as a pure builder so the
 * colour-surface contract can be unit-tested without a live EGL
 * display.  `want_linear` appends EGL_GL_COLORSPACE (see ncz_gl.h). */
int ncz_gles3_config_attrs(EGLint *out, int cap, bool want_linear) {
    /* 8 base key/value pairs + 1 optional coloursapce pair. */
    enum { BASE_PAIRS = 8, LINEAR_PAIRS = 1 };
    int n = 0;
    if (!out || cap < BASE_PAIRS * 2) return 0;
    out[n++] = EGL_SURFACE_TYPE;    out[n++] = EGL_WINDOW_BIT;
    out[n++] = EGL_RENDERABLE_TYPE; out[n++] = EGL_OPENGL_ES3_BIT;
    out[n++] = EGL_RED_SIZE;        out[n++] = 8;
    out[n++] = EGL_GREEN_SIZE;      out[n++] = 8;
    out[n++] = EGL_BLUE_SIZE;       out[n++] = 8;
    out[n++] = EGL_ALPHA_SIZE;      out[n++] = 8;
    out[n++] = EGL_DEPTH_SIZE;      out[n++] = 16;
    out[n++] = EGL_STENCIL_SIZE;    out[n++] = 0;
    if (want_linear) {
        if (cap < (BASE_PAIRS + LINEAR_PAIRS) * 2) return 0;
        out[n++] = EGL_GL_COLORSPACE;
        out[n++] = EGL_GL_COLORSPACE_LINEAR;
    }
    return n;
}

EGLConfig ncz_gles3_choose_config(EGLDisplay dpy) {
    const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
    bool have_gl_colorspace =
        ext && strstr(ext, "EGL_KHR_gl_colorspace") != NULL;

    EGLConfig cfg;
    EGLint num = 0;

    /* Prefer a linear (non-sRGB) window surface.  If the driver exposes
     * the extension but has no matching config, fall back to a plain
     * RGBA8 config rather than failing to start. */
    if (have_gl_colorspace) {
        EGLint attrs[20];
        int n = ncz_gles3_config_attrs(attrs, 20, true);
        attrs[n] = EGL_NONE;
        if (eglChooseConfig(dpy, attrs, &cfg, 1, &num) && num >= 1)
            return cfg;
    }

    EGLint attrs[20];
    int n = ncz_gles3_config_attrs(attrs, 20, false);
    attrs[n] = EGL_NONE;
    if (!eglChooseConfig(dpy, attrs, NULL, 0, &num) || num < 1)
        return NULL;
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &num) || num < 1)
        return NULL;
    return cfg;
}

int ncz_diag_periodic_samples(void) {
    static int v = -1;
    if (v < 0) {
        const char *d = getenv("NCZ_DIAG_FRAMEBUFFER");
        v = (d && *d && strcmp(d, "0") != 0) ? 1 : 0;
    }
    return v;
}
