// SPDX-License-Identifier: Apache-2.0
#version 300 es
// goo.frag -- magmasimplex "sixties": wax in a glowing liquid, seen from
// inside the lamp, with the eye against the glass, or as a liquid light show.
//
// Ground truth is the operator's reference photographs of real lamps
// (docs/MAGMASIMPLEX-60S.md): the LIQUID carries the colour -- saturated,
// glowing, brightest over the bulb, dark toward the top and the edges -- and
// the WAX is a milky, nearly opaque, matte material in a complementary colour,
// lumpy and pitted, with light getting through only where it is thin. Not
// glass balloons, not candy: no point highlights anywhere.
//
// Geometry: an implicit field per depth layer, summed from the wax bodies and
// strands (necks, towers, stalactites) the CPU simulates
// (src/magmasimplex_goo_sim.c), plus the heaving pool below and the cap mass
// above. The world maps screen to simulation space: a tank (heat below), a
// dish (heat at one side), radial (a hot centre, log-polar so shapes keep
// their proportions) or drift (gravity swinging slowly).
//
// Views (u_modeA/u_modeB, cross-faded by u_mmix, never cut):
//   0 inside  -- the camera hangs in the liquid; three layers at different
//                depths drift with parallax, far wax fogs into the liquid,
//                near wax passes huge and out of focus.
//   1 glass   -- extreme macro against the glass: wax pressed flat on the
//                inside with a sharp contact line and thin films, the rest of
//                the lamp soft behind, dust and smudges on the curved glass.
//   2 show    -- a 1960s liquid light show: inks and oil between glass dishes
//                projected on a screen; the wax bodies become oil lenses.
//   3 field   -- the classic full-screen view: one layer, pool below, cap
//                above, no depth layers (cheapest; the weak-GPU fallback).
//   4 butterfly -- "Flowerchild": an overhead-projector liquid light show reproduced from
//                reference footage: saturated dye fields, oil droplets with
//                dark rims, lacy fingering, a slow breathing (2026-10-05).
precision highp float;
out vec4 fragColor;

#define LAYERS 3
#define PERL 12
#define PERS 6
#define MAXB 36
#define MAXS 18
uniform vec2  u_res;
uniform float u_time;
uniform float u_seed;
uniform float u_quality;       // 0 = light (weak GPUs), 1 = full
uniform float u_exposure;
uniform float u_flicker;       // bulb intensity, drifts well under 1 Hz
uniform vec4  u_ball[MAXB];    // x, y, radius, temperature
uniform vec4  u_shape[MAXB];   // dir x, dir y, stretch, hue + 2 * texture id
uniform vec4  u_sa[MAXS];      // strand: x0, y0, x1, y1
uniform vec4  u_sb[MAXS];      // strand: r0, r1, beading, temperature
uniform ivec3 u_nl;            // bodies per layer
uniform ivec3 u_ns;            // strands per layer
uniform vec3  u_cam[LAYERS];   // per layer: offset x, offset y, scale
uniform vec4  u_world;         // type, angle, zoom, simulation aspect
uniform vec4  u_pool;          // level, wave amplitude, wave phase, heat
uniform float u_cap;           // cap mass at the top: 0 none .. 1 full
uniform vec3  u_liquid, u_wax, u_secondary, u_bulb;
uniform int   u_modeA, u_modeB;
uniform float u_mmix;
uniform float u_kal;           // light show: kaleidoscope sectors, below 2 = off
uniform vec4  u_look;          // material (0 milky, 1 translucent, 2 jelly), texture, motes, glass
uniform vec4  u_look2;         // depth, glitter kind (0 none, 1 silver, 2 gold, 3 rainbow), glitter amount, warmth
uniform float u_mist;          // hazy liquid (the "Mist" lamps)
uniform float u_mask;          // light shows: 0 fill, 1 soft round throw and dark surround, 2 dish on black, 3 large cool disc
uniform float u_mask_size;     // projector-mask dish/disc radius: 0 = style default (0.92), else 0.30..1.00
uniform float u_overlay;       // Flowerchild projector layers: 0 off, 1 streaks, 2 neon, 3 both
uniform float u_poster;        // Flowerchild: 1 = flat colour bands with hard edges (poster art)
// Beat / audio layer (2026-10-05, docs/AUDIO-BUS.md). Every term below is
// multiplied by a drive that is exactly 0 when dynamics=0, so u_dyn=0 leaves
// the pre-beat renderer unchanged. u_radius is bounded to +/-4 %.
uniform float u_dyn, u_beat, u_snare, u_bass, u_mid, u_phase;
uniform float u_radius, u_squeeze, u_burst, u_flow, u_breath, u_bloom, u_glow;
uniform float u_dendrites;     // Flowerchild: 0 off, 1 ink tree, 2 vein network
uniform float u_dendrite;      // Flowerchild: dendrite strength 0..1
// Pattern seeding (2026-10-05): the dye/ink FIELD LAYOUT of the light shows is
// drawn from the run/slice seed, not fixed. u_pattern: x,y = noise-domain
// offset, z = field rotation, w = field zoom (bounded). u_warp: x = warp
// amplitude, y,z = warp flow direction, w = droplet/mask phase. All bounded so
// a preset's look stays recognisable; only the arrangement changes.
uniform vec4  u_pattern;
uniform vec4  u_warp;
// Seeded emitter x: the hot spot the liquid glows around sits over a different
// column per seed (operator, 2026-10-05, Lava presets), so the lamp's bright
// region is not in the same place every run.
uniform float u_heat_x;

const float ISO = 0.33;
const float PI = 3.14159265;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
vec2 hash22(vec2 p) { return vec2(hash12(p), hash12(p + 17.17)); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p, int oct) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) {
        if (i >= oct) break;
        s += a * vnoise(p);
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        a *= 0.5;
    }
    return s / (1.0 - exp2(-float(oct)));
}
vec2 rot(vec2 v, float a) { float c = cos(a), s = sin(a); return vec2(c * v.x - s * v.y, s * v.x + c * v.y); }
// Polynomial atan2, within 0.005 rad: enough for a shape phase, a fraction
// of the cost of atan() on Mali.
float fastAtan2(float y, float x) {
    float ax = abs(x), ay = abs(y);
    float a = min(ax, ay) / max(max(ax, ay), 1e-6);
    float s = a * a;
    float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;
    if (ay > ax) r = 1.57079637 - r;
    if (x < 0.0) r = 3.14159274 - r;
    return y < 0.0 ? -r : r;
}

// Screen to simulation space for layer camera c (offset xy, scale z).
vec2 layerMap(vec2 p, vec3 c, float k);
// Screen (already divided by the layer scale) to simulation space.
vec2 worldMap(vec2 v) {
    v = rot(v * u_world.z, u_world.y);
    if (u_world.x > 1.5 && u_world.x < 2.5) {
        // Radial: log-polar, conformal, so bodies keep their shape and grow
        // as they move out from the hot centre.
        float c = u_world.w / PI;
        float r = max(length(v), 1e-3);
        return vec2(atan(v.y, v.x) * c, c * (log(r) - 0.713) + 1.0);
    }
    return v;
}

vec2 layerMap(vec2 p, vec3 c, float k) { return worldMap(p / max(c.z * k, 1e-2) + c.xy); }

// Light from the bulb, which sits below the bottom of the simulation.
float glowAt(vec2 q) {
    float up = q.y + 1.05;
    float hx = q.x - u_heat_x;
    return min(exp(-up * 1.15) * (0.55 + 0.45 * exp(-hx * hx * 0.7)) + 0.03, 1.25) * u_flicker;
}

