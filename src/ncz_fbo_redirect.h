// SPDX-License-Identifier: Apache-2.0
#ifndef NCZ_FBO_REDIRECT_H
#define NCZ_FBO_REDIRECT_H

#include <GLES3/gl3.h>

/* The framebuffer that stands in for "framebuffer 0" (the window) while a hack
 * renders off-screen. 0 = no redirect (on-screen rendering). Set only by
 * ncz_feed_render.c; see ncz_fbo_redirect.c. */
extern GLuint ncz_screen_fbo;

#endif
