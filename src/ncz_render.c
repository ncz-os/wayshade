/* SPDX-License-Identifier: Apache-2.0 */
#include "ncz_render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int ncz_render_size(int nw, int nh, double scale, int max_h, int *rw, int *rh) {
    if (nw < 2 || nh < 2) { *rw = nw; *rh = nh; return 0; }
    if (!(scale >= 0.25)) scale = 0.25;
    if (scale > 1.0) scale = 1.0;
    if (scale >= 0.9999 && (max_h <= 0 || nh <= max_h)) { *rw = nw; *rh = nh; return 0; }
    double w = nw * scale, h = nh * scale;
    if (max_h > 0 && h > max_h) {
        double k = (double)max_h / h;
        w *= k;
        h = max_h;
    }
    int ow = ((int)floor(w + 0.5)) & ~1;
    int oh = ((int)floor(h + 0.5)) & ~1;
    if (ow < 2) ow = 2;
    if (oh < 2) oh = 2;
    if (ow >= nw && oh >= nh) { *rw = nw; *rh = nh; return 0; }
    if (ow > nw) ow = nw & ~1;
    if (oh > nh) oh = nh & ~1;
    *rw = ow;
    *rh = oh;
    return 1;
}

int ncz_render_platform_cap(const char *r, int native_h) {
    if (!r) return 0;
    if (strcasestr(r, "mali")) return 1080;
    if (strcasestr(r, "intel") && native_h > 1440) return 1080;
    return 0;
}

const char *ncz_gpu_class(const char *r, const char *env) {
    if (env && !strcasecmp(env, "weak")) return "weak";
    if (env && !strcasecmp(env, "mid")) return "mid";
    if (env && !strcasecmp(env, "strong")) return "strong";
    if (!r) return "mid";
    if (strcasestr(r, "intel") || strcasestr(r, "llvmpipe") || strcasestr(r, "softpipe") || strcasestr(r, "lavapipe")) return "weak";
    if (strcasestr(r, "mali") || strcasestr(r, "adreno") || strcasestr(r, "powervr") || strcasestr(r, "xclipse") || strcasestr(r, "apple")) return "mid";   /* mobile GPUs */
    return "strong";
}

static char g_render_renderer[512];
void ncz_render_set_renderer(const char *r) { snprintf(g_render_renderer, sizeof g_render_renderer, "%s", r ? r : ""); }
const char *ncz_gpu_class_current(void) {
    return ncz_gpu_class(g_render_renderer[0] ? g_render_renderer : NULL, getenv("NCZ_GPU_CLASS"));
}

double ncz_render_step_down(double scale) {
    if (scale > 0.80) return 0.75;
    if (scale > 0.55) return 0.5;
    if (scale > 0.40) return 0.35;
    return scale;
}
