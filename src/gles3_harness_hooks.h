/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_harness_hooks.h — internal but visible interface between
 * the wayland/EGL harness and the cross-platform ncz_platform
 * abstraction. Hacks include only ncz_platform.h; only the harness
 * and the platform backend know about these.
 *
 * Currently exposes just one hook: frame-size publication into the
 * platform abstraction, so ncz_frame_size() returns live values
 * rather than init-time defaults.
 */
#ifndef GLE3_HARNESS_HOOKS_H
#define GLE3_HARNESS_HOOKS_H

/* Push the live drawable size into the platform abstraction. Called
 * by the harness each time it has a measured framebuffer size. */
void ncz_harness_attach_frame_size(int w, int h);

#endif /* GLE3_HARNESS_HOOKS_H */
