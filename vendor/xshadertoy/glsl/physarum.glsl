// SPDX-License-Identifier: Apache-2.0
// physarum.glsl - calligraphic transport filaments (a procedural
// approximation of the LOOK of physarum networks, not an agent simulation).
//
// What this is, honestly: a single-pass, stateless picture. Real physarum
// needs agents depositing into a persistent trail field (multipass feedback);
// here nothing persists between frames. The forms come from a domain-warped
// gradient-noise field; the motion from a bounded analytic flow:
//
//   - stretching and folding: alternating shear maps (x += a sin(b y + phase),
//     y += a sin(b x + phase), then a smaller x shear again); a composition of
//     shears is area-preserving and
//     is the textbook mechanism of stretching and folding (a "standard map").
//     Their phases come from the director's integrated phases, their amplitude,
//     orientation and the filament density from its slow parameters, so the
//     network re-forms over minutes and never jumps;
//   - finer internal motion: each finer octave drifts a little faster along
//     the flow (a slight depth-dependent drift), so striations slide inside
//     the forms;
//   - filament density varies over minutes and across the frame (a broad,
//     slowly moving mask), so the network never dissolves everywhere at once.
//
// Cost (design estimate, measured numbers in docs/XSHADERTOY-PERF.md):
//   quality 0 (weak):  2 warp octaves x2 + 3 field octaves = 7 gradient-noise evaluations (L)
//   quality 1 (mid):   3 x2 + 4 = 10 (L-M)
//   quality 2 (strong): 3 x2 + a second warp level (2) + 5 = 13 (M)
//
// Original work (2026-09-26, reworked 2026-09-29 by LEAD 27), MIT.

//@saver id=physarum version=2 family=dark|ink|botanical director=80
//@palette-native bg=#0e1526 body=#e9dcc6 accent=#f4e6cc accent2=#8a7058
//@option name=density type=float default=0.5 min=0 max=1 rand=0.42:0.75 group=Simulation
//@+ label="Filament density" desc="How much of the frame the forms cover. The director still thickens and thins them regionally over minutes."
//@option name=fold type=float default=0.55 min=0 max=1 rand=0.35:0.8 group=Simulation
//@+ label="Stretch and fold" desc="Amplitude of the shear flow that stretches and folds the forms. 0 = only the slow warp drift."
//@option name=scale type=float default=1 min=0.5 max=2 rand=0.8:1.35 group=Simulation
//@+ label="Form scale" desc="Size of the forms; larger values give fewer, broader strokes."
//@option name=striation type=float default=0.65 min=0 max=1 rand=0.4:0.9 group=Look
//@+ label="Striation" desc="Strength of the fine internal lines inside the strokes."
//@option name=flow type=float default=1 min=0 max=2 rand=0.7:1.15 group=Motion
//@+ label="Flow rate" desc="Speed of the flow relative to the global speed. 0 freezes the flow; the director's slow parameters (density, fold, heading) still reshape the forms over minutes."
//@option name=depth-drift type=float default=0.3 min=0 max=1 rand=0.15:0.5 group=Motion
//@+ label="Depth drift" desc="How much faster the finer layers slide than the broad forms."
//@option name=vignette type=float default=0.25 min=0 max=1 group=Look
//@+ label="Vignette" desc="Darkening toward the corners."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define WARP_OCT 2
#define FIELD_OCT 3
#elif NCZ_QUALITY == 1
#define WARP_OCT 3
#define FIELD_OCT 4
#else
#define WARP_OCT 3
#define FIELD_OCT 5
#define SECOND_WARP 1
#endif

