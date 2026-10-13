// SPDX-License-Identifier: Apache-2.0
// caustictidepool.glsl - a shallow tide pool from above: stones, sand and moving light.
//
// What it is: analytic small-slope water optics over a procedural pool floor.
//   - Water surface: a height field of six directional waves (a long swell set
//     and a short chop set, deep-water speed ratio, headings turning slowly with
//     the director). Gradient and Hessian are analytic.
//   - Light on the floor: irradiance 1 / |det(I + D Hess h)| with a floor on the
//     denominator (bounded focusing: an artistic caustic approximation, as in
//     caustics-on-stone), compressed so the net keeps a gradient.
//   - Seeing the floor through the water: each pixel looks at the floor point
//     displaced by the refracted surface slope (D' grad h), so stones wobble
//     gently under the ripples, as they do.
//   - Floor: rounded submerged stones (a jittered cell field of domed ellipses,
//     2x2 cells per pixel), sand between them, deeper water darker and bluer;
//     sparse sun glints where the surface faces the sun.
// The old core's static grain came from seeding time per PIXEL
// (fract(sin(dot(fragCoord, ...)))), so every pixel showed a different moment.
//
// Cost: 6 waves (sin+cos) + 4 stone cells + 3 gradient-noise evaluations: L-M.
//
// Original work (2026-09-27, core replaced 2026-09-29 by LEAD 27), MIT.

//@saver id=caustictidepool version=2 family=dark+water director=70
//@palette-native bg=#0a2a34 body=#c8b48c accent=#fff4d8 accent2=#5a6e4a
//@option name=focus type=float default=0.55 min=0.1 max=1 rand=0.4:0.75 group=Simulation
//@+ label="Focus" desc="How sharply the ripples focus sunlight into a net on the floor."
//@option name=chop type=float default=0.5 min=0 max=1 rand=0.3:0.75 group=Simulation
//@+ label="Chop" desc="Share of the short wind ripples against the long swell."
//@option name=depth type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Depth" desc="Water depth: deeper water is darker, bluer, and softens the net."
//@option name=stones type=float default=0.55 min=0 max=1 rand=0.35:0.8 group=Look
//@+ label="Stones" desc="How much of the floor is covered by stones rather than sand."
//@option name=stone-size type=float default=1 min=0.5 max=2 rand=0.8:1.4 group=Look
//@+ label="Stone size" desc="Size of the stones."
//@option name=glints type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Glints" desc="Sun glints on the ripple crests."
//@option name=sun type=float default=1 min=0.4 max=1.6 rand=0.85:1.15 group=Look
//@+ label="Sunlight" desc="Brightness of the direct light."

#include "xst_common.glsl"

// six waves: returns height gradient (xy) and Hessian (xx, yy, xy)
void waves(vec2 x, out vec2 grad, out vec3 hess) {
    grad = vec2(0.0); hess = vec3(0.0);
    float headA = XST_TAU * iSeed.w + 0.45 * (DIR_P1 - 0.5);
    float headB = headA + 1.9 + 0.9 * (DIR_P2 - 0.5);
    const vec3 spread = vec3(-1.05, 0.12, 1.21);
    const vec3 kmul = vec3(1.0, 1.37, 0.81);
    for (int f = 0; f < 2; f++) {
        float head = f == 0 ? headA : headB;
        float k0 = f == 0 ? 4.6 : 15.0;
        float amp = f == 0 ? 0.030 * (1.0 - 0.55 * OPT_CHOP) : 0.0085 * OPT_CHOP * (0.75 + 0.5 * DIR_P3);
        float ph = f == 0 ? 0.6 * DIR_PH0 : 1.0 * DIR_PH1 * sqrt(15.0 / 4.6);
        float sd = f == 0 ? iSeed.x : iSeed.y;
        for (int i = 0; i < 3; i++) {
            float a = head + spread[i] + 0.35 * (sd - 0.5);
            vec2 d = vec2(cos(a), sin(a));
            float k = k0 * kmul[i];
            float arg = k * dot(d, x) - sqrt(kmul[i]) * ph + 6.2831 * fract(sd * (3.1 + float(i)));
            float ai = amp / kmul[i];
            grad += ai * k * cos(arg) * d;
            hess += -ai * k * k * sin(arg) * vec3(d.x * d.x, d.y * d.y, d.x * d.y);
        }
    }
}

