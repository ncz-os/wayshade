/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "ncz_gpu_guard.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int ncz_renderer_is_software(const char *r) {
    if (!r) return 0;
    return strcasestr(r, "llvmpipe") || strcasestr(r, "softpipe") ||
           strcasestr(r, "swrast") || strcasestr(r, "software rasterizer") ||
           strcasestr(r, "lavapipe");
}

static int hw_driver(const char *d) {
    static const char *hw[] = {"amdgpu", "radeon", "i915", "xe", "nouveau", "nvidia",
                               "nvidia-drm", "panthor", "panfrost", "lima", "msm",
                               "v3d", "vc4", "etnaviv", "asahi", "apple", "mali",
                               "mali_kbase", "tegra", "nvgpu", NULL};
    for (int i = 0; hw[i]; i++) if (!strcmp(d, hw[i])) return 1;
    return 0;
}

int ncz_gpu_hardware_present(const char *root, char *driver, size_t drvsz) {
    char path[PATH_MAX];
    if (!root) root = "";
    struct stat st;
    {
        /* Mali kbase nodes: /dev/mali, /dev/mali0, /dev/mali1, ... */
        snprintf(path, sizeof path, "%s/dev", root);
        DIR *dd = opendir(path);
        if (dd) {
            struct dirent *de;
            while ((de = readdir(dd))) {
                if (strncmp(de->d_name, "mali", 4) == 0) {
                    closedir(dd);
                    if (driver) snprintf(driver, drvsz, "mali_kbase");
                    return 1;
                }
            }
            closedir(dd);
        }
    }
    (void)st;
    snprintf(path, sizeof path, "%s/sys/class/drm", root);
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (strncmp(e->d_name, "renderD", 7) != 0) continue;
        char link[PATH_MAX], target[PATH_MAX];
        snprintf(link, sizeof link, "%s/sys/class/drm/%s/device/driver", root, e->d_name);
        ssize_t n = readlink(link, target, sizeof target - 1);
        if (n <= 0) continue;
        target[n] = 0;
        const char *base = strrchr(target, '/');
        base = base ? base + 1 : target;
        if (hw_driver(base)) { if (driver) snprintf(driver, drvsz, "%s", base); found = 1; }
    }
    closedir(d);
    return found;
}

int ncz_gpu_guard_check(const char *root, const char *gl_renderer,
                        const char *allow_software, char *msg, size_t msgsz) {
    if (!ncz_renderer_is_software(gl_renderer)) return 0;
    char drv[64] = "";
    int hw = ncz_gpu_hardware_present(root, drv, sizeof drv);
    if (hw) {
        snprintf(msg, msgsz, "software renderer '%s' refused: a hardware GPU is present (driver %s). "
                 "Never software render on a machine with a GPU; fix the graphics environment (renderer/EGL vendor).",
                 gl_renderer ? gl_renderer : "?", drv);
        return 1;
    }
    if (allow_software && !strcmp(allow_software, "1")) return 0;
    snprintf(msg, msgsz, "software renderer '%s' refused: no hardware GPU found; set NCZ_ALLOW_SOFTWARE=1 to run on the CPU",
             gl_renderer ? gl_renderer : "?");
    return 1;
}