// fbm of gradient noise; each finer octave is offset along `drift` by an
// extra fraction (depth-dependent drift) and faded below the pixel footprint.
float fbm(vec2 p, vec2 drift, float fscale, const int oct) {
    float a = 0.5, v = 0.0, f = fscale;
    for (int i = 0; i < 5; i++) {
        if (i >= oct) break;
        v += a * xst_octave_fade(f) * xst_noise(p + drift * (float(i) * OPT_DEPTH_DRIFT));
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + vec2(3.1, 1.7);
        f *= 2.0;
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float t = iTime * OPT_FLOW;

    // --- bounded analytic flow: rotate into the slowly turning dominant
    // direction, then two shear passes (stretch and fold).
    float ang = XST_TAU * (iSeed.x + 0.35 * (DIR_P1 - 0.5));
    vec2 p = xst_rot(ang) * uv;
    float scale = (0.8 + 0.5 * iSeed.y) * OPT_SCALE;
    p *= scale;
    float amp = OPT_FOLD * (0.35 + 0.4 * DIR_P2);          // shear amplitude (form units)
    float k1 = 0.9 + 0.5 * iSeed.z, k2 = 0.8 + 0.6 * iSeed.w;
    p.x += amp * sin(k1 * p.y + 0.061 * DIR_PH0 * OPT_FLOW);
    p.y += amp * 0.8 * sin(k2 * p.x + 0.047 * DIR_PH1 * OPT_FLOW + 1.7);
    p.x += amp * 0.45 * sin(1.7 * k2 * p.y - 0.083 * DIR_PH2 * OPT_FLOW + 4.1);

    // --- domain warp, drifting along the flow
    vec2 drift = vec2(0.022, 0.009) * t;
    float fs = scale * 0.5;                    // noise cycles per uv unit (for AA)
    vec2 w = vec2(fbm(p + vec2(0.0, 0.012 * t), drift, fs, WARP_OCT),
                  fbm(p + vec2(5.2, 1.3) - vec2(0.010 * t, 0.0), drift, fs, WARP_OCT));
#ifdef SECOND_WARP
    w += 0.5 * vec2(xst_noise(1.7 * w + p + 11.7), xst_noise(1.7 * w + p + 3.9));
#endif
    float warpAmp = 1.5 + 0.7 * iSeed.w;
    float v = fbm(p * 0.9 + warpAmp * w + 7.3, drift * 1.3, fs * 0.9, FIELD_OCT);

    // --- density: global (option and director) and regional (broad mask)
    float region = xst_noise(uv * 0.55 + vec2(0.006, -0.004) * t + 19.1);
    float dens = clamp(OPT_DENSITY + 0.25 * (DIR_P0 - 0.5) + 0.22 * region, 0.0, 1.0);
    float thr = mix(0.22, 0.02, dens);
    float wid = mix(0.10, 0.20, dens);          // ribbon width in field units

    // --- forms: calligraphic ribbons along one iso-contour band of the
    // field (dark interiors), fine striation lines running inside the
    // ribbon, and sparse swollen nodes where the field peaks.
    float up = xst_aastep(thr, v);
    float aw = fwidth(v);
    float down = 1.0 - smoothstep(thr + wid * 0.55 - aw, thr + wid + aw, v);
    float ribbon = up * down;
    float x = clamp((v - thr) / wid, 0.0, 1.0);            // 0 outer edge .. 1 inner edge
    float stri_f = (v - thr) * (38.0 + 14.0 * iSeed.x);
    float stri = xst_aaband(fract(stri_f), 0.5, 0.16) * smoothstep(0.1, 0.35, x) * ribbon;
    float node = smoothstep(thr + wid + 0.16, thr + wid + 0.30, v);

    // --- shading in linear light: brush-like falloff across the ribbon and a
    // gentle relief from the screen-space field gradient.
    float relief = clamp(0.5 + 14.0 * dFdx(v) * iResolution.y / 1080.0, 0.0, 1.0);
    vec3 bg = PAL_BG * (1.0 + 0.45 * (region + 0.3) * (1.0 - length(uv) * 0.35));
    vec3 bodyc = PAL_BODY * mix(0.72, 1.0, relief) * mix(1.0, 0.82, x * x);
    vec3 col = mix(bg, bodyc, ribbon);
    col = mix(col, PAL_ACCENT2, stri * OPT_STRIATION * 0.6);
    col = mix(col, mix(PAL_BODY, PAL_ACCENT, 0.5) * mix(0.8, 1.0, relief), node * 0.9);

    // vignette (multiplicative, in linear light)
    col *= 1.0 - OPT_VIGNETTE * 0.45 * dot(uv * vec2(0.6, 0.9), uv * vec2(0.6, 0.9));

    fragColor = xst_output(col, fragCoord);
}