float poolY(float x, out float slope) {
    float a = u_pool.y, ph = u_pool.z;
    // A heaving, lumpy mass rather than a level band.
    float n0 = vnoise(vec2(x * 2.3, ph * 0.3)), n1 = vnoise(vec2(x * 2.3 + 0.05, ph * 0.3));
    slope = a * (2.3 * cos(x * 2.3 + ph) + 0.35 * 5.1 * cos(x * 5.1 - ph * 1.3)) + 0.10 * (n1 - n0) / 0.05;
    return u_pool.x + a * (sin(x * 2.3 + ph) + 0.35 * sin(x * 5.1 - ph * 1.3)) + 0.10 * (n0 - 0.5);
}
// The cooled wax that collects under the cap: a soft, gently scalloped
// lens (the reference lamps show a smooth bulge, not a row of bells).
float capY(float x, out float slope) {
    float ph = u_pool.z * 0.4;
    float n0 = vnoise(vec2(x * 3.1 + 40.0, ph)), n1 = vnoise(vec2(x * 3.1 + 40.05, ph));
    float bells = abs(sin(x * 4.2 + 0.3 * sin(ph)));
    slope = 0.035 * 4.2 * cos(x * 4.2) * sign(sin(x * 4.2)) - 0.09 * (n1 - n0) / 0.05;
    return 0.90 - 0.035 * (1.0 - bells) - 0.09 * n0 + (1.0 - u_cap) * 0.3;
}

// Field value F, gradient G, temperature T, hue H and a texture coordinate U
// that moves with the wax (so pits and marbling travel with the body instead
// of swimming through it).
void field(int l, vec2 p, out float F, out vec2 G, out float T, out float H, out vec2 U, out vec2 S) {
    F = 0.0; G = vec2(0.0); T = 0.0; H = 0.0; U = vec2(0.0); S = vec2(0.0, 1.0);
    float wsum = 0.0;
    int n = l == 0 ? u_nl.x : (l == 1 ? u_nl.y : u_nl.z);
    int base = l * PERL;
    for (int k = 0; k < PERL; k++) {
        if (k >= n) break;
        vec4 b = u_ball[base + k];
        vec4 sh = u_shape[base + k];
        vec2 d = p - b.xy;
        float R = b.z * 1.8;
        float ext = max(1.8, 1.0 + sh.z) * 1.15;   // stretched bodies reach R (1 + stretch)
        if (dot(d, d) > R * R * ext * ext) continue;
        vec2 dir = sh.xy, perp = vec2(-dir.y, dir.x);
        float sa = 1.0 / (1.0 + sh.z), sp = 1.0 + 0.5 * sh.z;
        float al = dot(d, dir);
        // Bulbous leading end, narrower trailing end: bells, pears, drops.
        float ar = clamp(al / R, -1.0, 1.0);
        float taper = 1.0 - 0.28 * sh.z * ar + 0.06 * (1.0 - tanh(ar * 3.0)) * (1.0 - ar * ar);
        vec2 q = vec2(al * sa, dot(d, perp) * sp * taper);
        float tid = floor(sh.w * 0.5);
        // Real wax is never a clean ellipse (reference frames: lumps, knobs,
        // a flattened side): a slow, low-order deformation per body, with a
        // different phase for each, makes every mass an irregular lump.
        float ang = fastAtan2(q.y, q.x);
        float def = 1.0 + 0.05 * sin(3.0 * ang + tid * 1.7 + u_time * 0.03);
        float R2 = R * R * def * def;
        float x2 = dot(q, q) / R2;
        if (x2 >= 1.0) continue;
        float o = 1.0 - x2;
        float kk = o * o * o;
        vec2 dq = -6.0 * o * o * q / R2;
        G += dir * dq.x * sa + perp * dq.y * sp * taper;
        F += kk;
        T += kk * b.w;
        H += kk * (sh.w - 2.0 * tid);
        U += kk * (d / max(b.z, 0.03) * 0.35 + hash22(vec2(tid, 3.7)) * 1.7);
        S += kk * dir;
        wsum += kk;
    }
    // Strands: necks, tower stalks, stalactites and beaded threads.
    int ns = l == 0 ? u_ns.x : (l == 1 ? u_ns.y : u_ns.z);
    int sbase = l * PERS;
    for (int k = 0; k < PERS; k++) {
        if (k >= ns) break;
        vec4 A = u_sa[sbase + k], B = u_sb[sbase + k];
        vec2 a = A.xy, ba = A.zw - A.xy;
        vec2 pa = p - a;
        float bb = max(dot(ba, ba), 1e-6);
        float h = clamp(dot(pa, ba) / bb, 0.0, 1.0);
        vec2 dv = pa - ba * h;
        float len = sqrt(bb);
        // Beads on thinning threads; stacked, irregular knuckles on towers
        // (two incommensurate harmonics so the lobes are not a regular chain).
        // About one lobe per diameter, so knuckles stay round on a thick
        // column and become fine beads on a thin thread (a fixed wavelength
        // made thick columns serrated).
        float r0 = max(mix(B.x, B.y, h), 0.004);
        float ph = h * len / (3.5 * r0) * 6.2831 + float(k) * 2.1;
        // Uneven spacing and size (a regular chain of beads read as a string
        // of pearls, not wax: operator photo IMG_8240).
        ph += 1.3 * sin(ph * 0.31 + float(k));
        float amp = 0.14 + 0.12 * hash12(vec2(floor(ph / 6.2831), float(k) + 0.5));
        float lobe = B.z * smoothstep(0.012, 0.035, r0);
        float R = r0 * (1.0 + lobe * (amp * sin(ph) + 0.08 * sin(ph * 1.7 + 1.3))) * 1.8;
        float x2 = dot(dv, dv) / max(R * R, 1e-6);
        if (x2 >= 1.0) continue;
        float o = 1.0 - x2;
        float kk = o * o * o;
        G += -6.0 * o * o * dv / (R * R);
        F += kk;
        T += kk * B.w;
        // Strands take world-space texture (like the pool): a coordinate along
        // a thin strand compressed the marbling into wood-grain rings.
        U += kk * (p * 2.2 + float(k) * 0.9);
        S += kk * ba / len;
        wsum += kk;
    }
    // The pool and the cap exist once, in the far layer: nearer layers are
    // mid-column, between them. (v1 drew them per layer: stacked bands.)
    if (l != 0) {
        if (wsum > 1e-5) { T /= wsum; H /= wsum; U /= wsum; }
        S = normalize(S + vec2(0.0, 1e-4));
        return;
    }
    // The pool: a slab under a heaving surface.
    float slope;
    float ys = poolY(p.x, slope);
    float yd = p.y - ys;
    float s0 = -0.20, s1 = 0.22;
    float tt = clamp((yd - s0) / (s1 - s0), 0.0, 1.0);
    float kp = 1.4 * (1.0 - tt * tt * (3.0 - 2.0 * tt));
    float dk = (yd > s0 && yd < s1) ? -1.4 * 6.0 * tt * (1.0 - tt) / (s1 - s0) : 0.0;
    F += kp;
    G += vec2(-dk * slope, dk);
    T += kp * u_pool.w;
    U += kp * (p * 2.2 + vec2(u_pool.z * 0.02, 0.0));
    S += kp * vec2(1.0, 0.0);
    wsum += kp;
    if (u_cap > 0.01) {
        float cs;
        float yc = capY(p.x, cs);
        float cd = yc - p.y;
        float c0 = -0.20, c1 = 0.18;
        float ct = clamp((cd - c0) / (c1 - c0), 0.0, 1.0);
        float kc = 1.4 * (1.0 - ct * ct * (3.0 - 2.0 * ct));
        float dc = (cd > c0 && cd < c1) ? -1.4 * 6.0 * ct * (1.0 - ct) / (c1 - c0) : 0.0;
        F += kc;
        G += vec2(dc * cs, -dc);
        T += kc * 0.12;
        U += kc * (p * 2.2 + 50.0);
        S += kc * vec2(1.0, 0.0);
        wsum += kc;
    }
    if (wsum > 1e-5) { T /= wsum; H /= wsum; U /= wsum; }
    S = normalize(S + vec2(0.0, 1e-4));
}

// Micro pits and craters on the wax skin (reference: the green lamp). One
// cell per scale, pits kept inside their cell, so no neighbour search.
vec3 pits(vec2 u, float sc) {
    vec2 g = u * sc;
    vec2 c = floor(g), f = fract(g);
    vec2 h = hash22(c + 11.0);
    vec2 ctr = 0.3 + 0.4 * h;
    float rr = 0.12 + 0.16 * hash12(c + 5.0);
    vec2 d = f - ctr;
    float x = length(d) / rr;
    // Sparse: one cell in nine. Denser pits tiled the pool with an even
    // speckle (operator photo IMG_8240).
    if (h.x > 0.89 || x >= 1.0) return vec3(0.0);
    // Bowl: the slope faces inward, the rim catches a little light.
    return vec3(d / rr * 0.9, (1.0 - x * x));
}

