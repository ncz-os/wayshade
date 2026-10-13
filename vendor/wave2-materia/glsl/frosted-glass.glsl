// SPDX-License-Identifier: Apache-2.0
// frosted-glass.glsl - fluted, reeded or hammered glass over drifting light.
//
// What it is: an analytic model of light received through textured glass, not a
// ray tracer.
//   - Behind the glass sit a few broad Gaussian light sources (palette roles)
//     on smooth Lissajous paths from the director phases; they never jump and
//     never repeat over minutes.
//   - The glass surface has a height profile with an analytic slope: fluted
//     (cylindrical lenses side by side), reeded (fine flutes) or hammered (a
//     hex-ish dimple field). The slope bends the line of sight by the small-
//     angle refraction of a thin sheet, displacing where the lights are sampled
//     by  slope * gap  (gap = distance of the lights behind the glass).
//   - Frost is a convolution of the scene with a kernel. Because every source is
//     a Gaussian, convolving with a Gaussian kernel only widens the source: the
//     frost is exact and costs nothing (no jittered taps, no noise).
//   - Dispersion: red, green and blue use slightly different gaps.
//   - A raking light adds a highlight along each flute edge from the slope, and
//     a static fine grain (frost roughness) breaks the highlights up.
// Why the old one changed: it summed jittered samples of value noise built on
// fract(p * vec2(123.34, 456.21)) hashes (the precision-collapse pattern that
// produced straight-edged cells in caustics), and its lights wandered by noise
// with time in the argument.
//
// Cost: (sources x 3 channels) Gaussians and one profile: about 12 exp; no
// noise at all except the grain hash.
//
// Original work (LEAD 27, 2026-09-30), MIT.

//@saver id=frosted-glass version=2 family=light|mineral director=100
//@palette-native bg=#3a3f52 body=#f0b48a accent=#fff4e0 accent2=#7d8ec4
//@option name=style type=enum default=fluted choices=fluted,reeded,hammered rand=fluted|reeded|hammered group=Glass
//@+ label="Glass" desc="Fluted (broad cylindrical ribs), reeded (fine ribs) or hammered (dimpled)."
//@option name=rib type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Glass
//@+ label="Rib size" desc="Width of a flute or dimple."
//@option name=frost type=float default=0.5 min=0 max=1 rand=0.25:0.75 group=Glass
//@+ label="Frost" desc="How much the glass scatters: low keeps the lights nearly sharp, high melts them."
//@option name=refraction type=float default=0.5 min=0 max=1 rand=0.35:0.75 group=Glass
//@+ label="Refraction" desc="How far the ribs displace the light behind them."
//@option name=sources type=int default=4 min=2 max=4 rand=3:4 group=Light
//@+ label="Lights" desc="Number of light sources behind the glass."
//@option name=motion type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Light
//@+ label="Light drift" desc="How far the lights wander."
//@option name=dispersion type=float default=0.4 min=0 max=1 rand=0.2:0.6 group=Look
//@+ label="Dispersion" desc="Color fringes at the rib edges."
//@option name=brightness type=float default=1 min=0.5 max=1.5 rand=0.9:1.2 group=Look
//@+ label="Brightness" desc="Overall level of the light."

#include "xst_common.glsl"

#define NSRC 4

// light k at time: position in view units, Lissajous with director phases
vec2 src_pos(int k) {
    float fk = float(k);
    vec2 base = vec2(-0.55 + 0.37 * fk, 0.28 * sin(1.7 * fk + 6.28 * iSeed.x));
    base.x += 0.10 * (fract(iSeed.y * (3.0 + fk)) - 0.5);
    float m = 0.15 + 0.6 * OPT_MOTION;
    vec2 ph = vec2(DIR_PH0, DIR_PH1) * (0.012 + 0.004 * fk) + 6.28 * fract(vec2(iSeed.z, iSeed.w) * (2.0 + fk));
    return base + m * vec2(0.30 * sin(ph.x + 0.9 * fk), 0.22 * sin(1.3 * ph.y + 2.0 * fk));
}

