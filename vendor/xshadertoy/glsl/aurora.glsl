// SPDX-License-Identifier: Apache-2.0
// aurora.glsl - curtains of aurora: one design for the old aurora and
// aurora-restrained (merged 2026-09-29).
//
// What it is: a procedural picture of auroral curtains, not a plasma model.
//   - Each curtain is a sheet hanging above a curved lower edge, drawn as a
//     continuous signed distance from that edge (the edge curve is a sum of
//     three sines whose derivative is analytic, so the distance is normalized
//     and the lower edge stays sharp without segments).
//   - Before the distance is taken, the curtain's own domain is deformed by a
//     divergence-free flow: the curl of a noise potential, from its analytic
//     derivatives (one noise evaluation, not four finite differences). This is
//     what makes a curtain fold and drift like a real one (Bridson et al.,
//     "Curl-noise for procedural fluid flow", 2007: flow from a potential; this is
//     domain deformation, not physical advection).
//   - Emission rises sharply at the lower edge and decays upward with a scale
//     height; fine vertical rays (field-aligned striations) slide along each
//     curtain; the nitrogen fringe tints the lower edge and oxygen red the
//     upper glow.
//   - Two or three sheets at different depths overlap obliquely: farther
//     sheets are higher, smaller, dimmer and slower. Emission adds, as light
//     from a transparent emitter does.
// Why the old ones failed: aurora.glsl drew its per-run "seed" from
// launchRand(iTime) (a new palette every frame) and marched 3D value noise
// (7 fps on Mali); aurora-restrained's segmented edges came from the hash
// fract(p * vec2(123.34, 456.21)) fed with t * 0.3, whose float precision
// collapses into straight-edged cells as t grows.
//
// Cost: per curtain 1 curl noise + 1-2 ray noises + a few sines; 2 curtains
// at quality 0 (about 5 noise evaluations, L), 3 above (about 8-9, M-).
//
// Original work (LEAD 27, 2026-09-29), MIT.

//@saver id=aurora version=2 family=aurora|night+cool director=75
//@palette-native bg=#03060c body=#6ef0a8 accent=#e0607a accent2=#9a7ae0
//@option name=curtains type=int default=3 min=1 max=3 rand=2:3 group=Simulation
//@+ label="Curtains" desc="Number of curtain sheets (quality 0 draws at most two)."
//@option name=activity type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Activity" desc="Strength of the folding flow and how fast the curtains ripple. Low is a quiet arc; high is a substorm."
//@option name=folds type=float default=0.5 min=0 max=1 rand=0.3:0.75 group=Simulation
//@+ label="Folds" desc="Amplitude of the large bends in each curtain's lower edge."
//@option name=height type=float default=0.5 min=0 max=1 rand=0.35:0.7 group=Look
//@+ label="Curtain height" desc="How far the light reaches up from the lower edge."
//@option name=rays type=float default=0.6 min=0 max=1 rand=0.4:0.85 group=Look
//@+ label="Rays" desc="Strength of the fine vertical striations."
//@option name=brightness type=float default=1 min=0.4 max=1.6 rand=0.85:1.15 group=Look
//@+ label="Brightness" desc="Overall emission."
//@option name=horizon type=bool default=true rand=0.7 group=Look
//@+ label="Horizon" desc="A dark line of hills along the bottom of the frame."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define MAX_CURTAINS 2
#else
#define MAX_CURTAINS 3
#endif

// lower edge of curtain k at x: height and slope (analytic)
vec2 edge(float x, float k, float s, float y0, float slope) {
    float h = y0 + slope * x, d = slope;
    float fa = OPT_FOLDS;
    // three bends, phases from the director (never jump), frequencies per curtain
    vec3 w = vec3(0.9, 1.7, 3.1) / s;
    vec3 a = vec3(0.16, 0.07, 0.025) * fa * s;
    vec3 ph = vec3(0.045 * DIR_PH0, -0.07 * DIR_PH1, 0.11 * DIR_PH2) * (0.5 + OPT_ACTIVITY)
            + 6.2831 * fract(vec3(0.13, 0.57, 0.91) * (k + 1.0) + iSeed.xyz);
    for (int i = 0; i < 3; i++) {
        h += a[i] * sin(w[i] * x + ph[i]);
        d += a[i] * w[i] * cos(w[i] * x + ph[i]);
    }
    return vec2(h, d);
}