vec3 liquidAt(vec2 q, vec2 p, float t) {
    float g = glowAt(q);
    vec3 L = u_liquid;
    // The liquid carries the colour: saturated, glowing, hottest over the bulb.
    // (Kept under about 1.0 before tonemapping: brighter liquid lost its
    // saturation to the ACES shoulder and read as pastel.)
    vec3 col = L * (0.04 + 0.95 * g) + u_bulb * pow(g, 3.0) * 0.7;
    // Dark, cooler far side (purple-brown room tones creep in at the top).
    col += vec3(0.03, 0.008, 0.035) * (1.0 - g);
    // Convection currents: faint rising sheets of warmer, brighter liquid. The
    // plume pattern is seed-offset too (operator, 2026-10-05: the lamp's heat
    // plume looked the same for every seed), bounded so the lamp still reads.
    float cv = fbm(vec2(q.x * 2.6 + 0.4 * sin(q.y * 1.7 + t * 0.03), q.y * 0.9 - t * 0.035) + u_pattern.xy * 0.22,
                   u_quality > 0.5 ? 3 : 2);
    col *= 0.78 + 0.45 * cv;
    // Shafts of bulb light rising through the column.
    float sh = vnoise(vec2(q.x * 3.2 + q.y * 0.35, t * 0.012) + u_pattern.xy * 0.45);
    col += L * g * 0.5 * pow(sh, 4.0);
    // Mist lamps: a milky haze that scatters the bulb light.
    col = mix(col, (L * 0.6 + u_bulb * 0.4) * (0.15 + 0.8 * g), u_mist * 0.35);
    // Glitter: fine metal flecks suspended in the liquid, turning slowly so
    // each catches the light for a few seconds (well under 1 Hz). They ride
    // the convection in swirls, so the density is clumped, never an even
    // speckle.
    if (u_look2.y > 0.5 && u_look2.z > 0.0) {
        vec2 gq = q * 34.0 + vec2(0.0, -t * 0.08);
        vec2 c = floor(gq), f = fract(gq) - 0.5;
        float h = hash12(c + 7.0 + u_seed);
        float d = length(f - (hash22(c + 1.3) - 0.5) * 0.7);
        float swirl = smoothstep(0.3, 0.75, vnoise(q * 2.2 + vec2(0.3 * sin(q.y * 1.3 + t * 0.02), -t * 0.03)));
        float fl = step(1.0 - 0.16 * u_look2.z * (0.25 + 0.75 * swirl), h) * (1.0 - smoothstep(0.04, 0.11, d));
        float tw = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(t * 0.6 + h * 40.0), 6.0);
        vec3 gc = u_look2.y < 1.5 ? vec3(0.9, 0.92, 0.95) : (u_look2.y < 2.5 ? vec3(1.0, 0.78, 0.35)
                  : 0.6 + 0.4 * cos(6.2831 * (h * 7.0 + vec3(0.0, 0.33, 0.67))));
        col += gc * fl * tw * (0.25 + 1.2 * g);
    }
    return col;
}

// Tiny motes and micro bubbles drifting up with the convection.
float motes(vec2 q, float sc, float spd, float t, float sz, float dens) {
    vec2 g = q * sc + vec2(0.0, -t * spd);
    vec2 c = floor(g), f = fract(g) - 0.5;
    float h = hash12(c + u_seed);
    vec2 off = (hash22(c + 3.1) - 0.5) * 0.6;
    float d = length(f - off);
    return step(1.0 - dens, h) * (1.0 - smoothstep(sz * 0.5, sz, d));
}
// The few motes a real lamp has ride in the rising plumes: motes() gated by
// a slow, clumped density mask, so specks never tile the liquid evenly. The
// mask is only evaluated in the rare cells that pass the density draw.
float plumeMotes(vec2 q, float sc, float spd, float t, float sz, float dens) {
    vec2 g = q * sc + vec2(0.0, -t * spd);
    vec2 c = floor(g), f = fract(g) - 0.5;
    float h = hash12(c + u_seed);
    if (h < 1.0 - dens) return 0.0;
    float pl = smoothstep(0.42, 0.72, vnoise(vec2(q.x * 1.6 + 0.3 * sin(q.y * 1.1 + t * 0.02), q.y * 0.7 - t * 0.025)));
    if (h < 1.0 - dens * pl) return 0.0;
    vec2 off = (hash22(c + 3.1) - 0.5) * 0.6;
    float d = length(f - off);
    return 1.0 - smoothstep(sz * 0.5, sz, d);
}

// Wax over what lies behind it.
//
// Reference frames (docs/MAGMASIMPLEX-REALISM.md): lamp wax is a scattering
// body lit from below. Near the bulb it glows BRIGHTER than the liquid around
// it; its dense core is pale (the dye washed toward the bulb's white by
// multiple scattering), its thin edges are saturated and partly see-through,
// so the silhouette is soft and never outlined; there is no highlight on it,
// no rim, no sheen; the surface is smooth, pitted only where it has cooled.
//   behind: colour behind this layer; q: simulation position; light: bulb
//   light here; detail: texture strength for this layer (depth of field).
vec3 waxOver(vec3 behind, vec2 q, vec2 p, float cover, float F, vec2 G, float T, float H, vec2 U, vec2 S,
             float light, float t, float detail) {
    if (cover <= 0.001) return behind;
    vec3 W0 = mix(u_wax, u_secondary, clamp(H, 0.0, 1.0));
    float h = max(F - ISO, 0.0);
    float th = sqrt(h) * 1.3;                       // thickness proxy: 0 at the edge
    vec2 gh = G / (2.0 * sqrt(max(h, 0.003)));
    // A dome, with low-frequency lumps that travel with the wax.
    vec3 n = normalize(vec3(-gh * 0.25, 1.0));
    float lump = vnoise(U * 2.4);
    vec2 lg = vec2(vnoise(U * 2.4 + vec2(0.07, 0.0)) - lump, vnoise(U * 2.4 + vec2(0.0, 0.07)) - lump) / 0.07;
    n = normalize(n + vec3(-lg * 0.025, 0.0));
    float jelly = step(1.5, u_look.x), transl = step(0.5, u_look.x) * (1.0 - jelly);
    detail *= u_look.y * (1.0 - jelly);
    // Pits only in patches, and only on wax that has cooled (the references
    // show smooth bodies; a pitted skin where the wax sat cold). The earlier
    // second pit scale and the "cold wrinkle" stripes (which drew a comb of
    // hatching on the cap, operator photo IMG_8240) are gone.
    float cav = 0.0;
    if (detail > 0.2) {
        // ...and only on thick wax: pits on a thin strand drew it as a
        // stack of rings.
        float pm = smoothstep(0.55, 0.85, vnoise(U * 0.45 + 3.0)) * clamp(1.4 - 1.6 * T, 0.0, 1.0) *
                   smoothstep(0.25, 0.5, th);
        vec3 pt = pits(U, 12.0);
        n = normalize(n + vec3(pt.xy * 0.25 * detail * pm, 0.0));
        cav = pt.z * detail * pm * 0.35;
    }
    // Inside: slow milky clouds and faint striations along the stretch; a
    // brightness variation only, the colour stays the wax's own.
    float m = fbm(U * 1.3 + vec2(t * 0.004, 0.0) + 2.0 * vnoise(U * 0.7 - t * 0.003), u_quality > 0.5 ? 3 : 2);
    vec2 perp = vec2(-S.y, S.x);
    float stri = vnoise(vec2(dot(U, perp) * 14.0, dot(U, S) * 1.2));
    float cloud = 1.0 + (0.22 * smoothstep(0.3, 0.7, m) - 0.11 + (0.06 * stri - 0.03)) * clamp(u_look.y, 0.0, 1.5);

    // Light reaching the wax: the bulb below, plus light piped up through the
    // wax itself (a column stays lit along its length), so even the top glows.
    float lw = 0.26 + 0.70 * light;
    // Lit from below: the under side of each body is a little brighter.
    vec3 Lb = normalize(vec3(0.0, -0.75, 0.66));
    float ndl = max(dot(n, Lb) * 0.5 + 0.5, 0.0);
    // Dense core: the dye lit from inside, luminous but still its own hue
    // (a mix toward white made orange wax read as putty; an intensity above
    // 1 bleached it to cream in the tonemapper); thin edge: the dye.
    vec3 core = mix(sqrt(W0), u_bulb, 0.08 + 0.08 * transl);
    vec3 Wc = mix(W0 * 0.95, core, smoothstep(0.0, 0.5, th));
    // Shading (operator, 2026-10-05: flat fills did not read as wax):
    //  - the dome: lit from below, the under side brighter, the top dimmer;
    //  - thickness: the thin band inside the edge glows with transmitted
    //    bulb light, saturated (subsurface), the thick core is calmer;
    //  - rim falloff: the outermost band curves away and shows less light,
    //    a darker contour that is never a line;
    //  - a broad, soft sheen from the room above, milky, never a point.
    float thin = exp(-th * 2.5);                     // one exp: the glow band, squared the rim band
    vec3 edgeGlow = W0 * (0.30 + 0.80 * light) * thin * (0.45 + 0.55 * T) * (0.7 + 0.5 * transl);
    float rimDark = 1.0 - 0.22 * thin * thin;
    vec3 Ls = normalize(vec3(-0.3, 0.7, 0.65));
    float sh1 = max(dot(n, Ls), 0.0), sh2 = sh1 * sh1;
    float sheen = sh2 * sh2 * sh2 * 0.10 * (1.0 - 0.6 * transl);
    vec3 wax = (Wc * lw * (0.48 + 0.62 * ndl) * cloud * (1.0 - 0.35 * cav) + edgeGlow) * rimDark;
    wax += sheen * (0.3 + 0.7 * light) * mix(W0, u_bulb, 0.6);
    // Hot wax at the bulb glows warmer from within.
    wax += W0 * u_bulb * light * light * 0.4 * T * u_look2.w;
    // Cold wax (startup): dull, dense.
    wax *= mix(0.5, 1.0, u_look2.w);
    // The thin edge lets the liquid behind through: a soft, few-pixel
    // transition (Gaussian in thickness, so the body stays opaque inside),
    // never a dyed band or a line.
    float tk = th * mix(4.5, 3.0, transl);
    float Tr = exp(-tk * tk);
    vec3 body = mix(wax, behind, Tr);
    if (jelly > 0.5) {
        // The earlier candy look, opt-in only (material=jelly): glossy, clear,
        // with a point highlight. Never drawn by randomize.
        vec3 Ld = normalize(vec3(-0.45, 0.6, 0.65));
        float spec = pow(max(dot(n, normalize(Ld + vec3(0.0, 0.0, 1.0))), 0.0), 90.0) * 0.35;
        vec3 tr = exp(-((1.0 - W0) * 3.4 + 0.18) * th);
        body = behind * tr * 1.35 + W0 * light * (0.35 + 0.65 * exp(-1.6 * th)) + spec;
    }
    return mix(behind, body, cover);
}

