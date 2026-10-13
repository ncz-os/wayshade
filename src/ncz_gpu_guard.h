/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_gpu_guard.h - refuse to run shader hacks on a CPU renderer when the
 * machine has a GPU.
 *
 * Policy: never software render if a hardware GPU exists. NCZ_ALLOW_SOFTWARE=1
 * is honored only on machines with no hardware render node (GPU-less VMs).
 */
#ifndef NCZ_GPU_GUARD_H
#define NCZ_GPU_GUARD_H
#include <stddef.h>

/* 1 if GL_RENDERER names a CPU renderer (llvmpipe, softpipe, swrast, ...). */
int ncz_renderer_is_software(const char *gl_renderer);

/* Probe for a hardware GPU under `root` (empty string for the real system):
 * DRM render nodes whose kernel driver is a hardware driver, or /dev/mali0.
 * Returns 1 and copies the driver name when found. Virtual devices (vgem,
 * vkms, simpledrm, virtio_gpu, ...) do not count. */
int ncz_gpu_hardware_present(const char *root, char *driver, size_t drvsz);

/* Decide. Returns 0 to run, 1 to refuse (msg says why). allow_software is the
 * value of NCZ_ALLOW_SOFTWARE (NULL if unset). */
int ncz_gpu_guard_check(const char *root, const char *gl_renderer,
                        const char *allow_software, char *msg, size_t msgsz);
#endif
