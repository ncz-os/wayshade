// SPDX-License-Identifier: Apache-2.0
// thin-film.glsl - a draining soap film: interference colors computed, not painted.
//
// What it is: analytic thin-film optics on a procedural film.
//   - Optics (physical): a film of index n (soap water, 1.33) in air, lit from the
//     viewer's side. Reflection at the first interface flips the phase by pi, at the
//     second it does not. With r = (n - 1) / (n + 1) and phase difference
//     delta = 4 pi n d cos(theta_t) / lambda, the Airy two-interface reflectance is
//         R(lambda) = 2 r^2 (1 - cos delta) / (1 + r^4 - 2 r^2 cos delta),
//     so the thinnest film (d -> 0) is BLACK (the classic black film at the top),
//     colors run through the orders as d grows, and very thick film averages to
//     pale silver. R is evaluated at 8 wavelengths (3 on the weak tier) and
//     integrated against the CIE 1931 observer (Wyman, Sloan and Shirley's 2013
//     analytic fit) into linear sRGB. theta_t follows Snell's law from the viewing
//     angle over a gently curved membrane.
//   - Film (procedural): a vertical film drains under gravity, so thickness grows
//     downward from a few tens of nanometres at the top; the profile is swirled by
//     a divergence-free flow (curl of a noise potential, analytic derivatives), with
//     finer eddies near the top where the film is thinnest. Over minutes the
//     director thins the whole film (drainage) and thickens it again (renewal),
//     never abruptly. The swirl is an artistic stand-in for Marangoni flow, not a
//     fluid simulation.
//   - Behind the film: the dark palette ground; the film reflects a large soft
//     light, whose image gives the quiet, bright regions.
//
// Cost: two curl evaluations and 8 (or 3) cosines per pixel: L.
//
// Original work (wave2-materia 2026-09-26, rebuilt 2026-09-29 by LEAD 27), MIT.

//@saver id=thin-film version=2 family=dark+ink|dark+night director=90
//@palette-native bg=#05060a body=#f2eee6 accent=#fff8ea accent2=#3a4458
//@option name=thickness type=float default=0.9 min=0.3 max=2 rand=0.6:1.3 group=Simulation
//@+ label="Film thickness (um)" desc="Thickness at the bottom of the film in micrometres; the top is always a few tens of nanometres. Thicker film shows more, paler color orders."
//@option name=drainage type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Drainage" desc="How strongly the film thins and renews over minutes, moving the color bands down and up."
//@option name=swirl type=float default=0.5 min=0 max=1 rand=0.3:0.75 group=Simulation
//@+ label="Swirl" desc="Strength of the eddies that stir the bands."
//@option name=curvature type=float default=0.4 min=0 max=1 rand=0.2:0.6 group=Simulation
//@+ label="Membrane curvature" desc="How much the film bulges; the viewing angle shifts the colors across the bulge."
//@option name=light type=float default=1 min=0.4 max=1.6 rand=0.85:1.15 group=Look
//@+ label="Light" desc="Brightness of the reflected light."
//@option name=index type=float default=1.33 min=1.2 max=1.6 group=Simulation
//@+ label="Refractive index" desc="Index of the film: 1.33 is soap water, 1.5 a thin oil or polymer film."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define NL 3
const float LAMBDA[3] = float[3](450.0, 550.0, 650.0);
const vec3 WEIGHT[3] = vec3[3](vec3(-0.0330, 0.0049, 1.1108), vec3(0.2267, 1.0401, -0.0950),
                               vec3(0.8063, -0.0450, -0.0158));
