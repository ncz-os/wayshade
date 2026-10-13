// SPDX-License-Identifier: Apache-2.0
// caustics-on-stone.glsl - sunlight focused by moving water onto still pale stone.
//
// What it is: an analytic small-slope caustic model, then artistic compression.
//   - The water surface is a height field h(x, t): two wave families, each a sum
//     of three directional sines. The swell is long and slow; the ripples are short
//     and faster. Speeds follow the deep-water dispersion ratio (omega ~ sqrt(k)), their
//     phases come from the director (integrated, so speed changes never jump), and
//     each family's heading turns slowly over minutes (directional interference).
//   - Light refracted through the surface lands displaced by D * grad h, where D
//     is the effective depth. The irradiance on the stone is 1 / |det(I + D Hess h)|:
//     where the map folds (det -> 0) light concentrates into the bright ribbons.
//     The Hessian is analytic, so this costs a few sin/cos per wave. The
//     denominator is floored (a softness that grows with depth), which bounds the
//     focusing: an artistic approximation, not a full caustic integral.
//   - A slow, low-frequency warp of the sample point keeps the six-wave sum from
//     repeating in any regular lattice (no grid-like boundaries).
//   - The stone is STATIONARY and lit multiplicatively (albedo x light), so its
//     texture stays readable inside the brightest light; highlights are
//     compressed before the shared shoulder, and the ground between ribbons shows
//     warm stone rather than white wash.
//
// Cost: 6 waves (sin+cos) + 3 gradient-noise evaluations (warp, regional focus) + 3-5 for
// the stone (static, faded below the pixel footprint): L on every tier.
//
// Original work (wave2-materia 2026-09-26, rebuilt 2026-09-29 by LEAD 27), MIT.

//@saver id=caustics-on-stone version=2 family=light+stone director=70
//@palette-native bg=#b9a584 body=#e2d3b4 accent=#fff1d2 accent2=#8a7458
//@option name=focus type=float default=0.55 min=0.1 max=1 rand=0.4:0.75 group=Simulation
//@+ label="Focus" desc="How strongly the waves focus the light (wave steepness times depth). Higher gives sharper, brighter ribbons."
//@option name=depth type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Water depth" desc="Deeper water softens the ribbons and widens them."
//@option name=ripple-scale type=float default=1.15 min=0.5 max=2 rand=0.95:1.45 group=Simulation
//@+ label="Ripple size" desc="Size of the short wind ripples relative to the swell."
//@option name=ripples type=float default=0.6 min=0 max=1 rand=0.4:0.8 group=Simulation
//@+ label="Ripple strength" desc="Share of the short ripples against the long swell. 0 = swell only (broad, slow pools of light)."
//@option name=sun type=float default=1 min=0.4 max=1.6 rand=0.85:1.15 group=Look
//@+ label="Sunlight" desc="Brightness of the direct light."
//@option name=stone-texture type=float default=0.6 min=0 max=1 rand=0.4:0.8 group=Look
//@+ label="Stone texture" desc="Strength of the stone's grain and veins."
//@option name=vignette type=float default=0.2 min=0 max=1 group=Look
//@+ label="Vignette" desc="Darkening toward the corners."

#include "xst_common.glsl"

// Three waves of one family: returns the height Hessian (xx, yy, xy) scaled by
// amplitude, and accumulates the gradient.
void family(vec2 x, float heading, float k0, float amp, float phase, float seed,
            inout vec3 hess) {
    // three headings spread around the family heading, with incommensurate
    // wavenumbers (ratios 1 : 1.37 : 0.81) so the sum never tiles
    const vec3 spread = vec3(-1.05, 0.12, 1.21);
    const vec3 kmul = vec3(1.0, 1.37, 0.81);
    for (int i = 0; i < 3; i++) {
        float a = heading + spread[i] + 0.35 * (seed - 0.5);
        vec2 d = vec2(cos(a), sin(a));
        float k = k0 * kmul[i];
        float w = sqrt(kmul[i]);                        // deep water: omega ~ sqrt(k)
        float s = sin(k * dot(d, x) - w * phase + 6.2831 * fract(seed * (3.1 + float(i))));
        float ai = amp / kmul[i];                       // shorter waves are lower
        // Hess(h) of a sin(k d.x - wt) = -a k^2 d d^T sin(...)
        hess += -ai * k * k * s * vec3(d.x * d.x, d.y * d.y, d.x * d.y);
    }
}

