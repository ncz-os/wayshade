/* SPDX-License-Identifier: Apache-2.0 */
/* Test for the blackhole fp32 linear sample accumulator.
 *
 * The capture-quality pipeline (--capture=PATH) sums capture-supersample^2
 * jittered GLES3 passes per output pixel in linear radiance before the
 * display transform is applied in C. 32-64 sample artist masters expose the
 * precision floor of the GPU-side render target and the host-side
 * accumulator when HDR radiance values vary by orders of magnitude (the
 * disk ranges from a cold ~1e-3 jet to a hot ~1e4 photon ring, so the
 * running sum has to hold small differences after subtracting large totals
 * in the final divide).
 *
 * This test is a CPU-only reference for the host-side accumulator. The
 * precision gap it exposes is exactly the gap the live GPU saw when the
 * FBO was GL_RGBA16F: per-sample writes were clamped/rounded to half-float
 * storage, so the readback added a half-float-relative error per sample that
 * did not cancel in the mean. The fp32 FBO keeps each per-sample write at
 * full fp32 precision, the same way the host accumulator below does.
 *
 * The positive gate: a deterministic 64-sample sum that the fp32 path
 * preserves within fp32 ulp of the input magnitude. The negative gate: the
 * same loop with per-sample writes rounded to half-float must NOT achieve
 * the same precision (proves the precision gap the renderer fix closes).
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static int fails;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* IEEE-754 binary16 (1 sign, 5 exponent, 10 mantissa) round-trip. Pure-C,
 * so the test does not need a hardware FBO to expose the precision gap. */