// Light scattered from the wax into the liquid just around it: faint.
vec3 bleed(float F, float H, float light) {
    vec3 W0 = mix(u_wax, u_secondary, clamp(H, 0.0, 1.0));
    return W0 * (0.03 + 0.08 * light) * smoothstep(0.02, ISO, F) * (1.0 - smoothstep(ISO - 0.02, ISO, F));
}

vec3 renderInside(vec2 p, float t) {
    vec3 cb = u_cam[0];
    vec2 qb = layerMap(p, cb, 0.85);
    // Beat: a radial squeeze ripple from the dish centre.
    qb *= 1.0 - 0.02 * u_squeeze;
    // Heat shimmer low in the column.
    qb.x += 0.006 * sin(qb.y * 28.0 + t * 1.3) * clamp(-qb.y, 0.0, 1.0);
    vec3 col = liquidAt(qb, p, t);
    col += u_bulb * glowAt(qb) * 0.35 * plumeMotes(qb, 30.0, 0.5, t, 0.08, 0.025 * u_look.z);
    for (int l = 0; l < LAYERS; l++) {
        vec3 cam = u_cam[l];
        vec2 q = layerMap(p, cam, 1.0);
        q *= 1.0 - 0.02 * u_squeeze;
        vec2 warp = (vec2(vnoise(q * 1.7 + vec2(0.0, t * 0.05) + float(l) * 9.1),
                          vnoise(q * 1.7 + vec2(19.3, -t * 0.04) + float(l) * 4.7)) - 0.5) * 0.07;
        float F, T, H; vec2 G, U, S;
        field(l, q + warp, F, G, T, H, U, S);
        float blur = l == 2 ? 9.0 : (l == 0 ? 2.5 : 1.0);
        float fw = max(fwidth(F), 1e-4) * blur;
        float cover = smoothstep(ISO - fw, ISO + fw, F);
        float light = glowAt(q);
        col += bleed(F, H, light);
        col = waxOver(col, q, p, cover, F, G, T, H, U, S, light, t, l == 0 ? 0.3 : (l == 1 ? 1.0 : 0.6));
        // Aerial perspective: the liquid in front veils what is farther away.
        vec3 veil = u_liquid * (0.05 + 1.1 * light) + u_bulb * pow(light, 3.0) * 0.8;
        if (l == 0) col = mix(col, veil, clamp(0.42 * u_look2.x, 0.0, 0.8));
        if (l == 1) col = mix(col, veil, clamp(0.12 * u_look2.x, 0.0, 0.5));
        if (l == 1) col += u_bulb * light * 0.25 * plumeMotes(q, 18.0, 0.35, t, 0.10, 0.02 * u_look.z);
    }
    // A rare, out-of-focus bubble drifting past the eye: a soft disc, no ring
    // (the ring-outlined bubbles read as drawn circles, operator photo IMG_8237).
    vec2 qn = layerMap(p, u_cam[2], 1.0);
    vec2 g = qn * 5.0 + vec2(0.0, -t * 0.09);
    vec2 c = floor(g), f = fract(g) - 0.5;
    float hb = hash12(c + u_seed + 91.0);
    float d = length(f - (hash22(c + 8.0) - 0.5) * 0.5);
    float r = 0.08 + 0.06 * hash12(c + 2.0);
    float disc = 1.0 - smoothstep(r * 0.4, r, d);
    col += step(1.0 - 0.012 * u_look.z, hb) * u_bulb * 0.07 * disc * glowAt(qn);
    // Faint reflections of the curved glass wall toward the screen edges.
    float asp = u_res.x / u_res.y;
    float ex = abs(p.x) / asp;
    col += u_bulb * 0.05 * u_look.w * smoothstep(0.78, 0.97, ex) * (0.6 + 0.4 * vnoise(vec2(p.y * 2.0, 3.0)));
    // Beat: brightness breathing, an emitter glow pulse and a mid bloom.
    col *= 1.0 + 0.18 * u_breath;
    col += u_bulb * glowAt(qb) * 0.35 * u_glow;
    col += col * 0.20 * u_bloom;
    return col;
}

// The classic full-screen field: one layer with the pool and the cap.
vec3 renderField(vec2 p, float t) {
    vec3 cam = vec3(u_cam[0].x * 0.3, 0.0, 1.0);
    vec2 q = layerMap(p, cam, 1.0);
    q *= 1.0 - 0.02 * u_squeeze;      // beat: radial squeeze ripple
    vec3 col = liquidAt(q, p, t);
    col += u_bulb * glowAt(q) * 0.35 * plumeMotes(q, 26.0, 0.45, t, 0.08, 0.025 * u_look.z);
    vec2 warp = (vec2(vnoise(q * 1.7 + vec2(0.0, t * 0.05)), vnoise(q * 1.7 + vec2(19.3, -t * 0.04))) - 0.5) * 0.07;
    float F, T, H; vec2 G, U, S;
    field(0, q + warp, F, G, T, H, U, S);
    float fw = max(fwidth(F), 1e-4);
    float cover = smoothstep(ISO - fw, ISO + fw, F);
    float light = glowAt(q);
    col += bleed(F, H, light);
    col = waxOver(col, q, p, cover, F, G, T, H, U, S, light, t, 1.0);
    // Beat: brightness breathing, emitter glow and a mid bloom.
    col *= 1.0 + 0.18 * u_breath;
    col += u_bulb * glowAt(q) * 0.35 * u_glow;
    col += col * 0.20 * u_bloom;
    return col;
}