float stone_fbm(vec2 p) {
    float v = 0.0, a = 0.5, f = 1.0;
#if NCZ_QUALITY == 0
    const int OCT = 3;
#else
    const int OCT = 5;
#endif
    for (int i = 0; i < OCT; i++) {
        v += a * xst_octave_fade(f * 2.2) * xst_noise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p + 17.3;
        f *= 2.0;
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);

    // ---- stationary stone ----------------------------------------------------
    vec2 sp = uv * 1.3 + iSeed.xy * 40.0;
    float tex = stone_fbm(sp * 2.2);
    float broad = xst_noise(sp * 0.45 + 3.0);
    // thin, sparse veins: an iso-line of a warped field
    float vf = xst_noise(sp * 0.9 + 1.3 * vec2(xst_noise(sp * 0.7), xst_noise(sp * 0.7 + 9.0)));
    float vein = xst_aaband(vf, 0.08 * (iSeed.z - 0.5), 0.010) * smoothstep(-0.2, 0.3, broad);
    vec3 albedo = mix(PAL_BG, PAL_BODY, clamp(0.55 + 0.9 * broad + 0.9 * tex * OPT_STONE_TEXTURE, 0.0, 1.0));
    albedo = mix(albedo, PAL_ACCENT2, vein * 0.55 * OPT_STONE_TEXTURE);

    // ---- water surface and caustic irradiance -------------------------------
    // slow low-frequency warp: breaks every lattice, moves at a walking pace
    vec2 wp = uv + 0.30 * vec2(xst_noise(uv * 0.6 + 0.013 * iTime + 5.0),
                               xst_noise(uv * 0.6 - 0.011 * iTime + 1.0));
    float headA = XST_TAU * iSeed.w + 0.45 * (DIR_P1 - 0.5);
    float headB = headA + 1.9 + 0.9 * (DIR_P2 - 0.5);
    float kA = 4.2, kB = 17.0 / OPT_RIPPLE_SCALE;
    vec3 hess = vec3(0.0);
    family(wp, headA, kA, 0.034 * (1.0 - 0.6 * OPT_RIPPLES), 0.55 * DIR_PH0, iSeed.x, hess);
    family(wp, headB, kB, 0.0085 * OPT_RIPPLES * (0.75 + 0.5 * DIR_P3), 0.9 * DIR_PH1 * sqrt(kB / kA), iSeed.y, hess);
    // regional focus: some areas sharpen while others relax (a slow breathing of
    // the net at the scale of a third of the screen)
    float regional = 0.6 + 0.65 * smoothstep(-0.35, 0.35, xst_noise(uv * 0.42 + vec2(0.004, 0.003) * iTime + 31.0));
    float D = mix(0.8, 2.6, OPT_FOCUS) * (0.85 + 0.3 * DIR_P0) * regional;
    float jxx = 1.0 + D * hess.x, jyy = 1.0 + D * hess.y, jxy = D * hess.z;
    float det = jxx * jyy - jxy * jxy;
    float soft = mix(0.05, 0.30, OPT_DEPTH);                 // floor of |det|: depth blur
    float E = 1.0 / max(abs(det), soft);                     // relative irradiance, mean ~1
    // compress: the ribbons stay bright but keep a highlight gradient, no flat white
    float Ec = E / (1.0 + 0.30 * E);
    // broad illumination under the ribbons (swell-scale pools)
    // broad pools: the swell's own long-wave focusing, at a third of the screen
    // and larger, so bright and dim regions carry the net
    float pools = 0.62 + 0.26 * sin(dot(wp, vec2(cos(headA), sin(headA))) * 1.7 - 0.22 * DIR_PH0)
                       + 0.18 * xst_noise(wp * 0.5 + vec2(-0.009, 0.006) * iTime + 71.0);

    vec3 sunc = mix(vec3(1.0), PAL_ACCENT, 0.4) * OPT_SUN;
    vec3 ambient = mix(PAL_BODY, vec3(0.62, 0.66, 0.70), 0.3) * 0.36;  // cool skylight through water
    vec3 light = ambient + sunc * pools * (0.30 + 0.55 * Ec);
    vec3 col = albedo * light;

    col *= 1.0 - OPT_VIGNETTE * 0.35 * dot(uv * vec2(0.55, 0.9), uv * vec2(0.55, 0.9));
    fragColor = xst_output(col, fragCoord);
}
