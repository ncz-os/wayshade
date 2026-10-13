/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_gl.h - GLES3 and EGL headers plus the few harness helpers every hack
 * binary shares.  Hacks talk to libGLESv2 directly; there is no
 * immediate-mode or matrix-stack emulation layer. */
#ifndef NCZ_GL_H
#define NCZ_GL_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* Defined in gles3_harness.c.  Every fatal exit path must go through this
 * rather than exit(): GL/EGL teardown from an atexit handler crashes on NVIDIA
 * (the vendor driver registers its own atexit handler during context creation,
 * which then runs before ours). */
void ncz_harness_die(int code);

/* EGL_KHR_gl_colorspace / core EGL 1.5 tokens.  Some vendor headers omit
 * the core names, so define the fallbacks we need. */
#ifndef EGL_GL_COLORSPACE
#define EGL_GL_COLORSPACE 0x309D
#endif
#ifndef EGL_GL_COLORSPACE_LINEAR
#define EGL_GL_COLORSPACE_LINEAR 0x308A
#endif

/* Build the shared RGBA8 + depth16 window config attribute list.  When
 * `want_linear` is true, EGL_GL_COLORSPACE=EGL_GL_COLORSPACE_LINEAR is
 * requested so the window surface is NOT sRGB-tagged.  The vendored
 * shaders write display-referred sRGB themselves (they apply their own
 * 1/2.2 gamma), so an sRGB-tagged surface encodes their output a second
 * time on screen while glReadPixels returns the un-encoded value --
 * the "frame dump looks right, monitor looks washed out" defect.
 * Writes at most `cap` EGLint (excluding the EGL_NONE terminator) and
 * returns the count written. */
int ncz_gles3_config_attrs(EGLint *out, int cap, bool want_linear);

/* Default RGBA8 + depth16 window config, linear (non-sRGB) when the
 * display exposes EGL_KHR_gl_colorspace. */
EGLConfig ncz_gles3_choose_config(EGLDisplay dpy);

/* GLES has only the float form of glClearDepth. */
#define glClearDepth(d) glClearDepthf((GLfloat)(d))

/* Periodic diagnostic pixel readbacks stall the GPU pipeline (glReadPixels is a
 * synchronous drain; on Mali-G720 a full-frame sample costs 70-110 ms).  The
 * one-shot startup sample (frame 4) is always kept; periodic ones run only when
 * NCZ_DIAG_FRAMEBUFFER is set to something other than "0".  period must be > 0. */
int ncz_diag_periodic_samples(void);
static inline int ncz_diag_sample_frame(unsigned long frame, unsigned long period) {
    if (frame == 4)
        return 1;
    return period > 0 && frame >= period && (frame % period) == 0 && ncz_diag_periodic_samples();
}

#endif /* NCZ_GL_H */