vec3 renderGlass(vec2 p, float t) {
    // Curved glass right at the eye.
    vec2 pd = p * (1.0 + 0.05 * dot(p, p));
    // Smudges and a fingerprint on the glass bend the view a little.
    float smudge = smoothstep(0.5, 0.85, vnoise(pd * 1.1 + 11.0 + u_seed));
    vec2 fc = vec2(0.7, -0.25) + 0.2 * vec2(sin(u_seed), cos(u_seed));
    float ridge = sin(length((pd - fc) * vec2(1.0, 1.25)) * 110.0 + 3.0 * vnoise(pd * 5.0));
    float print = (1.0 - smoothstep(0.18, 0.34, length((pd - fc) * vec2(1.0, 1.25)))) * 0.6;
    pd += vec2(ridge, vnoise(pd * 30.0) - 0.5) * 0.0025 * max(smudge, print);

    vec3 c0 = u_cam[0];
    vec2 qb = layerMap(pd, c0, 0.85);
    vec3 col = liquidAt(qb, p, t);
    // The deep lamp behind: soft, out of focus.
    {
        vec2 q = layerMap(pd, c0, 1.0);
        float F, T, H; vec2 G, U, S;
        field(0, q, F, G, T, H, U, S);
        float cover = smoothstep(ISO - 0.12, ISO + 0.18, F);
        float light = glowAt(q);
        col = waxOver(col, q, p, cover, F, G, T, H, U, S, light, t, 0.0);
        col = mix(col, u_liquid * (0.05 + 1.1 * light) + u_bulb * pow(light, 3.0) * 0.8, 0.35);
    }
    // Wax against the glass.
    vec3 c1 = u_cam[1];
    vec2 q = layerMap(pd, c1, 2.1);
    vec2 warp = (vec2(vnoise(q * 2.3 + vec2(0.0, t * 0.04)), vnoise(q * 2.3 + vec2(19.3, -t * 0.03))) - 0.5) * 0.05;
    float F, T, H; vec2 G, U, S;
    field(1, q + warp, F, G, T, H, U, S);
    float fw = max(fwidth(F), 1e-4);
    float light = glowAt(q);
    col += bleed(F, H, light) * 1.5;
    float body = smoothstep(ISO - 0.05, ISO + 0.10, F);       // curving away: soft
    col = waxOver(col, q, p, body, F, G, T, H, U, S, light, t, 0.5);
    float IC = ISO + 0.26;
    float contact = smoothstep(IC - fw, IC + fw, F);
    vec3 W0 = mix(u_wax, u_secondary, clamp(H, 0.0, 1.0));
    // Pressed flat on the glass: the same wax waxOver just shaded, only
    // flattened (no dome gradient), evenly lit and pitted, sharply in focus.
    // Any separate colour here drew a ring around the contact (fried eggs).
    vec3 pt = pits(U, 9.0);
    float m = fbm(U * 1.6 + 2.0 * vnoise(U * 0.8), 3);
    vec3 pressed = col * (0.96 + 0.10 * smoothstep(0.3, 0.7, m)) * (1.0 - 0.25 * pt.z);
    pressed += (u_bulb * 0.06) * light * max(dot(pt.xy, vec2(0.0, -1.0)), 0.0);
    col = mix(col, pressed, contact);
    // Contact line: a soft meniscus with a faint lip just inside it (kept
    // gentle: a dark line plus a bright line is an outline).
    float dpx = (F - IC) / fw;
    col *= 1.0 - 0.14 * exp(-dpx * dpx * 0.5);
    col += mix(W0, u_bulb, 0.5) * light * 0.25 * exp(-max(dpx - 1.2, 0.0) * 0.3) * step(1.2, dpx);
    // Thin films of wax wetting the glass just outside the contact.
    float film = smoothstep(IC - 0.10, IC - 0.10 + max(0.10 - 2.0 * fw, 1e-3), F) * (1.0 - contact);
    col = mix(col, col * mix(vec3(1.0), W0, 0.5) + W0 * light * 0.12, film * 0.6);
    // A few dust specks on the glass: in focus, catching the light.
    float sp = motes(pd + 3.0, 42.0, 0.0, 0.0, 0.06, 0.015);
    float sp2 = motes(pd + 9.0, 90.0, 0.0, 0.0, 0.10, 0.012);
    col = col * (1.0 - 0.35 * sp * u_look.w) + u_bulb * 0.06 * sp2 * u_look.w;
    // Broad soft reflection of the room on the glass, and the smudge sheen.
    float band = exp(-pow((p.x + 0.9 + 0.15 * p.y) * 2.2, 2.0));
    col += (vec3(0.03, 0.03, 0.035) * band + vec3(0.02) * (smudge + print * (0.5 + 0.5 * ridge))) * u_look.w;
    // Beat: breathing, a squeeze ripple on the near wax and an emitter glow.
    col *= 1.0 + 0.15 * u_breath;
    col += col * 0.15 * u_bloom;
    return col;
}

float sat(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    return mx > 0.001 ? (mx - mn) / mx : 0.0;
}

vec3 showInk(vec2 r, float t, int oct) {
    // Seed-dependent domain offset and warp flow: the ink field is different
    // every seed instead of a fixed marbling (the operator saw the same
    // layout at different palettes before this).
    r += u_pattern.xy;
    vec2 flow = u_warp.yz;
    vec2 w1 = vec2(fbm(r * 1.1 + vec2(0.0, t * 0.012) + flow, oct), fbm(r * 1.1 + vec2(5.2, -t * 0.010) + flow * 0.7, oct));
    vec2 w2 = vec2(fbm(r * 1.9 + 2.2 * u_warp.x * w1 + vec2(1.7, 9.2) + t * 0.015 + flow * 1.3, oct), fbm(r * 1.9 + 2.2 * u_warp.x * w1 + vec2(8.3, 2.8) + flow, oct));
    float d = fbm(r * 1.4 + 2.6 * w2, oct);
    // Feathered, dendritic edges where inks bleed into each other.
    float fe = (vnoise(r * 21.0 + w2 * 7.0) - 0.5) * 0.05 + (vnoise(r * 57.0 + w1 * 9.0) - 0.5) * 0.02;
    float v = d + fe;
    // Inks: the palette's own colours, pushed toward the era's hot orange,
    // magenta, cyan and yellow so every show still reads as a 1960s show.
    // Each ink is either the palette's own colour, when it is saturated, or
    // the era's hot ink. Never a blend: mixing a colour with its complement
    // (cyan with pink) gives grey, which read as a washed-out show.
    vec3 c0 = sat(u_liquid) > 0.5 ? mix(u_liquid * 0.22, vec3(0.10, 0.0, 0.30), 0.5) : vec3(0.10, 0.0, 0.30);  // deep ground, kept apart from c3
    vec3 c1 = sat(u_wax) > 0.5 ? u_wax : vec3(1.00, 0.38, 0.02);
    vec3 c2 = sat(u_secondary) > 0.5 ? u_secondary : vec3(1.00, 0.04, 0.50);
    vec3 c3 = sat(u_liquid) > 0.5 ? u_liquid : vec3(0.02, 0.75, 0.90);
    vec3 c4 = vec3(1.00, 0.88, 0.35);
    float e = 0.018;
    vec3 c = mix(c0, c1, smoothstep(0.46 - e, 0.46 + e, v));
    c = mix(c, c2, smoothstep(0.55 - e, 0.55 + e, v));
    c = mix(c, c3, smoothstep(0.64 - e, 0.64 + e, v) * (1.0 - smoothstep(0.71 - e, 0.71 + e, v)));
    c = mix(c, c4, smoothstep(0.78 - e, 0.78 + e, v));
    // Dark oily veins along the boundaries, richer ink in the pools.
    float vein = 0.0;
    for (int i = 0; i < 4; i++) {
        float th = i == 0 ? 0.46 : (i == 1 ? 0.55 : (i == 2 ? 0.64 : 0.78));
        vein += exp(-pow((v - th) / 0.008, 2.0));
    }
    c *= 1.0 - 0.45 * clamp(vein, 0.0, 1.0);
    c *= 0.75 + 0.5 * smoothstep(0.1, 0.9, length(w2 - 0.5) * 2.0);
    return c;
}

