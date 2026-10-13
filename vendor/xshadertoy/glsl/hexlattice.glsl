// hexlattice.glsl - an elastic lattice under traveling strain.
//
// What it is: a triangular strut lattice (three families of struts meeting at
// six-way junctions) drawn in its REST coordinates, seen through a smooth,
// bounded elastic displacement u(x, t):
//
//   X = x - u(x, t),  u = sum of a few traveling plane strain waves
//                         + a slow, low-frequency bend (gradient noise)
//
// Because every strut is drawn from the same continuous map, the net always
// stays connected: nothing jitters independently, it stretches and compresses
// as one body. The amplitude is capped so the map never folds (|grad u| below about 0.65 even at the largest strain, well under 1, so the map stays locally invertible).
// Strut brightness follows the local stretch measured along each strut family
// (tension brightens toward the accent, compression dims), so the strain waves
// are visible as bands traveling across the net. A few junctions glow: each
// lit junction has its own slow phase (no synchronized pulses), and a broad
// emphasis field drifts across the screen over minutes deciding where they are.
// This is a kinematic picture of elasticity (prescribed displacement), not a
// solved elastic body.
//
// Cost: 3 strut families (fract + fwidth) + 3 nearest-junction tests (hashes,
// analytic emphasis) + 3 gradient-noise evaluations + 3 waves; quality 1+ adds a dim, slower far
// layer (a second set of struts). Cost class L (a few analytic fields, about 2-6
// noise evaluations per pixel) on every tier.
//
// Original work (2026-09-26, reworked 2026-09-29 by LEAD 27), MIT.

//@saver id=hexlattice version=2 family=dark+night|dark+ink|dark+cool director=75
//@palette-native bg=#071a20 body=#5fb8c2 accent=#d8fbff accent2=#e0b070
//@option name=density type=float default=6 min=3 max=14 rand=4.5:7.5 group=Simulation
//@+ label="Cells" desc="Lattice cells across the screen height."
//@option name=strain type=float default=0.55 min=0 max=1 rand=0.35:0.75 group=Simulation
//@+ label="Strain" desc="Amplitude of the elastic displacement. Capped so the lattice never folds or tears."
//@option name=wave-speed type=float default=1 min=0.2 max=2 rand=0.7:1.25 group=Motion
//@+ label="Wave speed" desc="Speed of the traveling strain waves."
//@option name=junctions type=float default=0.24 min=0 max=1 rand=0.14:0.4 group=Look
//@+ label="Lit junctions" desc="Share of the junctions that glow at the brightest part of the emphasis field."
//@option name=strut-width type=float default=1 min=0.5 max=2 rand=0.8:1.3 group=Look
//@+ label="Strut width" desc="Width of the struts relative to a cell."
//@option name=depth-layer type=bool default=true rand=0.7 group=Look
//@+ label="Far layer" desc="A dimmer, slower lattice behind the first (medium quality and up)."
//@option name=drift type=float default=1 min=0 max=2 rand=0.6:1.3 group=Motion
//@+ label="Drift" desc="Slow translation and rotation of the whole lattice."

#include "xst_common.glsl"

#define SQ3 1.73205080757

// lattice coordinates (i, j) of the triangular lattice a1 = (1,0), a2 = (1/2, sqrt3/2)
vec2 to_lat(vec2 X) { return vec2(X.x - X.y / SQ3, 2.0 * X.y / SQ3); }
vec2 from_lat(vec2 L) { return vec2(L.x + 0.5 * L.y, 0.5 * SQ3 * L.y); }

// displacement field u(x) and the stretch it causes; x in cell units
vec2 displacement(vec2 x, float t, float amp, float seed) {
    vec2 u = vec2(0.0);
    // three traveling longitudinal/transverse strain waves; wavelengths of 3-7 cells
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float a = XST_TAU * fract(seed * (1.7 + fi * 0.61)) + 0.25 * (DIR_P1 - 0.5) * (1.0 + fi);
        vec2 d = vec2(cos(a), sin(a));
        float k = XST_TAU / (3.2 + 1.9 * fi + 1.5 * fract(seed * 7.3 + fi));
        float ph = k * dot(d, x) - (0.32 + 0.12 * fi) * t + XST_TAU * fract(seed * 3.1 * (fi + 1.0));
        // mix of longitudinal (along d) and transverse (perpendicular) polarization
        vec2 pol = mix(d, vec2(-d.y, d.x), fract(seed * 5.7 + fi * 0.37));
        // |grad u_i| = amp * 0.16 * (0.6..1.1): about 0.075 per wave at the default strain, 0.176 at strain 1
        float ai = amp * 0.16 / k * (0.6 + 0.5 * (i == 0 ? DIR_P2 : i == 1 ? DIR_P3 : DIR_P4));
        u += ai * pol * sin(ph);
    }
    // slow broad bend
    u += amp * 0.35 * vec2(xst_noise(x * 0.09 + vec2(0.0, 0.021 * t)), xst_noise(x * 0.09 + 17.0 - vec2(0.017 * t, 0.0)));
    return u;
}

