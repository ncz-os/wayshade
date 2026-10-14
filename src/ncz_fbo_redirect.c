// SPDX-License-Identifier: Apache-2.0
//
// ncz_fbo_redirect.c - "framebuffer 0" for off-screen rendering.
//
// Hacks written for a window bind framebuffer 0 to mean "the screen" (for
// example after rendering a post-processing pass into their own FBO). In the
// off-screen frame-source mode (ncz_feed_render.c) there is no window and no
// default framebuffer: binding 0 leaves nothing to draw into, every draw
// fails with GL_INVALID_FRAMEBUFFER_OPERATION and the frame the lock receives
// is black. Every hack binary is linked with -Wl,--wrap=glBindFramebuffer, so
// the calls land here first; while ncz_screen_fbo is set (only in the
// off-screen mode), a bind of 0 binds the frame-source FBO instead. On screen
// ncz_screen_fbo stays 0 and every call passes through unchanged.

#include "ncz_fbo_redirect.h"

GLuint ncz_screen_fbo = 0;

void __real_glBindFramebuffer(GLenum target, GLuint framebuffer);

void __wrap_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (framebuffer == 0 && ncz_screen_fbo != 0)
        framebuffer = ncz_screen_fbo;
    __real_glBindFramebuffer(target, framebuffer);
}