vec3 renderShow(vec2 p, float t) {
    // Slow rotation of the dishes and a slow heartbeat swell (0.8 Hz, 2 %).
    float beat = 0.5 + 0.5 * sin(t * 5.0);
    beat = beat * beat * beat * beat;
    vec2 r = rot(p, t * 0.012 + u_pattern.z) * u_pattern.w * (1.0 - 0.02 * beat);
    // Beat: the projected dishes breathe with the music, a squeeze ripple
    // pulls inward and the projector's throw pulses (bounded +/-4 %).
    r *= 1.0 - 0.02 * u_squeeze;
    r *= 1.0 + 0.5 * u_radius;
    if (u_kal > 1.5) {
        float sector = 2.0 * PI / u_kal;
        float a = atan(r.y, r.x);
        a = mod(a, sector);
        a = abs(a - 0.5 * sector);
        r = length(r) * vec2(cos(a), sin(a));
    }
    // Oil lenses: the simulated bodies, drifting through the ink.
    vec2 q = r * 0.55 + vec2(0.0, 0.05);
    float F, T, H; vec2 G, U, S;
    field(1, q, F, G, T, H, U, S);
    r *= 0.42;  // big, slow pools of ink, not confetti
    float fw = max(fwidth(F), 1e-4);
    float cover = smoothstep(ISO - fw * 1.5, ISO + fw * 1.5, F);
    vec2 refr = -G * 0.02 * cover;
    int oct = u_quality > 0.5 ? 4 : 3;
    vec3 col;
    if (u_quality > 0.5) {
        // Chromatic fringing through the oil: each channel bends differently.
        col.r = showInk(r + refr * 0.8, t, oct).r;
        col.g = showInk(r + refr * 1.0, t, oct).g;
        col.b = showInk(r + refr * 1.25, t, oct).b;
    } else {
        col = showInk(r + refr, t, oct);
        col += vec3(0.25, -0.05, 0.3) * cover * (1.0 - smoothstep(ISO, ISO + 0.2, F)) * 0.3;
    }
    // Oil edge: a dark rim with a bright inner ring.
    float e = (F - ISO) / fw;
    col *= 1.0 - 0.55 * exp(-e * e * 0.3);
    col += vec3(1.0, 0.9, 0.7) * 0.22 * exp(-max(e - 2.0, 0.0) * 0.3) * step(2.0, e);
    // Expanding rings from a drop of ink somewhere in the dish.
    vec2 rc = vec2(0.5 * sin(t * 0.021 + u_seed), 0.35 * cos(t * 0.017 + u_seed * 1.7));
    float rd = length(r - rc);
    col *= 1.0 + 0.10 * sin(rd * 16.0 - t * 0.35) * exp(-rd * 0.9);
    // Tiny air bubbles trapped between the plates.
    float bub = motes(r + 5.0, 26.0, 0.0, 0.0, 0.10, 0.05);
    float bub2 = motes(r + 1.0, 26.0, 0.0, 0.0, 0.07, 0.05);
    col = col * (1.0 - 0.6 * (bub - bub2)) + vec3(0.25) * bub2;
    // The projector: hot spot, soft circular throw.
    float rr = length(p * vec2(0.92, 1.0));
    // (v2 used a 35 % hot spot and a 1.25 gain: the inks hit the tonemapper
    // shoulder and bleached toward cream.)
    col *= (1.0 + 0.15 * exp(-rr * rr * 1.4)) * mix(1.0, 1.0 - smoothstep(0.85, 1.55, rr), step(0.5, u_mask));
    // Beat: brightness/saturation breathing, a bloom and an onset glow pulse.
    col *= 1.0 + 0.20 * u_breath;
    col += col * (0.12 * u_bloom + 0.25 * u_glow);
    return col * 0.9;
}

// ---- Flowerchild (view=butterfly): a 1960s overhead-projector liquid light show, taken from
// the operator's reference footage (docs/MAGMASIMPLEX-REALISM.md,
// "Butterfly"): full-saturation dye fields that bloom and merge, oil droplets
// with dark rims from tiny to large (the big ones ringed), lacy fingering
// where two dyes meet, a slow breathing of the colour phase, bloom from the
// projector and a little film softness. No symmetry unless kaleidoscope is
// set; nothing bright stays in one place (OLED).
float lacy(vec2 r) {
    // Cellular fingering: folded noise has ridges like dye cells.
    float a = abs(vnoise(r * 23.0) - 0.5) * 2.0;
    float b = abs(vnoise(r * 47.0 + 7.0) - 0.5) * 2.0;
    return (a * 0.7 + b * 0.3) - 0.5;
}
vec3 butterflyDye(float v, float t) {
    // Dyes: the palette's liquid is the field, the wax and the secondary are
    // the inks dropped into it, and a near-white (the footage's white
    // marbling) is the lightest dye; the deepest pools are darker liquid.
    vec3 c0 = u_liquid, c1 = u_wax, c2 = u_secondary;
    vec3 c3 = mix(vec3(1.0, 0.97, 0.9), u_liquid, 0.25);
    vec3 cd = u_liquid * 0.25;
    // The breathing: thresholds drift slowly, so bands swell and shrink.
    float b0 = 0.015 * sin(t * 0.37), b1 = 0.015 * sin(t * 0.29 + 2.0), b2 = 0.015 * sin(t * 0.23 + 4.0);
    float t0 = 0.30 + b0, t1 = 0.47 + b1, t2 = 0.58 + b2, t3 = 0.66 + b0, t4 = 0.76 + b1;
    float e = mix(0.012, 0.0025, u_poster);
    vec3 c = mix(cd, c0, smoothstep(t0 - e, t0 + e, v));
    c = mix(c, c1, smoothstep(t1 - e, t1 + e, v));
    c = mix(c, c2, smoothstep(t2 - e, t2 + e, v) * (1.0 - smoothstep(t3 - e, t3 + e, v)));
    c = mix(c, c0, smoothstep(t3 - e, t3 + e, v) * (1.0 - smoothstep(t4 - e, t4 + e, v)));
    c = mix(c, c3, smoothstep(t4 - e, t4 + e, v));
    // The meniscus where two dyes meet scatters the projector light: a
    // bright seam with a soft glow, the halo along every edge in the footage.
    float seam = exp(-pow((v - t0) / 0.012, 2.0)) + exp(-pow((v - t1) / 0.012, 2.0)) +
                 exp(-pow((v - t2) / 0.012, 2.0)) + exp(-pow((v - t3) / 0.012, 2.0)) +
                 exp(-pow((v - t4) / 0.012, 2.0));
    return c + (c * 0.6 + 0.25) * clamp(seam, 0.0, 1.0) * 0.35 * (1.0 - u_poster);
}
// One layer of oil droplets: returns inside coverage; rim (dark edge), lens
// (0 at the rim .. 1 at the centre) and band (concentric rings, big ones).
float droplets(vec2 r, float sc, float t, float dens, float rmin, float rmax, out float rimv, out float lensv,
               out float bandv) {
    vec2 g = r * sc;
    vec2 c = floor(g), f = fract(g) - 0.5;
    // The droplet layout (which cells carry a drop, where in the cell, how
    // big) is seeded through u_pattern.w as well as u_seed, so two seeds never
    // share a drop pattern.
    vec2 h = hash22(c + 13.0 + u_seed + u_pattern.w * 7.0);
    rimv = 0.0; lensv = 0.0; bandv = 0.0;
    if (h.x > dens) return 0.0;
    float R = mix(rmin, rmax, h.y * h.y) * (1.0 + 0.06 * sin(t * 0.33 + h.x * 20.0)) *
              (0.85 + 0.30 * fract(u_pattern.w * 0.6180339)) * (1.0 + 0.30 * u_burst);
    vec2 ctr = (hash22(c + 5.0 + u_pattern.w * 3.0) - 0.5) * max(1.0 - 2.0 * R, 0.0);
    vec2 dv = f - ctr;
    float d = length(dv) / R;
    if (d >= 1.0) return 0.0;
    // A dark meniscus, thicker on small drops, with the projector's glint
    // on the upper-left inside of the rim (the footage's droplets).
    float w = 0.70 - 0.10 * smoothstep(0.1, 0.4, R);
    rimv = smoothstep(w, 0.93, d) * (1.0 - smoothstep(0.93, 1.0, d));
    float glint = smoothstep(0.55, 0.85, d) * (1.0 - smoothstep(0.85, 0.95, d)) *
                  smoothstep(0.2, 0.9, dot(normalize(dv + 1e-5), vec2(-0.6, 0.8)));
    lensv = (1.0 - d * d) * (1.0 - 0.5 * rimv) + 0.8 * glint;
    bandv = R > 0.3 ? 0.5 + 0.5 * sin(d * 22.0 + h.x * 6.0) : 0.0;
    return 1.0 - smoothstep(0.93, 1.0, d);
}
// Optional second and third projectors over the dye (overlay=streaks|neon|
// both; the 1969 footage shows both): tall vertical colour streaks from a
// streaked slide, and neon-sign tubes (a bright core in a soft glow). Both
// drift slowly, so nothing bright stays in one place.
vec3 overlays(vec3 col, vec2 p, float t) {
    if (u_overlay < 0.5) return col;
    if (u_overlay < 1.5 || u_overlay > 2.5) {
        float x = p.x * 1.7 + t * 0.012;
        float s1 = vnoise(vec2(x * 6.0, 0.3)), s2 = vnoise(vec2(x * 13.0 + 7.0, 1.7));
        float s3 = vnoise(vec2(x * 3.0 + 3.0, p.y * 0.3 + t * 0.005));
        float str = smoothstep(0.55, 0.9, s1 * 0.6 + s2 * 0.4);
        vec3 sc = mix(u_wax, u_secondary, s3);
        sc = mix(sc, vec3(1.0, 0.95, 0.8), 0.3 * smoothstep(0.8, 1.0, s2));
        float vert = 0.8 + 0.2 * sin(p.y * 2.0 + t * 0.07);
        col = 1.0 - (1.0 - col) * (1.0 - sc * str * 0.55 * vert);   // screen
    }
    if (u_overlay > 1.5) {
        float glow = 0.0, core = 0.0;
        for (int i = 0; i < 3; i++) {
            float fi = float(i);
            vec2 c = vec2(0.6 * sin(t * 0.017 + fi * 2.1), 0.4 * cos(t * 0.013 + fi * 1.3));
            vec2 d = p - c;
            float dist = i == 1 ? abs(d.y - 0.12 * sin(d.x * 9.0 + t * 0.2 + fi))   // a wavy tube
                                : abs(length(d) - 0.28 - 0.06 * fi);                 // a looped tube
            core += exp(-dist * dist * 6000.0);
            glow += exp(-dist * 7.0) * 0.14;
        }
        vec3 nc = mix(u_secondary, vec3(1.0, 0.3, 0.6), 0.5);
        col += nc * glow + (nc * 0.3 + 0.7) * core * 0.9;
    }
    return col;
}