static uint16_t f32_to_f16_(float v) {
    uint32_t b; memcpy(&b, &v, sizeof b);
    uint32_t s = (b >> 16) & 0x8000u;
    int32_t e = (int32_t)((b >> 23) & 0xff) - 127 + 15;
    uint32_t m = b & 0x7fffffu;
    if (e <= 0) {
        if (e < -10) return (uint16_t)s;
        m = (m | 0x800000u) >> (uint32_t)(1 - e);
        if (m & 0x1000u) m += 0x2000u;
        return (uint16_t)(s | (m >> 13));
    }
    if (e >= 31) {
        if (e == 31 && !(m & 0x400000u)) return (uint16_t)(s | 0x7c00u | (m >> 13));
        return (uint16_t)(s | 0x7c00u);
    }
    if (m & 0x1000u) m += 0x2000u;
    return (uint16_t)(s | ((uint32_t)e << 10) | (m >> 13));
}
static float f16_to_f32_(uint16_t h) {
    uint32_t s = (uint32_t)(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1fu;
    uint32_t m = h & 0x3ffu;
    uint32_t b;
    if (e == 0) {
        if (m == 0) b = s;
        else {
            while (!(m & 0x400u)) m <<= 1, e--;
            e++; m &= 0x3ffu;
            b = s | ((e + 127 - 15) << 23) | (m << 13);
        }
    } else if (e == 31) {
        if (m == 0) b = s | 0x7f800000u;
        else b = s | 0x7f800000u | (m << 13);
    } else {
        b = s | ((e + 127 - 15) << 23) | (m << 13);
    }
    float v; memcpy(&v, &b, sizeof v); return v;
}

/* Per-pixel strip-sum the C side of bh_capture_still does. The fp32 path
 * keeps the running accumulator in fp32 the whole time. */
static void run_fp32(float *acc, const float *radiance, int ns, size_t npix) {
    memset(acc, 0, npix * 3 * sizeof(float));
    for (int k = 0; k < ns; k++) {
        const float *q = radiance + (size_t)k * npix * 3;
        for (size_t i = 0; i < npix; i++) {
            acc[3 * i]     += q[3 * i];
            acc[3 * i + 1] += q[3 * i + 1];
            acc[3 * i + 2] += q[3 * i + 2];
        }
    }
    float inv = 1.0f / (float)ns;
    for (size_t i = 0; i < npix * 3; i++) acc[i] *= inv;
}

/* The legacy half-float path: every per-sample write to the FBO is rounded
 * to half-float, the way the GL_RGBA16F storage forced the GPU to do. The
 * readback converts back to fp32 but the precision is already lost. */
static void run_fp16_roundtrip(float *acc, const float *radiance, int ns, size_t npix) {
    memset(acc, 0, npix * 3 * sizeof(float));
    for (int k = 0; k < ns; k++) {
        const float *q = radiance + (size_t)k * npix * 3;
        for (size_t i = 0; i < npix; i++) {
            float s0 = f16_to_f32_(f32_to_f16_(q[3 * i]));
            float s1 = f16_to_f32_(f32_to_f16_(q[3 * i + 1]));
            float s2 = f16_to_f32_(f32_to_f16_(q[3 * i + 2]));
            acc[3 * i]     += s0;
            acc[3 * i + 1] += s1;
            acc[3 * i + 2] += s2;
        }
    }
    float inv = 1.0f / (float)ns;
    for (size_t i = 0; i < npix * 3; i++) acc[i] *= inv;
}

int main(void) {
    /* ---- positive case: 64 jittered samples spread over 10 half-float ulps
     * at magnitude 1.0. The fp32 path keeps the analytic mean (the
     * perturbation sums to zero by construction); the fp16 path biases the
     * mean by ~1 half-ulp because each rounded sample carries a different
     * rounding error that doesn't cancel. */
    {
        const int ns = 64;
        float rad[64 * 3];
        for (int k = 0; k < ns; k++) {
            float d = (k - 31.5f) * (1.0e-2f / 31.5f);   /* symmetric, ±5e-3 */
            rad[3 * k]     = 1.0f + d;
            rad[3 * k + 1] = 1.0f + d * 0.5f;
            rad[3 * k + 2] = 1.0f + d * 0.25f;
        }
        float acc32[3] = {0}, acc16[3] = {0};
        run_fp32(acc32, rad, ns, 1);
        run_fp16_roundtrip(acc16, rad, ns, 1);
        /* positive gate: fp32 path recovers the analytic mean exactly */
        CHECK(fabsf(acc32[0] - 1.0f) <= 1.0e-7f);
        CHECK(fabsf(acc32[1] - 1.0f) <= 1.0e-7f);
        CHECK(fabsf(acc32[2] - 1.0f) <= 1.0e-7f);
        /* negative gate: fp16 path biases the mean by ~1 half-ulp at this
         * magnitude; the recovery error is well above fp32 noise. */
        CHECK(fabsf(acc16[0] - 1.0f) >= 1.0e-5f);
        CHECK(fabsf(acc16[0] - 1.0f) >  fabsf(acc32[0] - 1.0f));
    }
    /* ---- HDR gradient: 32 dark samples and 32 bright samples, mixed band.
     * The fp32 mean is exact; the fp16 mean loses ~1e-5. */
    {
        const int ns = 64;
        float rad[64 * 3];
        for (int k = 0; k < ns; k++) {
            float v;
            if (k < 32) v = 1.0f + (k - 15.5f) * 0.01f;     /* dark band, ±0.15 */
            else        v = 4.0f + (k - 47.5f) * 0.05f;     /* bright band, ±0.75 */
            rad[3 * k] = v; rad[3 * k + 1] = v * 0.5f; rad[3 * k + 2] = v * 0.25f;
        }
        float acc32[3] = {0}, acc16[3] = {0};
        run_fp32(acc32, rad, ns, 1);
        run_fp16_roundtrip(acc16, rad, ns, 1);
        float true_mean = 2.5f;
        CHECK(fabsf(acc32[0] - true_mean) <= 1.0e-6f);
        CHECK(fabsf(acc16[0] - true_mean) >= 1.0e-5f);
        CHECK(fabsf(acc16[0] - true_mean) >  fabsf(acc32[0] - true_mean));
    }
    /* ---- sanity: constant input is trivially exact in either path; the
     * point of the test is to confirm we are not chasing a phantom. */
    {
        const int ns = 64;
        float rad[64 * 3];
        for (int k = 0; k < ns; k++) { rad[3 * k] = 0.5f; rad[3 * k + 1] = 0.5f; rad[3 * k + 2] = 0.5f; }
        float acc32[3] = {0}, acc16[3] = {0};
        run_fp32(acc32, rad, ns, 1);
        run_fp16_roundtrip(acc16, rad, ns, 1);
        CHECK(fabsf(acc32[0] - 0.5f) < 1.0e-6f);
        CHECK(fabsf(acc16[0] - 0.5f) < 1.0e-6f);
    }
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    printf("blackhole_fp32_accum: all checks passed\n");
    return 0;
}