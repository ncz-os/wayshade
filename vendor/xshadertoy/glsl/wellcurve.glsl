// SPDX-License-Identifier: Apache-2.0
// wellcurve.glsl - a glowing grid on the "rubber sheet" of one or more masses.
//
// What it is: the classic embedding picture of curved space, drawn honestly as
// an analytic height field, not a physical simulation.
//   - The sheet is y = h(x, z) = -depth * sum_i m_i / sqrt(r_i^2 + s2)  (a softened
//     1/r potential well per mass; s2 = (2 w)^2 with w from "well width" is the softening).
//   - Masses circle their common centre on slowly varying orbits: angles are the
//     director's integrated phases (closed form, never jump), radii and mass
//     ratios come from the seed. With one mass the well sits still and the grid
//     turns slowly.
//   - Each pixel casts a ray from a fixed oblique camera, marches to the sheet
//     with a step that shrinks near the surface, then refines the hit with
//     bisection. The lines are then drawn in the sheet's own (x, z) coordinates
//     (square, or polar around the heaviest mass), with fwidth coverage, so the
//     grid compresses into every well exactly as the surface does.
//   - Line color follows the depth of the sheet: the palette body far from a
//     well, accent2 in the slopes, accent at the bottom, and a dim glow ties the
//     lines to the surface without lighting it.
// Why it changed: the old shader displaced a 2D grid radially in screen space,
// which drew no surface at all (nothing hid, nothing had a slope).
//
// Cost: steps (40 / 52 / 68) x (masses) softened-potential evaluations per pixel
// plus 7 bisections and one more sheet sample for the colour.
//
// Original work (LEAD 27, 2026-09-30), MIT.

//@saver id=wellcurve version=2 family=dark director=100
//@palette-native bg=#04050d body=#3ab7ff accent=#ff5fa8 accent2=#a37bff
//@option name=masses type=int default=2 min=1 max=4 rand=1:4 group=Simulation
//@+ label="Masses" desc="Number of bodies bending the sheet."
//@option name=depth type=float default=0.5 min=0 max=1 rand=0.35:0.75 group=Simulation
//@+ label="Depth" desc="How deep each well sinks."
//@option name=width type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Well width" desc="Softening of each well: small is a sharp funnel, large a broad dish."
//@option name=orbit type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Orbit speed" desc="How fast the masses circle each other."
//@option name=grid type=enum default=square choices=square,polar rand=square|polar group=Look
//@+ label="Grid" desc="Square lattice, or rings and spokes around the heaviest mass."
//@option name=density type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Grid density" desc="Lines per unit of the sheet."
//@option name=glow type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Glow" desc="Width of the halo around each line."
//@option name=tilt type=float default=0.5 min=0 max=1 rand=0.35:0.65 group=Look
//@+ label="Camera tilt" desc="Low looks along the sheet; high looks down onto it."
//@option name=brightness type=float default=1 min=0.5 max=1.6 rand=0.85:1.2 group=Look
//@+ label="Brightness" desc="Line intensity."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define STEPS 40
#elif NCZ_QUALITY == 1
#define STEPS 52
#else
#define STEPS 68
#endif

#define MAXM 4

vec4 mass_at(int i) {          // xz position, mass, unused
    float fi = float(i);
    if (OPT_MASSES == 1) return vec4(0.0, 0.0, 1.0, 0.0);
    float ph = DIR_PH0 * (0.020 + 0.05 * OPT_ORBIT) + 6.28 * iSeed.x;
    // orbit radius and phase per body; masses 1.0 .. 0.4
    float rad = (0.9 + 0.5 * fract(iSeed.y * (3.0 + fi))) * (0.5 + 0.12 * fi);
    float a = ph * (1.0 / (0.6 + 0.5 * fi)) + 6.28 * fi / float(OPT_MASSES) + 0.8 * fi * DIR_P0;
    float m = 1.0 - 0.18 * fi - 0.15 * fract(iSeed.w * (5.0 + fi));
    return vec4(rad * cos(a), rad * sin(a) * 0.9, max(m, 0.4), 0.0);
}