// Dendrites (Flowerchild): a cheap, deterministic, slowly evolving branching
// pattern used as the boundary between the two dye phases and as an ink tree.
// A ridged fbm in a polar warp makes fingers radiate from a drifting centre;
// a multi-scale Voronoi edge makes the fibrous vein network. Both are rooted
// in screen coordinates, with several roots across a wide display, and drift
// slowly so nothing is ever static (OLED).
float maskRadius() {
    if (u_mask_size > 0.001) return clamp(u_mask_size, 0.30, 1.00);
    return 0.92;   // the style's default: a large lit disc, most of the screen
}
float vedge(vec2 x) {
    vec2 n = floor(x), f = fract(x);
    float f1 = 9.0, f2 = 9.0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 d = g + hash22(n + g) - f;
            float dd = dot(d, d);
            if (dd < f1) { f2 = f1; f1 = dd; } else if (dd < f2) { f2 = dd; }
        }
    return sqrt(f2) - sqrt(f1);
}
float ridgedFbm(vec2 p, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) {
        if (i >= oct) break;
        float n = 1.0 - abs(2.0 * vnoise(p) - 1.0);
        s += a * n * n;
        p = rot(p, 0.7) * 2.03 + 5.1;
        a *= 0.5;
    }
    return s / (1.0 - exp2(-float(oct)));
}
// One ink tree radiating from r's origin: thin dark fingers, softened with
// radius so a tree reaches a little over one half-screen height.
float inkTree(vec2 r, float t, int oct) {
    float rad = length(r) + 0.05;
    float a = fastAtan2(r.y, r.x);
    vec2 q1 = vec2(a * 0.85, log(rad) * 2.3 - t * 0.025);
    vec2 q2 = vec2(a * 2.4 + t * 0.02, log(rad) * 4.4 - t * 0.045);
    float m = max(ridgedFbm(q1 * 1.35 + 11.0, oct), ridgedFbm(q2 * 1.15 + 3.0, oct) * 0.9);
    // A dense branch network plus a sharper core of main fingers.
    float v = clamp(smoothstep(0.60, 0.90, m) + 0.5 * smoothstep(0.80, 0.96, m), 0.0, 1.0);
    return v * smoothstep(1.80, 1.05, rad);
}
// One vein network: thin cell walls at two scales, broken into fibres.
float veinNet(vec2 r, float t, int oct, out float sel) {
    vec2 warped = r + 0.16 * vec2(fbm(r * 0.7 + t * 0.010, oct), fbm(r * 0.7 + 5.0 - t * 0.008, oct));
    float e1 = vedge(warped * 2.1 + t * 0.004);
    float e2 = vedge(warped * 4.6 + 7.0 - t * 0.006);
    float fib = ridgedFbm(warped * 3.0 + t * 0.008, oct);
    float v = max(1.0 - smoothstep(0.0, 0.070, e1), (1.0 - smoothstep(0.0, 0.038, e2)) * 0.75);
    sel = fbm(r * 0.55 + t * 0.012, 3);
    return v * (0.55 + 0.45 * fib);
}
// The layer: several ink trees across a wide display, or one vein network,
// plus sparse red droplets denser where the ink is.
void dendriteLayer(vec2 p, float t, int oct, out float ink, out float veins, out float sel, out float drop) {
    float asp = u_res.x / u_res.y;
    ink = 0.0; veins = 0.0; sel = 0.0; drop = 0.0;
    if (u_dendrites < 1.5) {
        for (int i = 0; i < 4; i++) {
            float fi = float(i);
            float xn = (fi - 1.5) * 0.95;
            if (abs(xn) <= asp * 0.98) {
                vec2 root = vec2(xn, 0.10 * sin(t * 0.021 + fi * 1.7));
                ink = max(ink, inkTree(p - root, t, oct));
            }
        }
    } else {
        veins = veinNet(p, t, oct, sel);
    }
    vec2 gg = p * 7.0;
    vec2 cell = floor(gg), f = fract(gg) - 0.5;
    float h = hash12(cell + 41.0 + floor(t * 0.03));
    if (h > 0.90) {
        vec2 ctr = (hash22(cell + 3.0) - 0.5) * 0.55;
        float rad = 0.08 + 0.09 * hash12(cell + 9.0);
        drop = 1.0 - smoothstep(rad * 0.5, rad, length(f - ctr));
    }
}

