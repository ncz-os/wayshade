// SPDX-License-Identifier: Apache-2.0
// caustics.glsl - sunlight wandering over a dark, deep floor.
//
// What it is: two layers of analytic small-slope caustics plus a broad,
// slow field of underwater light, composed for depth. Not a single consistent
// optical system: each layer is its own honest model, and the composition is
// artistic.
//
//   - Caustic model (per layer): a water height field made of a few
//     directional waves; light arriving through it is displaced by D * grad h,
//     so irradiance on the floor is 1 / |det(I + D Hess h)|. The Hessian is
//     analytic, so each wave costs one sin. The denominator is floored (depth
//     blur), which bounds the focusing.
//   - Clustering: the focusing depth D itself varies over the floor with a
//     slow, low-frequency field, so sharp caustics gather into a few focal
//     regions while elsewhere the light relaxes into soft ripples, and the
//     regions drift and breathe over minutes (negative space that changes).
//   - Depth: the far layer (a deeper part of the floor) is smaller in scale,
//     dimmer, lower in contrast and slower; the near layer is larger, sharper
//     and faster. Differential motion between them reads as depth.
//   - Broad illumination: soft pools and slanted shafts of scattered light,
//     much slower than the caustics, lift the ground unevenly.
// There is no fisheye; an optional vignette is mild.
//
// Cost: 2 layers x 5 waves (one sin each) + 5 gradient-noise evaluations;
// quality 0 drops the far layer to 3 waves. Cost class L (a few analytic fields,
// about 2-6 noise evaluations per pixel) on every tier.
//
// Long runs: the director phases grow without bound and reach the shader as
// float32, so after about a day of continuous running the fine caustic
// positions lose their last bits (visible as a slight loss of crispness, not
// as a jump); the shaft direction and focal-region drift share the same limit.
// A saver that runs for days needs the phases wrapped on the CPU.
//
// Original work (2026-09-26, rebuilt 2026-09-29 by LEAD 27), MIT.

//@saver id=caustics version=2 family=dark+water director=85
//@palette-native bg=#04121a body=#bfe6e0 accent=#fff4d8 accent2=#2e6e7a
//@option name=focus type=float default=0.6 min=0.1 max=1 rand=0.45:0.8 group=Simulation
//@+ label="Focus" desc="Strength of focusing in the focal regions: sharper, brighter ribbons."
//@option name=clustering type=float default=0.6 min=0 max=1 rand=0.4:0.8 group=Simulation
//@+ label="Clustering" desc="How strongly the sharp light gathers into a few regions; 0 = evenly spread."
//@option name=depth type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Depth" desc="Water depth: deeper softens the caustics and separates the near and far layers more."
//@option name=shafts type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Scattered light" desc="Strength of the broad pools and slanted shafts of underwater light."
//@option name=wave-scale type=float default=1 min=0.5 max=2 rand=0.8:1.3 group=Simulation
//@+ label="Wave size" desc="Size of the surface waves (and so of the caustic cells)."
//@option name=sun type=float default=1 min=0.4 max=1.6 rand=0.85:1.15 group=Look
//@+ label="Sunlight" desc="Brightness of the caustic light."
//@option name=vignette type=float default=0.15 min=0 max=1 group=Look
//@+ label="Vignette" desc="Mild darkening toward the corners."

#include "xst_common.glsl"

