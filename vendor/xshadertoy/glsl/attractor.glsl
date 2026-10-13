// SPDX-License-Identifier: Apache-2.0
// attractor.glsl - a Clifford attractor drawn as a field of filament whiskers.
//
// What it is: a procedural approximation of the look of a strange-attractor
// density image, not a histogram. A real attractor picture accumulates millions
// of iterates; a stateless fragment shader cannot. What it can do per pixel is
// iterate a fixed handful of orbits and, at every iterate, draw the short piece
// of the attractor's filament that passes through it:
//   - The map is Clifford's:  x' = sin(a y) + c cos(a x),  y' = sin(b x) + d cos(b y)
//     (Pickover / Bourke), with (a, b, c, d) taken from one of five published
//     parameter sets known to be chaotic, not drawn at random (random Clifford
//     parameters often collapse to a cycle or a point).
//   - Alongside the orbit the shader carries a tangent vector, pushed through
//     the analytic Jacobian each step and renormalised. It converges to the
//     dominant stretching direction, which is the direction of the filament
//     through that iterate. Each iterate becomes a short soft segment along
//     that direction (length option "whisker"), so a few hundred orbit points
//     read as continuous strands instead of dust.
//   - Orbit initial conditions are fixed by the seed and never depend on time;
//     the parameters drift slowly (director phases, options "drift") so the
//     picture morphs continuously. The old shader drew its seed from
//     launchRand(iTime) and perturbed the initial conditions with time: a chaotic
//     map turned that into shimmer.
//   - Density is tone-mapped 1 - exp(-k rho); the hue follows the filament's
//     direction, mixing the palette roles.
//
// Cost per pixel: trajectories x (burn-in + drawn steps) map iterations, each 4 sin
// and 4 cos (reused for the Jacobian): 6 x (4 + 10) at quality 0, 8 x (4 + 12) at 1,
// 10 x (4 + 16) at 2, i.e. 84, 128 and 200 iterations. Pixels outside the attractor's
// extent skip the loop.
//
// Original work (LEAD 27, 2026-09-30), MIT. Parameter sets: Paul Bourke,
// "Clifford Attractors" (paulbourke.net/fractals/clifford).

//@saver id=attractor version=2 family=dark director=90
//@palette-native bg=#04060d body=#7fc4ff accent=#ff86bd accent2=#ffd27a
//@option name=form type=enum default=ribbon choices=ribbon,lantern,web,shell,bloom rand=ribbon|lantern|web|shell|bloom group=Simulation
//@+ label="Form" desc="Which Clifford parameter set: ribbon (folded sheets), lantern, web (dense net), shell (nested loops), bloom (open petals)."
//@option name=drift type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Drift" desc="How far the parameters wander over minutes. 0 holds the attractor still; high drift morphs it visibly."
//@option name=trajectories type=int default=10 min=3 max=10 rand=7:10 group=Simulation
//@+ label="Orbits" desc="Number of orbits drawn (quality 0 caps it at 6, quality 1 at 8). More orbits fill the strands in."
//@option name=whisker type=float default=0.55 min=0 max=1 rand=0.35:0.75 group=Look
//@+ label="Filament length" desc="Length of the strand drawn through each iterate."
//@option name=thickness type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Thickness" desc="Width of the strands."
//@option name=glow type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Glow" desc="Soft halo around the strands."
//@option name=brightness type=float default=1 min=0.4 max=1.8 rand=0.85:1.25 group=Look
//@+ label="Brightness" desc="Exposure of the density."
//@option name=zoom type=float default=1 min=0.6 max=1.6 rand=0.9:1.25 group=Look
//@+ label="Zoom" desc="Scale of the view."
//@option name=turn type=bool default=true rand=0.7 group=Look
//@+ label="Slow turn" desc="Rotate the view very slowly."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define NTRAJ 6
#define NSTEP 10
#elif NCZ_QUALITY == 1
#define NTRAJ 8
#define NSTEP 12
#else
#define NTRAJ 10
#define NSTEP 16
#endif
#define BURN 4

