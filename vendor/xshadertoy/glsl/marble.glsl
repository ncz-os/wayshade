// SPDX-License-Identifier: Apache-2.0
// marble.glsl - a polished slab of veined stone under slowly moving light.
//
// What it is: a procedural stone (not a geological model). The slab itself
// never deforms; what changes is how you see it:
//   - the stone: one body color with a soft cloudy variation, broad veins
//     (iso-lines of a sine carried through a domain-warped fbm, the classic
//     marble construction) and subordinate hairlines at a finer scale, plus a
//     SPARSE mineral intrusion (a second, rare field) in the fourth palette role;
//   - the view: a slow pan, zoom and turn over minutes (director phases and
//     parameters), so the frame travels across the slab;
//   - the light: a raking light whose azimuth sweeps slowly, shading the
//     shallow relief of the veins, and a broad polished reflection band that
//     glides across the slab.
// Light tier (quality 0) drops the finest octaves first: 3 cloud octaves and
// one hairline field; quality 2 uses 5 and the full warp.
//
// Cost: about 16 gradient-noise evaluations per pixel at quality 0 (warp 2x2,
// clouds 3, vein fbm 2, hairline 1+1, intrusion 3, grain 1, masks 2) and about
// 26 at quality 2: class M. Measured on Radeon 780M at 1080p: q0 p95 about
// 5.5 ms against 6.8 ms for the old shader.
//
// The old version drew its "per-launch" seed from iTime every frame (the same
// strobe as the old physarum) and blended three unrelated colors; both gone.
//
// Original work (2026-09-26, rebuilt 2026-09-29 by LEAD 27), MIT.

//@saver id=marble version=2 family=mineral director=100
//@palette-native bg=#2a2226 body=#dccfc8 accent=#7c6c78 accent2=#5f8a70
//@option name=vein-scale type=float default=1 min=0.5 max=2 rand=0.75:1.35 group=Stone
//@+ label="Vein scale" desc="Size of the vein pattern; larger values give broader, fewer veins."
//@option name=veins type=float default=0.55 min=0 max=1 rand=0.35:0.75 group=Stone
//@+ label="Vein strength" desc="Width and contrast of the broad veins."
//@option name=hairlines type=float default=0.5 min=0 max=1 rand=0.3:0.8 group=Stone
//@+ label="Hairlines" desc="Strength of the fine subordinate veins between the broad ones."
//@option name=intrusion type=float default=0.08 min=0 max=0.3 rand=0.03:0.14 group=Stone
//@+ label="Mineral intrusion" desc="Share of the slab taken by the rare second mineral (accent color). Keep it small: it should be found, not seen everywhere."
//@option name=polish type=float default=0.6 min=0 max=1 rand=0.4:0.85 group=Light
//@+ label="Polish" desc="Strength of the reflection band gliding across the slab."
//@option name=light-sweep type=float default=1 min=0 max=2 rand=0.7:1.3 group=Light
//@+ label="Light sweep" desc="Speed at which the raking light turns around the slab. 0 = fixed light."
//@option name=drift type=float default=1 min=0 max=2 rand=0.7:1.3 group=Motion
//@+ label="Drift" desc="Speed of the slow pan across the slab."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define CLOUD_OCT 3
#define WARP_OCT 2
#define HAIR_OCT 1
#else
#define CLOUD_OCT 5
#define WARP_OCT 3
#define HAIR_OCT 2
#define HAIR_TWO 1
#endif

float fbm(vec2 p, const int oct, float f0) {
    float v = 0.0, a = 0.5, f = f0;
    for (int i = 0; i < 5; i++) {
        if (i >= oct) break;
        v += a * xst_octave_fade(f) * xst_noise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + vec2(7.1, 3.3);
        f *= 2.0;
        a *= 0.5;
    }
    return v;
}