// Gaussian light of the given rms radius; color per role
vec3 src_color(int k) {
    if (k == 0) return PAL_BODY;
    if (k == 1) return PAL_ACCENT2;
    if (k == 2) return PAL_ACCENT;
    return mix(PAL_BODY, PAL_ACCENT2, 0.5);
}

// glass slope (d height / dx, d height / dy) and a rib coordinate for the highlight
vec2 glass(vec2 p, out float edge) {
    float rib = mix(0.05, 0.22, OPT_RIB);
#if OPT_STYLE == 1
    rib *= 0.38;
#endif
    edge = 0.0;
#if OPT_STYLE == 2
    // hammered: slopes of a smooth, irregular dimple field: four cosines at
    // different angles and frequencies, over a slowly bent domain
    vec2 pw = p + 0.05 * xst_curl(p * 2.3 + 9.0 * iSeed.xy);
    float f = 6.2832 / (rib * 1.5);
    vec2 s = vec2(0.0);
    for (int i = 0; i < 4; i++) {
        float a = 1.571 * float(i) + 0.35 + 0.6 * float(i) * iSeed.z + 0.3 * iSeed.w;
        vec2 dir = vec2(cos(a), sin(a));
        float ph = f * (1.0 + 0.23 * float(i)) * dot(pw, dir) + 6.28 * fract(iSeed.w * (1.0 + float(i)));
        s += dir * (-sin(ph)) / (1.0 + 0.23 * float(i));
    }
    edge = 0.35;
    return s * 0.28;
#else
    // ribs run vertically; the glass may be turned a little (director)
    float ang = 0.18 * sin(0.02 * DIR_PH2 + 6.28 * iSeed.z) * 0.5;
    vec2 q = xst_rot(ang) * p;
    float u = q.x / rib;
    float cell = fract(u) * 2.0 - 1.0;                // -1 .. 1 across one rib
    // cylindrical lens: height sqrt(1 - c^2), slope -c / sqrt(1 - c^2) capped at the rib edge
    float sl = -cell / sqrt(max(1.0 - 0.94 * cell * cell, 0.06));
    edge = abs(cell);
    return xst_rot(-ang) * vec2(sl, 0.0) * (0.5 * xst_octave_fade(1.0 / rib));
#endif
}

vec3 lights(vec2 p, float gap, float sig2) {
    vec3 c = vec3(0.0);
    for (int k = 0; k < NSRC; k++) {
        if (k >= OPT_SOURCES) continue;
        vec2 d = p - src_pos(k);
        // per-source radius; widened by the frost variance
        float r2 = 0.045 + 0.02 * float(k & 1) + sig2;
        float w = exp(-dot(d, d) / (2.0 * r2));
        c += src_color(k) * (w * 0.05 / r2);
    }
    return c;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float edge;
    vec2 sl = glass(uv, edge);

    float gap = mix(0.10, 0.55, OPT_REFRACTION);
    float sig2 = mix(0.0006, 0.028, OPT_FROST);
    // dispersion: channel gaps differ by up to +-18%
    float disp = 0.18 * OPT_DISPERSION;
    vec3 col;
    col.r = lights(uv + sl * gap * (1.0 - disp), gap, sig2).r;
    col.g = lights(uv + sl * gap, gap, sig2).g;
    col.b = lights(uv + sl * gap * (1.0 + disp), gap, sig2).b;

    // the glass itself: ground tone, lit a little brighter across the rib crown
    vec3 base = PAL_BG * (0.22 + 0.16 * (1.0 - edge));
    vec3 lit = base + col * (0.9 * OPT_BRIGHTNESS);

    // raking highlight: bright where the surface faces the light direction
    vec2 L = normalize(vec2(-0.6, 0.8));
    float facing = max(dot(normalize(vec2(sl.x, 0.45)), L), 0.0);
    float grain = xst_hash21(floor(fragCoord * 0.5)) - 0.5;
    float spec = pow(facing, 12.0) * (0.10 + 0.05 * grain) * (1.0 - OPT_FROST * 0.5);
    lit += PAL_ACCENT * spec;

    // soft vignette so the panel reads as a lit sheet
    lit *= 1.0 - 0.18 * dot(uv * 0.6, uv * 0.6);
    fragColor = xst_output(lit, fragCoord);
}