vec4 params(int f) {
    if (f == 0) return vec4(-1.4, 1.6, 1.0, 0.7);
    if (f == 1) return vec4(1.6, -0.6, -1.2, 1.6);
    if (f == 2) return vec4(1.7, 1.7, 0.6, 1.2);
    if (f == 3) return vec4(1.5, -1.8, 1.6, 0.9);
    return vec4(-1.7, 1.3, -0.1, -1.21);
}
// centre and half extent (x, y) of each set's attractor: measured over 2000 orbits
// of 280 iterates each (0.3 to 99.7 percentile), so the picture fills the frame
void extent(int f, out vec2 c, out vec2 h) {
    if (f == 0) { c = vec2(0.29, 0.09);  h = vec2(1.68, 1.38); return; }
    if (f == 1) { c = vec2(0.07, 0.92);  h = vec2(1.89, 1.44); return; }
    if (f == 2) { c = vec2(0.09, -0.01); h = vec2(1.51, 2.19); return; }
    if (f == 3) { c = vec2(-0.01, 0.0);  h = vec2(2.55, 1.88); return; }
    c = vec2(-0.03, -0.20); h = vec2(1.05, 1.99);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float aspect = iResolution.x / iResolution.y;

    // parameters: the chosen set plus a slow bounded wander (director phases)
    vec4 P = params(OPT_FORM);
    float dr = 0.07 * OPT_DRIFT;
    P += dr * vec4(sin(0.031 * DIR_PH0), sin(0.027 * DIR_PH1 + 1.3),
                   sin(0.023 * DIR_PH2 + 2.1), sin(0.019 * DIR_PH3 + 0.7));
    float a = P.x, b = P.y, c = P.z, d = P.w;

    // view: fit the attractor's extent, a little slack
    vec2 ctr, hext;
    extent(OPT_FORM, ctr, hext);
    float S = 1.08 * max(hext.y, hext.x / aspect) / OPT_ZOOM;
    float ang = (OPT_TURN != 0) ? 0.012 * DIR_PH4 : 0.0;
    vec2 p = xst_rot(ang) * uv * S + ctr;
    float pixS = xst_px() * S;                       // one pixel in map space

    float w = max(mix(0.006, 0.026, OPT_THICKNESS) * S, 1.4 * pixS);
    float L = mix(0.012, 0.10, OPT_WHISKER) * S;   // sigma along the strand
    float halo = mix(0.0, 1.0, OPT_GLOW);

    int nt = NTRAJ;
#if NCZ_QUALITY == 0
    nt = min(OPT_TRAJECTORIES, 6);
#else
    nt = min(OPT_TRAJECTORIES, NTRAJ);
#endif

    // Pixels outside the attractor's extent (with slack for drift and the halo) cannot
    // be touched by any strand: skip the orbit loop there. Whole tiles take the same
    // branch, so this halves the work for most forms.
    vec2 off = abs(p - ctr) / (hext * 1.10 + 0.5 * L + 3.0 * w);
    if (max(off.x, off.y) > 1.0) {
        vec3 bg0 = PAL_BG * (1.0 + 0.9 * (1.0 - smoothstep(0.0, 1.4, length(uv))));
        fragColor = xst_output(bg0, fragCoord);
        return;
    }

    float rho = 0.0, halo_rho = 0.0;
    vec3 tint = vec3(0.0);
    float invw2 = 1.0 / (w * w);
    float invh2 = 1.0 / (9.0 * w * w);

    for (int k = 0; k < NTRAJ; k++) {
        if (k >= nt) break;
        float fk = float(k);
        // fixed, seed-dependent initial condition (never a function of time)
        vec2 x = vec2(fract(iSeed.x * 7.13 + fk * 0.6180339) - 0.5,
                      fract(iSeed.y * 5.71 + fk * 0.7548777) - 0.5) * 2.0;
        vec2 v = normalize(vec2(cos(fk * 2.3999 + 6.28 * iSeed.z), sin(fk * 2.3999 + 6.28 * iSeed.z)));
        for (int i = 0; i < BURN + NSTEP; i++) {
            float sax = sin(a * x.x), cax = cos(a * x.x);
            float say = sin(a * x.y), cay = cos(a * x.y);
            float sbx = sin(b * x.x), cbx = cos(b * x.x);
            float sby = sin(b * x.y), cby = cos(b * x.y);
            // Jacobian of the map at x, applied to the tangent vector
            vec2 vn = vec2(-a * c * sax * v.x + a * cay * v.y,
                            b * cbx * v.x - b * d * sby * v.y);
            x = vec2(say + c * cax, sbx + d * cby);
            v = vn / max(length(vn), 1e-6);
            if (i >= BURN) {
                vec2 q = p - x;
                float t = dot(q, v);
                vec2 r = q - v * t;
                float d2 = dot(r, r);
                float along = t * t / (L * L);
                float e = exp(-d2 * invw2 - along);
                rho += e;
                halo_rho += exp(-d2 * invh2 - 0.25 * along);
                // hue from the strand direction (orientation, not sign)
                float o = 0.5 + 0.5 * cos(2.0 * atan(v.y, v.x));
                tint += e * vec3(o, 1.0 - o, float(i - BURN) / float(NSTEP));
            }
        }
    }

    float norm = 1.0 / (float(nt) * float(NSTEP) * 0.05);   // ~ fills a pixel-wide strand at 1
    float dens = rho * norm * OPT_BRIGHTNESS;
    float lum = 1.0 - exp(-1.6 * dens);
    float hl = 1.0 - exp(-0.55 * halo_rho * norm * OPT_BRIGHTNESS);

    vec3 tc = tint / max(rho, 1e-4);
    // orientation mixes body and accent; iterate age adds the second accent
    vec3 col = mix(PAL_BODY, PAL_ACCENT, tc.x * 0.85);
    col = mix(col, PAL_ACCENT2, tc.z * 0.45 * (1.0 - tc.x * 0.4));
    // the hottest crossings burn toward the highlight
    col = mix(col, vec3(1.0), smoothstep(0.65, 1.0, lum) * 0.35);

    vec3 bg = PAL_BG * (1.0 + 0.9 * (1.0 - smoothstep(0.0, 1.4, length(uv))));
    vec3 lit = col * lum * 1.15 + mix(PAL_BODY, PAL_ACCENT2, 0.5) * hl * (0.16 + 0.34 * halo);
    fragColor = xst_output(bg + lit, fragCoord);
}