// emission of curtain k (0 near .. 2 far) at p; rgb in linear light
vec3 curtain(vec2 p, float k) {
    float z = k / 2.0;                              // depth 0..1
    float s = mix(1.0, 0.55, z);                    // apparent scale
    float y0 = mix(-0.42, 0.12, z) + 0.12 * (fract(iSeed.w * (3.0 + k)) - 0.5) + 0.08 * (DIR_P0 - 0.5);
    float slope = 0.18 * (fract(iSeed.z * (5.0 + 2.0 * k)) - 0.5) + 0.1 * (DIR_P1 - 0.5);
    float drift = iTime * mix(0.032, 0.012, z) * (0.4 + OPT_ACTIVITY);

    // curl-flow deformation of the curtain's domain (analytic derivatives)
    vec2 fp = vec2(p.x / s * 0.8 + drift + 7.0 * k, p.y / s * 1.3 - 0.3 * drift);
    vec2 c = xst_curl(fp);
    vec2 q = p + (0.05 + 0.13 * OPT_ACTIVITY) * s * c;

    vec2 e = edge(q.x, k, s, y0, slope);
    float d = (q.y - e.x) / sqrt(1.0 + e.y * e.y);  // signed distance above the lower edge

    // vertical profile: sharp lower edge (one pixel plus a little softness),
    // exponential decay upward with a scale height
    float H = mix(0.05, 0.20, OPT_HEIGHT) * s;
    float soft = 0.004 * s + fwidth(d);
    float lower = smoothstep(-soft, soft, d);
    // a bright band just above the edge plus a faint, much taller upper glow
    // that ends (smoothstep cap), so dark sky shows above every curtain
    float dp = max(d, 0.0);
    float prof = lower * (exp(-dp / H) + 0.22 * exp(-dp / (3.0 * H)) * (1.0 - smoothstep(4.0 * H, 7.0 * H, dp)))
               + 0.25 * exp(-abs(min(d, 0.0)) / (0.012 * s)) * (1.0 - lower);

    // rays: field-aligned striations, sliding along the curtain
    float rx = q.x / s;
    float slide = 0.06 * DIR_PH3 * (0.4 + OPT_ACTIVITY) + 3.7 * k;
    float r = xst_noise(vec2(rx * 38.0 + slide * 9.0, d * 2.2 / s + 0.1 * iTime)) * xst_octave_fade(38.0 / s);
#if NCZ_QUALITY > 0
    r += 0.5 * xst_noise(vec2(rx * 83.0 - slide * 13.0, d * 3.0 / s + 1.7 * k)) * xst_octave_fade(83.0 / s);
#endif
    float rays = 1.0 + OPT_RAYS * 1.4 * r;
    // brightness varies slowly along the curtain (no synchronized pulses)
    // each sheet has ends: brightness along it follows a slow noise that
    // reaches zero, so curtains begin and end and dark gaps open between them
    float along = smoothstep(-0.25, 0.3, xst_noise(vec2(rx * 0.55 - 0.6 * drift, 11.0 * k + DIR_P2)) - 0.02);

    // color by height above the edge: nitrogen fringe low, oxygen green body,
    // red upper glow
    vec3 col = mix(PAL_ACCENT2, PAL_BODY, smoothstep(0.0, 0.035 * s, d));
    col = mix(col, PAL_ACCENT, smoothstep(0.8 * H, 3.5 * H, d) * 0.85);
    float bright = mix(1.0, 0.45, z) * (0.75 + 0.5 * fract(iSeed.y * (7.0 + k)));
    return col * prof * max(rays, 0.0) * along * bright;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);

    // sky: the palette ground, a little lighter toward the horizon
    vec3 col = PAL_BG * mix(1.8, 0.7, smoothstep(-1.0, 0.8, uv.y));

    // stars: static anti-aliased points, dimmer near the horizon
    vec2 sg = uv * 90.0;
    vec2 cell = floor(sg);
    float h = xst_hash21(cell + 91.0);
    vec2 sp = cell + 0.2 + 0.6 * vec2(xst_hash21(cell + 3.0), xst_hash21(cell + 17.0));
    float sd = length(sg - sp);
    float star = step(0.975, h) * smoothstep(0.9, 0.2, sd / max(90.0 * xst_px(), 0.5)) * (h - 0.975) * 30.0;
    col += vec3(0.8, 0.85, 1.0) * star * 0.35 * smoothstep(-0.7, 0.2, uv.y);

    // curtains, far to near
    vec3 light = vec3(0.0);
    for (int i = MAX_CURTAINS - 1; i >= 0; i--) {
        if (i >= OPT_CURTAINS) continue;
        light += curtain(uv, float(i));
    }
    col += light * 0.9 * OPT_BRIGHTNESS;

#if OPT_HORIZON
    // hills: a dark silhouette, with the faint reflected glow of the sky above it
    float hill = -0.86 + 0.05 * xst_noise(vec2(uv.x * 1.3 + 40.0 * iSeed.x, 0.5)) + 0.02 * xst_noise(vec2(uv.x * 5.0, 3.0));
    float ground = xst_aastep(uv.y, hill);
    col = mix(col, PAL_BG * 0.35 + light * 0.03, ground);
#endif

    fragColor = xst_output(col, fragCoord);
}
