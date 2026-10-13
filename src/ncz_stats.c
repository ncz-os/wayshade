/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "ncz_stats.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <GLES3/gl32.h>
#include <EGL/egl.h>

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}

static double t0, t_first, t_prev;
static unsigned long nframes;
static unsigned nc, nl;
static double ms_c, ms_l;
#define CAP 40000
static float early[CAP], steady[CAP];
static int ne, ns;
static float recent[720]; static double recent_t[720]; static int nr;

/* Interposers: the executable's own definitions win over libGLESv2's. */
void glCompileShader(GLuint s) {
    static void (*real)(GLuint);
    if (!real) real = (void (*)(GLuint))dlsym(RTLD_NEXT, "glCompileShader");
    if (!real) real = (void (*)(GLuint))eglGetProcAddress("glCompileShader");
    if (!real) return;
    double a = now_ms();
    real(s);
    ms_c += now_ms() - a; nc++;
}
void glLinkProgram(GLuint p) {
    static void (*real)(GLuint);
    if (!real) real = (void (*)(GLuint))dlsym(RTLD_NEXT, "glLinkProgram");
    if (!real) real = (void (*)(GLuint))eglGetProcAddress("glLinkProgram");
    if (!real) return;
    double a = now_ms();
    real(p);
    ms_l += now_ms() - a; nl++;
}

void ncz_stats_start(void) { t0 = now_ms(); }

void ncz_stats_frame(void) {
    double t = now_ms();
    nframes++;
    if (nframes == 1) { t_first = t - t0; t_prev = t; return; }
    double dt = t - t_prev;
    t_prev = t;
    if (t - t0 < 5000.0) { if (ne < CAP) early[ne++] = (float)dt; }
    else if (ns < CAP) steady[ns++] = (float)dt;
    recent[nr % 720] = (float)dt; recent_t[nr % 720] = t; nr++;
}

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static void pct(float *v, int n, double *p50, double *p95, double *p99, double *mx) {
    *p50 = *p95 = *p99 = *mx = 0;
    if (n <= 0) return;
    qsort(v, n, sizeof(float), cmpf);
    *p50 = v[n / 2]; *p95 = v[(int)(n * 0.95)]; *p99 = v[(int)(n * 0.99)]; *mx = v[n - 1];
}

double ncz_stats_p95_recent(int seconds) {
    double t = now_ms(), cutoff = t - seconds * 1000.0;
    float w[720]; int n = 0;
    int total = nr < 720 ? nr : 720;
    for (int i = 0; i < total; i++) {
        int k = (nr - 1 - i) % 720; if (k < 0) k += 720;
        if (recent_t[k] < cutoff) break;
        w[n++] = recent[k];
    }
    if (n < 30) return -1;
    qsort(w, n, sizeof(float), cmpf);
    return w[(int)(n * 0.95)];
}

void ncz_stats_print(void) {
    double a50, a95, a99, amx, b50, b95, b99, bmx;
    pct(early, ne, &a50, &a95, &a99, &amx);
    pct(steady, ns, &b50, &b95, &b99, &bmx);
    fprintf(stderr,
            "[stats] compiles=%u (%.1f ms) links=%u (%.1f ms) first_frame=%.0f ms frames=%lu | "
            "first5s n=%d p50=%.2f p95=%.2f p99=%.2f max=%.2f | steady n=%d p50=%.2f p95=%.2f p99=%.2f max=%.2f ms\n",
            nc, ms_c, nl, ms_l, t_first, nframes, ne, a50, a95, a99, amx, ns, b50, b95, b99, bmx);
    fflush(stderr);
}