#else
#define NL 8
const float LAMBDA[8] = float[8](418.75, 456.25, 493.75, 531.25, 568.75, 606.25, 643.75, 681.25);
// CIE 1931 (Wyman et al. 2013 fit) integrated over each 37.5 nm bin, to linear
// sRGB, normalized so a flat spectrum is white.
const vec3 WEIGHT[8] = vec3[8](vec3(0.0329, -0.0380, 0.2997), vec3(0.0154, -0.0425, 0.6626),
                               vec3(-0.1527, 0.1851, 0.1640), vec3(-0.2034, 0.5157, -0.0420),
                               vec3(0.2810, 0.3833, -0.0571), vec3(0.6790, 0.0321, -0.0225),
                               vec3(0.3141, -0.0343, -0.0040), vec3(0.0337, -0.0014, -0.0007));
#endif

// Airy reflectance of a free film (phase flip at the first interface only)
vec3 film_reflect(float d_nm, float n, float cos_t) {
    float r = (n - 1.0) / (n + 1.0), r2 = r * r;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < NL; i++) {
        float delta = 12.5663706 * n * d_nm * cos_t / LAMBDA[i];
        float c = cos(delta);
        float R = 2.0 * r2 * (1.0 - c) / (1.0 + r2 * r2 - 2.0 * r2 * c);
        acc += R * WEIGHT[i];
    }
    return max(acc, 0.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float t = iTime;

    // membrane: a gentle bulge whose normal sets the viewing angle
    float bulge = OPT_CURVATURE * 0.55;
    vec2 bc = vec2(0.25 * (iSeed.x - 0.5), 0.15 * (iSeed.y - 0.5));
    vec2 slope = -bulge * (uv - bc) * 0.9;              // gradient of -bulge/2 |uv - bc|^2
    vec3 N = normalize(vec3(-slope, 1.0));
    float n = OPT_INDEX;
    float cos_i = N.z;
    float cos_t = sqrt(max(1.0 - (1.0 - cos_i * cos_i) / (n * n), 0.0));

    // swirl: curl flow deforms the film coordinates; finer eddies near the top
    vec2 sw = xst_curl(uv * 1.1 + vec2(0.045, -0.028) * t + iSeed.zw * 30.0);
    vec2 sw2 = xst_curl(uv * 2.8 + vec2(-0.08, 0.04) * t + 9.0);
    float topness = smoothstep(-0.3, 0.9, uv.y);
    vec2 q = uv + OPT_SWIRL * (0.24 * sw + 0.12 * (0.6 + 0.6 * topness) * sw2);

    // drainage profile: s = 0 at the top edge, 1 at the bottom
    float s = clamp((1.05 - q.y) / 2.1, 0.0, 1.0);
    float drain = 1.0 + OPT_DRAINAGE * 0.55 * (DIR_P0 - 0.5) * 2.0;   // renewal and thinning
    float dmax = OPT_THICKNESS * 1000.0 * drain;
    float dmin = 18.0;
    // the top of the film is black film (a few tens of nm) over a region that
    // grows as the film drains: the large quiet area
    float black = 0.18 + 0.2 * OPT_DRAINAGE * DIR_P0;
    float sb = smoothstep(black, 1.0, s);
    float d_nm = dmin + (dmax - dmin) * pow(sb, 1.25 + 0.4 * DIR_P1);

    // reflected light: a large soft window, slowly drifting over the bulge
    vec2 lc = vec2(-0.35 + 0.5 * (DIR_P2 - 0.5), 0.35 + 0.3 * (DIR_P3 - 0.5)) + 0.35 * slope;
    float window = 0.35 + 0.65 * exp(-dot(uv - lc, uv - lc) * 1.1);
    vec3 lightc = mix(vec3(1.0), PAL_ACCENT, 0.5) * OPT_LIGHT * 4.2;

    vec3 R = film_reflect(d_nm, n, cos_t);
    vec3 col = PAL_BG * (1.0 - xst_luma(R)) + R * lightc * window;
    // the frame's faint shadow at the very top and a dim rim of the ground
    col *= mix(0.55, 1.0, smoothstep(1.02, 0.9, uv.y));

    fragColor = xst_output(col, fragCoord);
}