vec3 renderButterfly(vec2 p, float t) {
    // The projector's throw: a hot spot that wanders (OLED: nothing bright
    // stays put). projector-mask=dish (the 1969 Fillmore footage): a bright
    // dish floating in black, its size pulsing, the dye zoomed into it.
    float dish = step(1.5, u_mask), big = step(2.5, u_mask);   // disc: a larger, brighter, cooler dish
    // The projector's drift phase is seeded, so the hot spot and dish centre
    // are in a different place for every run.
    vec2 hs = vec2(0.35 * sin(t * 0.013 + u_warp.w), 0.25 * cos(t * 0.011 + u_warp.w * 1.7)) * mix(1.0, 0.6, dish) * mix(1.0, 0.35, big);
    float Rd = maskRadius() * (1.0 + 0.08 * sin(t * 0.21) + 0.04 * sin(t * 0.37 + 1.0));
    // Beat: a bounded dish-radius pulse (+/-4 %, from u_radius).
    Rd *= 1.0 + u_radius;
    float dm = length((p - hs) * vec2(0.92, 1.0)) / Rd;
    if (dish > 0.5 && dm > 1.15) return vec3(0.0);
    // The dish turns slowly and everything drifts.
    vec2 r = rot(p - hs * dish, t * 0.009 + u_pattern.z) + vec2(0.05 * sin(t * 0.021), 0.04 * cos(t * 0.017));
    r *= mix(1.0, mix(0.75, 0.9, big) / Rd, dish);
    r *= 1.0 - 0.02 * u_squeeze;   // beat: squeeze ripple from the dish centre
    if (u_kal > 1.5) {
        float sector = 2.0 * PI / u_kal;
        float a = atan(r.y, r.x);
        a = mod(a, sector);
        a = abs(a - 0.5 * sector);
        r = length(r) * vec2(cos(a), sin(a));
    }
    // The simulated bodies: the big oil discs drifting through the dye.
    vec2 q = r * 0.55 + vec2(0.0, 0.05);
    float F, T, H; vec2 G, U, S;
    field(1, q, F, G, T, H, U, S);
    float fw = max(fwidth(F), 1e-4);
    float cover = smoothstep(ISO - fw, ISO + fw, F);
    float e1 = (F - ISO) / fw;
    r *= 0.5;
    // Seed-dependent domain offset and warp flow: every seed lays the dye
    // bands out differently (the operator's repeat-layout complaint).
    r += u_pattern.xy;
    vec2 flow = u_warp.yz;
    int oct = u_quality > 0.5 ? 4 : 3;
    // The dye field: domain-warped, slow.
    vec2 w1 = vec2(fbm(r * 0.9 + vec2(0.0, t * 0.010) + flow, oct), fbm(r * 0.9 + vec2(5.2, -t * 0.008) + flow * 0.7, oct));
    vec2 w2 = vec2(fbm(r * 1.6 + 2.0 * u_warp.x * w1 + vec2(1.7, 9.2) + t * 0.012 + flow * 1.3, oct), fbm(r * 1.6 + 2.0 * u_warp.x * w1 + vec2(8.3, 2.8) + flow, oct));
    float v = fbm(r * 1.2 + 2.4 * w2, oct);
    v += 0.05 * lacy(r + w2 * 0.5);                                   // fingering where dyes meet
    v += 0.06 * cover * (1.0 - smoothstep(ISO, ISO + 0.25, F)) - 0.03 * cover; // oil lenses bend the bands
    v += 0.02 * u_beat * (1.0 - u_poster);                                     // beat: the dye bands breathe
    float rim1, lens1, band1, rim2, lens2, band2;
    float d1 = droplets(r + vec2(0.0, -t * 0.004), 5.0, t, 0.55, 0.08, 0.42, rim1, lens1, band1);
    float d2 = droplets(r + vec2(3.1, -t * 0.007), 13.0, t, 0.45, 0.12, 0.45, rim2, lens2, band2);
    // Each drop is a lens: it shifts the dye bands inside it (its interior
    // reads differently from its surroundings) and brightens toward its
    // centre and its glint.
    v += 0.11 * (d1 * lens1 + d2 * lens2);
    vec3 col = butterflyDye(v, t);
    col *= mix(0.88 + 0.24 * w2.x, 1.0, u_poster);                      // gradients inside each band (none in poster)
    col *= 1.0 + 0.18 * (d1 * lens1 + d2 * lens2) * (1.0 - 0.7 * u_poster);
    // Dark rims around the droplets and the big discs, rings on the big ones.
    col *= 1.0 - 0.65 * max(rim1, rim2);
    col *= 1.0 - 0.15 * d1 * band1;
    col *= 1.0 - 0.5 * exp(-e1 * e1 * 0.25);
    col += vec3(1.0, 0.95, 0.85) * 0.12 * exp(-max(e1 - 2.0, 0.0) * 0.3) * step(2.0, e1);
    // Breathing, the throw and bloom: bright dye glows into its neighbours.
    float breath = 1.0 + 0.05 * sin(t * 0.35) + 0.03 * sin(t * 0.21 + 1.0);
    float rr = length((p - hs) * vec2(0.92, 1.0));
    float soft = step(0.5, u_mask) * (1.0 - dish);
    col *= breath * (1.0 + 0.18 * exp(-rr * rr * 1.2)) * mix(1.0, 1.0 - smoothstep(0.95, 1.7, rr), soft);
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col += col * 0.35 * smoothstep(0.45, 1.0, lum) * (1.0 - u_poster);
    col = overlays(col, p, t);
    if (dish > 0.5) {
        // White-hot transmitted light inside the dish, a soft glowing rim,
        // black beyond it.
        col *= 1.0 + mix(0.25, 0.4, big) * (1.0 - dm * dm);
        float rim = exp(-pow((dm - 1.0) / 0.06, 2.0));
        col = col * (1.0 - smoothstep(0.96, 1.02, dm)) + mix(vec3(1.0, 0.9, 0.75), vec3(0.8, 0.92, 1.0), big) * 0.35 * rim;
    }
    // Dendrites (Flowerchild). Disabled on the weak GPU tier (u_quality 0):
    // the presets that use the layer are min_class mid.
    float dstr = u_dendrite * step(0.5, u_quality);
    if (u_dendrites > 0.5 && dstr > 0.001) {
        float ink, veins, sel, drop;
        dendriteLayer(p, t, oct, ink, veins, sel, drop);
        if (u_dendrites < 1.5) {
            // Ink tree: dark fingers over the bright dish, red droplets along
            // the branches.
            col = mix(col, vec3(0.015, 0.012, 0.02), clamp(dstr * ink * 0.97, 0.0, 1.0));
            col += vec3(1.0, 0.06, 0.05) * drop * dstr * (0.35 + 0.65 * ink);
        } else {
            // Vein network: teal, yellow and pink fibres where the dyes meet.
            vec3 vcol = sel < 0.34 ? vec3(0.0, 0.85, 0.80)
                      : sel < 0.67 ? vec3(1.0, 0.88, 0.18)
                                   : vec3(1.0, 0.24, 0.62);
            col = mix(col, vcol, clamp(dstr * veins * 0.9, 0.0, 1.0));
        }
    }
    // Beat: the dye breathes and blooms; the droplets burst in droplets().
    col *= 1.0 + 0.18 * u_breath;
    col += col * 0.20 * u_bloom;
    return col;
}

vec3 renderMode(int m, vec2 p, float t) {
    if (m == 1) return renderGlass(p, t);
    if (m == 2) return renderShow(p, t);
    if (m == 3) return renderField(p, t);
    if (m == 4) return renderButterfly(p, t);
    return renderInside(p, t);
}

vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

void main() {
    vec2 p = (2.0 * gl_FragCoord.xy - u_res) / u_res.y;
    float t = u_time;
    vec3 col = renderMode(u_modeA, p, t);
    if (u_mmix > 0.001) col = mix(col, renderMode(u_modeB, p, t), u_mmix);
    // A strong, soft vignette: the dark room and the curve of the vessel. In
    // the light shows it is the projector mask, optional (projector-mask).
    float show = ((u_modeA == 2 || u_modeA == 4) ? 1.0 - u_mmix : 0.0) + ((u_modeB == 2 || u_modeB == 4) ? u_mmix : 0.0);
    float asp = u_res.x / u_res.y;
    vec2 v = vec2(p.x / asp, p.y);
    float vig = 1.0 - 0.55 * smoothstep(0.35, 1.25, length(v * vec2(1.0, 0.8)));
    // off: no vignette in a light show; dish: the butterfly draws its own
    // black; soft: the vignette is the projector's surround.
    float bfly = (u_modeA == 4 ? 1.0 - u_mmix : 0.0) + (u_modeB == 4 ? u_mmix : 0.0);
    float noVig = show * (u_mask < 0.5 ? 1.0 : (u_mask > 1.5 ? bfly : 0.0));
    col *= mix(vig, 1.0, noVig);
    col = aces(col * u_exposure);
    col = pow(col, vec3(1.0 / 2.2));
    // Film grain in the light shows, dither elsewhere.
    col += (hash12(gl_FragCoord.xy + fract(t * 7.13) * 91.0) - 0.5) * (1.0 / 255.0 + 0.035 * show);
    fragColor = vec4(col, 1.0);
}