// strut coverage and junction data for one lattice layer.
// returns vec4(strut coverage, tension shading, junction glow, junction core)
vec4 lattice(vec2 x, float t, float amp, float seed, float width, float jfrac, vec2 emph_uv) {
    vec2 X = x - displacement(x, t, amp, seed);
    vec2 L = to_lat(X);
    // three strut families: i = n, j = n, i + j = n
    vec3 f = vec3(L.x, L.y, L.x + L.y);
    vec3 fw = fwidth(f);
    vec3 g = abs(fract(f + 0.5) - 0.5);               // distance to the nearest strut, lattice units
    vec3 hw = vec3(0.045 * width);
    vec3 cov = 1.0 - smoothstep(hw - fw, hw + fw, g);
    // stretch per family: the rest-to-screen scale of the family coordinate
    // (one lattice unit per strut spacing when unstrained)
    vec3 grad = vec3(length(vec2(dFdx(f.x), dFdy(f.x))), length(vec2(dFdx(f.y), dFdy(f.y))),
                     length(vec2(dFdx(f.z), dFdy(f.z))));
    vec3 stretch = grad / max(vec3(length(vec2(dFdx(x.x), dFdy(x.x)))) * (2.0 / SQ3), vec3(1e-6)) - 1.0;
    float c = max(cov.x, max(cov.y, cov.z));
    // tension (grad smaller = spacing larger = stretched) brightens
    float tension = dot(cov, -stretch) / max(cov.x + cov.y + cov.z, 1e-3);
    // nearest junction: vertices of the containing triangle
    vec2 base = floor(L);
    vec2 fr = L - base;
    vec2 v0 = base, v1 = base + vec2(1.0, 0.0), v2 = base + vec2(0.0, 1.0);
    if (fr.x + fr.y > 1.0) v0 = base + vec2(1.0, 1.0);
    float glow = 0.0, core = 0.0;
    float px = length(fwidth(L));                        // pixel footprint in cells, taken before any branch
    for (int i = 0; i < 3; i++) {
        vec2 v = i == 0 ? v0 : i == 1 ? v1 : v2;
        float h = xst_hash21(v * 1.37 + seed * 91.0);
        float h2 = xst_hash21(v * 0.73 + 41.0 + seed * 13.0);
        // where the emphasis field is strong, more junctions light; each has its own slow phase
        vec2 vx = from_lat(v);
        // broad emphasis field, analytic (two crossed slow waves) and evaluated at
        // the junction, so a halo is never cut by a triangle edge
        vec2 e = emph_uv + vx * 0.07;
        float emph = smoothstep(0.1, 0.85, (0.5 + 0.5 * sin(e.x + 0.6 * sin(e.y * 0.8))) * (0.5 + 0.5 * sin(e.y * 1.3 - 0.5 * sin(e.x * 0.7))) * 1.6);
        float lit = step(h, jfrac * emph);
        float breath = 0.55 + 0.45 * sin(XST_TAU * h2 + t * (0.16 + 0.22 * h));
        float d = length(from_lat(L - v));              // distance in rest cell units
        glow += lit * breath * exp(-d * d / 0.09);
        core += lit * breath * (1.0 - smoothstep(0.085 * width - px, 0.085 * width + px, d));
    }
    return vec4(c, tension, glow, core);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float t = iTime * OPT_WAVE_SPEED;
    // slow drift of the whole lattice (director phases: continuous for fixed options)
    float rot = 0.05 * OPT_DRIFT * sin(0.021 * DIR_PH5) + XST_TAU * iSeed.x;
    vec2 drift = OPT_DRIFT * vec2(0.012 * DIR_PH6, 0.008 * DIR_PH7);
    vec2 emph_uv = vec2(0.021, -0.015) * iTime + iSeed.zw * 50.0;
    float amp = OPT_STRAIN;
    float cells = OPT_DENSITY * 0.5;                    // uv spans 2 units of height

    vec3 col = PAL_BG * (0.9 + 0.2 * xst_noise(uv * 0.8 + 3.0));

#if NCZ_QUALITY >= 1
    if (OPT_DEPTH_LAYER == 1) {
        // far layer: smaller cells, dimmer, slower, offset
        vec2 xf = xst_rot(rot + 0.52) * uv * cells * 1.6 + drift * 0.45 + 31.0;
        vec4 far = lattice(xf, t * 0.55, amp * 0.8, fract(iSeed.y + 0.37), OPT_STRUT_WIDTH * 0.9, 0.0, emph_uv);
        col = mix(col, mix(PAL_BG, PAL_BODY, 0.22), far.x * 0.45);
    }
#endif

    vec2 x = xst_rot(rot) * uv * cells + drift;
    vec4 near = lattice(x, t, amp, iSeed.y, OPT_STRUT_WIDTH, OPT_JUNCTIONS, emph_uv);
    float ten = clamp(near.y * 6.0, -1.0, 1.0);
    vec3 strut = PAL_BODY * (0.72 + 0.45 * ten);
    strut = mix(strut, PAL_ACCENT2, max(ten, 0.0) * 0.5);
    col = mix(col, strut, near.x);
    col += PAL_ACCENT * near.z * 0.45;                  // halo in linear light
    col = mix(col, PAL_ACCENT * 1.1, clamp(near.w, 0.0, 1.0));

    fragColor = xst_output(col, fragCoord);
}
