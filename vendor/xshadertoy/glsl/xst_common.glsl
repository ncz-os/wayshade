// SPDX-License-Identifier: Apache-2.0
// xst_common.glsl - shared foundation of the xshadertoy_opts savers.
//
// Spliced in by the driver where a shader writes #include "xst_common.glsl"
// (after the //@ directives). The driver has already declared:
//   uniform vec3 uPal[4];   palette roles, LINEAR RGB: bg, body, accent, accent2
//   uniform vec4 uDir[4];   director: uDir[0..1] = 8 slow parameters in [0,1]
//                           (seeded minute-scale keyframes, quintic interpolation);
//                           uDir[2..3] = 8 integrated phases (rate 0.5..1.5 per
//                           second of motion time, frequency changes never jump)
//   #define NCZ_QUALITY 0|1|2 and OPT_<NAME> for every option.
//
// Precision: the driver's preamble (precision highp float/int) precedes this
// file; it is not meant to compile on its own.
//
// Conventions every saver follows:
//   - aspect-correct coordinates (xst_uv): 1 unit = half the screen height;
//   - shading in linear light; one highlight shoulder and ONE display transfer
//     at the very end (xst_output), plus a static zero-mean dither (no
//     animated grain, which becomes shimmer);
//   - contours drawn with fwidth coverage (xst_aastep, xst_aaband) outside
//     any non-uniform control flow;
//   - octaves finer than the pixel footprint are faded out (xst_octave_fade).
//
// Original work (LEAD 27, 2026-09-29), Apache-2.0.

#define XST_PI  3.14159265359
#define XST_TAU 6.28318530718

// ---- palette roles (linear RGB) ------------------------------------------
#define PAL_BG      uPal[0]
#define PAL_BODY    uPal[1]
#define PAL_ACCENT  uPal[2]
#define PAL_ACCENT2 uPal[3]

// ---- director -------------------------------------------------------------
#define DIR_P0 uDir[0].x
#define DIR_P1 uDir[0].y
#define DIR_P2 uDir[0].z
#define DIR_P3 uDir[0].w
#define DIR_P4 uDir[1].x
#define DIR_P5 uDir[1].y
#define DIR_P6 uDir[1].z
#define DIR_P7 uDir[1].w
#define DIR_PH0 uDir[2].x
#define DIR_PH1 uDir[2].y
#define DIR_PH2 uDir[2].z
#define DIR_PH3 uDir[2].w
#define DIR_PH4 uDir[3].x
#define DIR_PH5 uDir[3].y
#define DIR_PH6 uDir[3].z
#define DIR_PH7 uDir[3].w

// ---- coordinates ------------------------------------------------------------
// Centered, aspect-correct: y in [-1, 1], x in [-aspect, aspect].
vec2 xst_uv(vec2 fragCoord) {
    return (2.0 * fragCoord - iResolution.xy) / iResolution.y;
}
// Size of one pixel in xst_uv units.
float xst_px() { return 2.0 / iResolution.y; }
mat2 xst_rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); }

// ---- hashing and noise (integer hashes: no sin() precision loss) ----------
uint xst_hu(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float xst_hash21(vec2 p) {
    uvec2 q = uvec2(ivec2(floor(p + 32768.0)));
    return float(xst_hu(q.x * 1597334677u ^ xst_hu(q.y + 0x9E3779B9u)) >> 8) * (1.0 / 16777216.0);
}
vec2 xst_grad2(vec2 i) {
    uvec2 q = uvec2(ivec2(i + 32768.0));
    uint h = xst_hu(q.x * 1597334677u ^ xst_hu(q.y * 3812015801u + 1u));
    float a = float(h >> 8) * (XST_TAU / 16777216.0);
    return vec2(cos(a), sin(a));
}
// 2D gradient noise with its analytic derivatives: returns (value, d/dx, d/dy).
// Value range about [-0.7, 0.7].
vec3 xst_noised(vec2 p) {
    vec2 i = floor(p), f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    vec2 ga = xst_grad2(i), gb = xst_grad2(i + vec2(1, 0));
    vec2 gc = xst_grad2(i + vec2(0, 1)), gd = xst_grad2(i + vec2(1, 1));
    float va = dot(ga, f), vb = dot(gb, f - vec2(1, 0));
    float vc = dot(gc, f - vec2(0, 1)), vd = dot(gd, f - vec2(1, 1));
    float v = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
    vec2 d = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd)
           + du * (u.yx * (va - vb - vc + vd) + vec2(vb, vc) - va);
    return vec3(v, d);
}
float xst_noise(vec2 p) { return xst_noised(p).x; }

// Divergence-free flow from a scalar potential: curl = (dpsi/dy, -dpsi/dx),
// from the analytic derivatives (one noise evaluation, not four).
vec2 xst_curl(vec2 p) { vec3 n = xst_noised(p); return vec2(n.z, -n.y); }

// ---- anti-aliasing and frequency control ---------------------------------
// Coverage of x > edge with a one-pixel ramp.
float xst_aastep(float edge, float x) {
    float w = max(fwidth(x), 1e-5);
    return smoothstep(edge - w, edge + w, x);
}
// Coverage of |x - c| < hw, with the ramp never wider than the band.
float xst_aaband(float x, float c, float hw) {
    float w = max(fwidth(x), 1e-5);
    float d = abs(x - c);
    return 1.0 - smoothstep(hw - w, hw + w, d);
}
// Weight for an octave of spatial frequency `freq` (cycles per xst_uv unit):
// 1 while a cycle spans >= 4 pixels, 0 once it spans <= 2.
float xst_octave_fade(float freq) {
    float cyc_px = 1.0 / (freq * xst_px());
    return smoothstep(2.0, 4.0, cyc_px);
}

// ---- shading and output -------------------------------------------------------
float xst_luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// Gentle highlight shoulder: identity below `knee`, asymptotic to 1 above,
// applied to luminance so hue is kept.
vec3 xst_shoulder(vec3 c, float knee) {
    float l = xst_luma(c);
    if (l <= knee) return c;
    float s = 1.0 - knee;
    float l2 = knee + s * (1.0 - exp(-(l - knee) / s));
    return c * (l2 / max(l, 1e-5));
}
vec3 xst_srgb(vec3 lin) {
    lin = clamp(lin, 0.0, 1.0);
    return mix(lin * 12.92, 1.055 * pow(lin, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, lin));
}
// Static, zero-mean triangular dither of +-1 LSB in screen space.
float xst_dither(vec2 fc) {
    float a = xst_hash21(fc), b = xst_hash21(fc + 71.3);
    return (a + b - 1.0) / 255.0;
}
// The one display transfer: shoulder, sRGB, dither.
vec4 xst_output(vec3 lin, vec2 fragCoord) {
    vec3 c = xst_srgb(xst_shoulder(max(lin, 0.0), 0.8));
    return vec4(c + xst_dither(fragCoord), 1.0);
}

// ---- motion helpers -------------------------------------------------------------
// Quintic ease.
float xst_q(float x) { x = clamp(x, 0.0, 1.0); return x * x * x * (x * (x * 6.0 - 15.0) + 10.0); }