float sheet(vec2 xz, float depth, float s2) {
    float h = 0.0;
    for (int i = 0; i < MAXM; i++) {
        if (i >= OPT_MASSES) continue;
        vec4 m = mass_at(i);
        vec2 d = xz - m.xy;
        h -= m.z / sqrt(dot(d, d) + s2);
    }
    return h * depth;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);

    float depth = mix(0.35, 0.9, OPT_DEPTH);
    float s2 = mix(0.03, 0.20, OPT_WIDTH);
    s2 *= s2 * 4.0;

    // camera: above and behind, looking toward +z, tilted down
    float pitch = mix(0.30, 0.95, OPT_TILT);
    float cp = cos(pitch), sp = sin(pitch);
    float yaw = 0.12 * sin(0.03 * DIR_PH1);            // a slow sway
    vec3 ro = vec3(0.0, 1.5, -3.2);
    vec3 fw = vec3(sin(yaw) * cp, -sp, cos(yaw) * cp);
    vec3 rt = normalize(cross(vec3(0.0, 1.0, 0.0), fw));
    vec3 up = cross(fw, rt);
    vec3 rd = normalize(fw * 1.7 + rt * uv.x + up * uv.y);
    // aim at the sheet centre from the pitch: shift the origin so the view stays framed
    ro.z = -2.6 - 1.2 * (1.0 - OPT_TILT);

    // march to the sheet
    float t = 0.0, tp = 0.0;
    float hp = 1.0, hitv = 0.0;
    for (int i = 0; i < STEPS; i++) {
        vec3 p = ro + rd * t;
        float hy = p.y - sheet(p.xz, depth, s2);
        if (hy < 0.0) { hitv = 1.0; break; }
        tp = t; hp = hy;
        t += clamp(0.34 * hy, 0.045, 0.3);
        if (t > 14.0) break;
    }
    float tl = tp, th = t;
    // a ray that used up its steps skimming the steep far wall counts as a hit
    // at its last position (leaving it unhit speckled the inside of the well)
    if (hitv < 0.5 && hp < 0.2 && t < 14.0) { hitv = 1.0; t = tp; }
    else if (hitv > 0.5) {
        for (int i = 0; i < 7; i++) {
            float tm = 0.5 * (tl + th);
            vec3 p = ro + rd * tm;
            if (p.y - sheet(p.xz, depth, s2) < 0.0) th = tm; else tl = tm;
        }
        t = 0.5 * (tl + th);
    }
    vec3 P = ro + rd * t;

    // sky / far field: the palette ground with a faint horizon glow
    vec3 sky = PAL_BG * (1.0 + 2.2 * exp(-abs(rd.y + 0.05) * 6.0));
    vec3 col = sky;

    // grid coordinates on the sheet
    float dens = mix(1.6, 4.5, OPT_DENSITY);
    vec2 g;
#if OPT_GRID == 1
    vec4 m0 = mass_at(0);
    vec2 dd = P.xz - m0.xy;
    float rr = length(dd);
    g = vec2(log(rr + 0.15) * dens * 1.4, atan(dd.y, dd.x) * (dens * 2.4) / 6.2832);
#else
    g = P.xz * dens;
#endif
    vec2 fw2 = max(fwidth(g), 1e-4);
    vec2 dl = abs(fract(g - 0.5) - 0.5);
    vec2 lw = 0.02 + mix(0.04, 0.12, OPT_GLOW) * 0.5 + fw2;   // halo half width in cell units
    vec2 core = 1.0 - smoothstep(fw2 * 0.5, fw2 * 1.5 + 0.01, dl);
    vec2 halo = exp(-dl * dl / (lw * lw));
    // lines closer than about 2 px fade out (the steep far wall of a well, and the
    // polar angle's seam, where the derivative is meaningless)
    vec2 ok = vec2(1.0) - smoothstep(vec2(0.22), vec2(0.45), fw2);
    float lines = max(core.x * ok.x, core.y * ok.y);
    float glow = max(halo.x * ok.x, halo.y * ok.y);

    // sheet depth drives color
    float h = sheet(P.xz, depth, s2);
    float dn = clamp(-h / (1.1 * depth * 1.2), 0.0, 1.0);
    vec3 lc = mix(PAL_BODY, PAL_ACCENT2, smoothstep(0.1, 0.55, dn));
    lc = mix(lc, PAL_ACCENT, smoothstep(0.5, 1.0, dn));

    // fade far away (the grid thins into the horizon) and moiré at grazing angles
    float dist = t;
    float far = exp(-dist * 0.09);
    float graze = clamp(abs(rd.y) * 4.0, 0.0, 1.0);
    float valid = hitv * far;
    vec3 line = lc * (lines * (0.85 + 0.6 * dn) + glow * 0.22) * OPT_BRIGHTNESS;
    col += line * valid * mix(0.55, 1.0, graze);
    // the surface itself: a very dim body tint so the sheet reads as a surface
    col += lc * 0.03 * valid * (0.4 + dn);

    fragColor = xst_output(col, fragCoord);
}