// stone field: returns (height of the stone surface 0..1 or -1 for sand, stone id)
vec2 stones(vec2 p) {
    vec2 c0 = floor(p);
    float best = -1.0, id = 0.0;
    for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++) {
        vec2 c = c0 + vec2(i, j) - step(fract(p), vec2(0.5));
        float h = xst_hash21(c + 7.0);
        if (h > OPT_STONES) continue;                       // sand cell
        vec2 ctr = c + 0.5 + 0.28 * (vec2(xst_hash21(c + 1.3), xst_hash21(c + 5.1)) - 0.5);
        float ang = 6.2831 * xst_hash21(c + 9.7);
        vec2 r = vec2(0.34 + 0.12 * xst_hash21(c + 2.2), 0.24 + 0.1 * xst_hash21(c + 4.4));
        vec2 q = xst_rot(ang) * (p - ctr) / r;
        float e = 1.0 - dot(q, q);                          // > 0 inside the ellipse
        if (e > best) { best = e; id = h; }
    }
    return vec2(best, id);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    // slow low-frequency warp breaks every lattice
    vec2 wp = uv + 0.25 * vec2(xst_noise(uv * 0.6 + 0.012 * iTime + 5.0), xst_noise(uv * 0.6 - 0.01 * iTime + 1.0));
    vec2 g; vec3 H;
    waves(wp, g, H);

    // floor seen through the water: displaced by the refracted slope
    float depth = mix(0.6, 1.6, OPT_DEPTH);
    vec2 fp = uv + 0.22 * depth * g;
    vec2 sp = fp * 1.5 / OPT_STONE_SIZE + iSeed.xy * 50.0;
    vec2 st = stones(sp);
    float dome = sqrt(clamp(st.x, 0.0, 1.0));
    float stone = xst_aastep(0.0, st.x);
    float sand = xst_noise(fp * 9.0 + 3.0) * xst_octave_fade(9.0 * 1.4);
    vec3 sandc = PAL_BODY * (0.78 + 0.18 * sand);
    vec3 stonec = mix(PAL_BODY * 0.8, PAL_ACCENT2 * 1.3, 0.25 + 0.5 * st.y) * (0.55 + 0.6 * dome);
    vec3 albedo = mix(sandc, stonec, stone);
    // floor depth: water between stones is deeper (darker, bluer)
    float wd = depth * (1.0 - 0.45 * stone * dome) * (0.85 + 0.3 * xst_noise(fp * 0.8 + 21.0));

    // caustic irradiance on the floor
    float Dj = mix(0.9, 2.6, OPT_FOCUS) * depth * (0.85 + 0.3 * DIR_P0);
    float det = (1.0 + Dj * H.x) * (1.0 + Dj * H.y) - Dj * Dj * H.z * H.z;
    float E = 1.0 / max(abs(det), mix(0.05, 0.3, OPT_DEPTH));
    float Ec = E / (1.0 + 0.3 * E);

    vec3 sunc = mix(vec3(1.0), PAL_ACCENT, 0.4) * OPT_SUN;
    vec3 floorlit = albedo * (0.28 + sunc * 0.62 * Ec * (0.7 + 0.3 * dome));
    // water column: absorbs toward the palette's water color with depth
    // water column: per-channel absorption toward the palette's water color,
    // plus a little in-scattered light of that color
    vec3 wc = clamp(PAL_BG / max(max(PAL_BG.r, PAL_BG.g), max(PAL_BG.b, 1e-3)), 0.02, 1.0);
    vec3 T = pow(wc, vec3(wd * 0.8));
    vec3 col = floorlit * T + PAL_BG * (1.0 - exp(-wd * 1.2)) * 0.8;

    // sun glints: where the surface normal points at the sun (sparse, soft)
    vec2 sun = vec2(0.30, 0.45);
    float spec = pow(clamp(1.0 - 50.0 * length(g - sun * 0.06), 0.0, 1.0), 3.0);
    col += sunc * spec * OPT_GLINTS * 1.4;

    fragColor = xst_output(col, fragCoord);
}
