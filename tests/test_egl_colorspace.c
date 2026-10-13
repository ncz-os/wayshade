/* SPDX-License-Identifier: Apache-2.0 */
/* test_egl_colorspace.c — regression guard for the harness colour-surface
 * contract.
 *
 * The vendored shaders write display-referred sRGB themselves (they end
 * with pow(col, 1/2.2)).  The window surface must therefore be LINEAR
 * (non-sRGB) or the driver encodes their output a second time and the
 * monitor looks washed out while the frame dump (read before the
 * encode) looks correct.  This test pins the attribute builder that
 * ncz_gles3_choose_config() feeds to eglChooseConfig(). */

#include <assert.h>
#include <stdio.h>

#include "ncz_gl.h"

static int contains_pair(const EGLint *a, int n, EGLint key, EGLint val) {
    for (int i = 0; i + 1 < n; i += 2) {
        if (a[i] == key) return a[i + 1] == val;
    }
    return 0;
}

static int contains_key(const EGLint *a, int n, EGLint key) {
    for (int i = 0; i + 1 < n; i += 2) {
        if (a[i] == key) return 1;
    }
    return 0;
}

int main(void) {
    EGLint a[24];

    /* Default (non-linear) config keeps the historical RGBA8+depth16 set
     * and must NOT tag a coloursapce. */
    int n = ncz_gles3_config_attrs(a, 24, false);
    assert(n > 0);
    assert(contains_pair(a, n, EGL_SURFACE_TYPE, EGL_WINDOW_BIT));
    assert(contains_pair(a, n, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT));
    assert(contains_pair(a, n, EGL_RED_SIZE, 8));
    assert(contains_pair(a, n, EGL_GREEN_SIZE, 8));
    assert(contains_pair(a, n, EGL_BLUE_SIZE, 8));
    assert(contains_pair(a, n, EGL_ALPHA_SIZE, 8));
    assert(contains_pair(a, n, EGL_DEPTH_SIZE, 16));
    assert(contains_pair(a, n, EGL_STENCIL_SIZE, 0));
    assert(!contains_key(a, n, EGL_GL_COLORSPACE));

    /* The shader convention (display-referred sRGB output) requires the
     * harness to request a LINEAR surface explicitly. */
    n = ncz_gles3_config_attrs(a, 24, true);
    assert(n > 0);
    assert(contains_pair(a, n, EGL_GL_COLORSPACE,
                         EGL_GL_COLORSPACE_LINEAR));
    /* The base attributes are still present alongside it. */
    assert(contains_pair(a, n, EGL_SURFACE_TYPE, EGL_WINDOW_BIT));
    assert(contains_pair(a, n, EGL_DEPTH_SIZE, 16));

    /* Too-small buffers are rejected rather than overrun. */
    EGLint tiny[4];
    assert(ncz_gles3_config_attrs(tiny, 4, false) == 0);
    assert(ncz_gles3_config_attrs(tiny, 4, true) == 0);

    printf("egl colorspace contract ok\n");
    return 0;
}