// Stone height (relief) and albedo at slab point p; fs = noise cycles per uv unit.
void stone(vec2 p, float fs, out float height, out vec3 albedo) {
    // domain warp shared by the veins
    vec2 w = vec2(fbm(p * 0.8, WARP_OCT, fs * 0.8), fbm(p * 0.8 + 5.2, WARP_OCT, fs * 0.8));
    vec2 q = p + 1.3 * w;

    // soft clouds in the body
    float cloud = fbm(p * 1.4 + 11.0, CLOUD_OCT, fs * 1.4);

    // broad veins: iso-lines of a sine carried through the warp
    float ang = XST_TAU * iSeed.x;
    vec2 d = vec2(cos(ang), sin(ang));
    float s = dot(q, d) * (2.8 / OPT_VEIN_SCALE) + 4.2 * fbm(q * 0.7 + 3.0, WARP_OCT, fs * 0.7);
    float sv = sin(s);
    float wv = (0.025 + 0.07 * OPT_VEINS) * (0.15 + 0.85 * smoothstep(-0.35, 0.45, xst_noise(q * 0.45 + 21.0)));
    float vein = xst_aaband(sv, 0.0, wv);
    float veinSoft = 1.0 - smoothstep(0.0, wv * 9.0 + 0.10, abs(sv));   // stained halo around a vein

    // subordinate hairlines: finer, thinner, crossing at another angle
    vec2 d2 = vec2(cos(ang + 1.1), sin(ang + 1.1));
    float s2 = dot(q, d2) * (5.0 / OPT_VEIN_SCALE) + 3.5 * fbm(q * 1.3 + 9.0, HAIR_OCT, fs * 1.3);
    float hair = xst_aaband(sin(s2), 0.0, 0.045) * smoothstep(-0.1, 0.3, xst_noise(q * 0.9 + 4.0));
#ifdef HAIR_TWO
    float s3 = dot(q, d.yx * vec2(1.0, -1.0)) * (9.0 / OPT_VEIN_SCALE) + 4.0 * fbm(q * 2.1 + 17.0, 2, fs * 2.1);
    hair = max(hair, 0.6 * xst_aaband(sin(s3), 0.0, 0.03) * smoothstep(0.1, 0.4, xst_noise(q * 1.3 + 8.0)));
#endif

    // sparse intrusion: a rare high region of a slow field, with a crystalline rim
    float m = fbm(p * 0.45 + 31.0, 2, fs * 0.45) + 0.25 * xst_noise(p * 3.1 + 2.0);
    float th = mix(0.42, 0.12, clamp(OPT_INTRUSION / 0.3, 0.0, 1.0));
    float intr = smoothstep(th - 0.01, th + 0.05, m);
    float rim = xst_aaband(m, th + 0.005, 0.008) * 0.6;
    // crystalline mottling inside the intrusion: small cells of lighter and darker grain
#if NCZ_QUALITY == 0
    float grain = xst_noise(p * 9.0 + 5.0);
#else
    float grain = xst_noise(p * 9.0 + 5.0) + 0.5 * xst_noise(p * 21.0 + 1.0);
#endif

    vec3 body = mix(PAL_BODY * 0.78, PAL_BODY * 1.04, clamp(0.5 + 1.3 * cloud, 0.0, 1.0));
    body = mix(body, mix(PAL_BODY, PAL_ACCENT, 0.4), veinSoft * 0.45 * OPT_VEINS);
    body = mix(body, PAL_ACCENT * 0.9, vein * (0.6 + 0.4 * OPT_VEINS));
    body = mix(body, mix(PAL_BODY, PAL_ACCENT, 0.8), hair * 0.85 * OPT_HAIRLINES);
    vec3 intrc = mix(PAL_ACCENT2 * 0.75, mix(PAL_ACCENT2, PAL_BODY, 0.35), smoothstep(-0.4, 0.5, grain));
    body = mix(body, intrc, intr * 0.9);
    body = mix(body, PAL_BG, rim * 0.35);
    albedo = body;
    // relief: veins sit slightly proud (harder mineral), intrusions stand out
    height = 0.8 * cloud + 0.12 * veinSoft + 0.25 * intr;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);

    // --- slow viewpoint: pan, zoom and turn
    float zoom = 1.0 + 0.35 * (DIR_P1 - 0.5);
    float turn = 0.35 * (DIR_P2 - 0.5) + XST_TAU * iSeed.y;
    vec2 pan = vec2(0.095 * DIR_PH0, 0.065 * DIR_PH1) * OPT_DRIFT + iSeed.zw * 50.0;
    vec2 p = xst_rot(turn) * uv * (0.9 / zoom) + pan;
    float fs = 0.9 / zoom;

    float hgt;
    vec3 alb;
    stone(p, fs, hgt, alb);

    // --- light: shallow relief from the height's screen derivatives
    vec2 g = vec2(dFdx(hgt), dFdy(hgt)) / xst_px();
    vec3 N = normalize(vec3(-g * 0.010, 1.0));
    float az = XST_TAU * iSeed.w + 0.07 * DIR_PH2 * OPT_LIGHT_SWEEP;
    float el = mix(0.45, 0.85, DIR_P3);
    vec3 L = normalize(vec3(cos(az) * cos(el), sin(az) * cos(el), sin(el)));
    float diff = max(dot(N, L), 0.0);
    vec3 col = alb * (0.28 + 0.85 * diff);

    // --- polish: a broad soft reflection band gliding across, plus a tight glint
    vec2 bd = vec2(cos(az + 1.4), sin(az + 1.4));
    float band = dot(uv, bd) - 1.4 * sin(0.06 * DIR_PH3 * OPT_LIGHT_SWEEP + 6.0 * iSeed.x);
    float sheen = exp(-band * band * 3.0) * (0.55 + 0.45 * N.z);
    vec3 H = normalize(L + vec3(0.0, 0.0, 1.0));
    float glint = pow(max(dot(N, H), 0.0), 90.0);
    // reflections scale with the stone's own lightness so dark stones are not hazed over
    float lift = 0.35 + 0.65 * clamp(xst_luma(PAL_BODY) * 2.0, 0.0, 1.0);
    col += OPT_POLISH * lift * (0.10 * sheen + 0.30 * glint) * mix(vec3(1.0), PAL_BODY, 0.5);

    col *= 1.0 - 0.18 * dot(uv * vec2(0.55, 0.9), uv * vec2(0.55, 0.9));
    fragColor = xst_output(col, fragCoord);
}