// irradiance of one caustic layer at x (units: screen heights), n waves
float caustic_layer(vec2 x, float k0, float amp, float phase, float head, float D, float soft, float seed, const int n) {
    vec3 hs = vec3(0.0);
    for (int i = 0; i < 5; i++) {
        if (i >= n) break;
        float fi = float(i);
        float a = head + XST_TAU * fi / float(n) + 0.35 * (fract(seed * (1.3 + 0.71 * fi)) - 0.5);   // evenly spread headings: isotropic net
        vec2 d = vec2(cos(a), sin(a));
        float km = 0.8 + 0.55 * fract(seed * 5.1 + 0.37 * fi);
        float k = k0 * km;
        float s = sin(k * dot(d, x) - sqrt(km) * phase + XST_TAU * fract(seed * 2.3 * (fi + 1.0)));
        float ai = amp / km;
        hs += -ai * k * k * s * vec3(d.x * d.x, d.y * d.y, d.x * d.y);
    }
    float jxx = 1.0 + D * hs.x, jyy = 1.0 + D * hs.y, jxy = D * hs.z;
    float E = 1.0 / max(abs(jxx * jyy - jxy * jxy), soft);
    return E / (1.0 + 0.3 * E);                  // compressed: bright but keeps a gradient
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float ws = OPT_WAVE_SCALE;
    float head = XST_TAU * iSeed.x + 0.5 * (DIR_P1 - 0.5);

    // clustering: focal regions drift and breathe (slow noise, director level)
    float cl = xst_noise(uv * 0.55 + vec2(0.006, -0.004) * iTime + iSeed.yz * 40.0);
    cl = smoothstep(-0.2 + 0.3 * (DIR_P0 - 0.5), 0.4, cl);
    float focusField = mix(1.0, 0.05 + 1.6 * cl * cl, OPT_CLUSTERING);

    // a gentle low-frequency warp of the floor coordinate breaks every lattice
    vec2 w = 0.25 * vec2(xst_noise(uv * 0.7 + 0.01 * iTime), xst_noise(uv * 0.7 + 9.1 - 0.01 * iTime));

    // near layer: larger, sharper, faster
    float Dn = mix(0.7, 2.4, OPT_FOCUS) * focusField;
    float softn = mix(0.05, 0.3, OPT_DEPTH);
    float near = caustic_layer(uv + w, 13.0 / ws, 0.012, 0.55 * DIR_PH0, head, Dn, softn, iSeed.w, 5);

    // far layer: a deeper floor seen through the same water, so smaller,
    // softer, slower and dimmer
#if NCZ_QUALITY == 0
    const int FAR_N = 3;
#else
    const int FAR_N = 5;
#endif
    float Df = mix(0.5, 1.6, OPT_FOCUS) * mix(1.0, 0.4 + 0.8 * cl, OPT_CLUSTERING * 0.6);
    float softf = mix(0.15, 0.5, OPT_DEPTH);
    float far = caustic_layer(uv * 1.9 + w * 0.6 + 13.0, 13.0 / ws, 0.010, 0.28 * DIR_PH1, head + 0.9, Df, softf, fract(iSeed.w + 0.41), FAR_N);

    // broad scattered light: pools plus slanted shafts, very slow
    vec2 sd = vec2(cos(head * 0.3 + 1.1), sin(head * 0.3 + 1.1));
    float pools = 0.5 + 0.5 * xst_noise(uv * 0.35 + vec2(0.003, 0.002) * iTime + 5.0);
    float shafts = pow(0.5 + 0.5 * sin(dot(uv, vec2(-sd.y, sd.x)) * 2.2 + 1.6 * xst_noise(uv * 0.6 + 2.0) + 0.05 * DIR_PH2), 4.0);
    float scatter = OPT_SHAFTS * (0.5 * pools * pools + 0.5 * shafts * pools);

    // compose in linear light: near caustics sit on top, the far layer is a
    // dim, cool texture, the scattered light lifts the ground
    float depthMix = mix(0.55, 0.35, OPT_DEPTH);
    // scattered and far light take the water's own tone (body filtered by the
    // ground), so every palette stays one body of water
    vec3 waterTone = mix(PAL_BODY, PAL_BG, 0.55) * 0.8 + PAL_ACCENT2 * 0.08;
    vec3 col = PAL_BG * (0.65 + 0.7 * pools * pools);
    col += waterTone * scatter * 0.3;
    col += waterTone * max(far - 0.85, 0.0) * 0.5 * depthMix * OPT_SUN * pools * pools;
    vec3 light = mix(PAL_BODY, PAL_ACCENT, smoothstep(1.2, 2.6, near));
    // outside the focal regions the near light relaxes to nothing: negative space
    float gate = smoothstep(0.3, 1.0, focusField);          // focusField == 1 when clustering is 0
    col += light * max(near - 1.05, 0.0) * 0.85 * OPT_SUN * gate * clamp(focusField, 0.0, 1.5);

    col *= 1.0 - OPT_VIGNETTE * 0.3 * dot(uv * vec2(0.55, 0.9), uv * vec2(0.55, 0.9));
    fragColor = xst_output(col, fragCoord);
}
