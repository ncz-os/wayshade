// SPDX-License-Identifier: Apache-2.0
// ephemeris.glsl -- every pass of the Ephemeris screensaver in one file.
//
// The C driver (src/gles3_ephemeris.c) compiles this file several times,
// each time with a different PASS_* define prepended after the #version
// line. One file keeps the whole piece readable in one sitting.
//
// What the piece is, stated plainly for whoever reads this next:
//
// A surface that lasts the length of one run, and workers that last a few
// seconds each. A worker is born with nothing from the worker that held its
// slot before it: no position, no heading, no intent. What it has is a
// 32x32 summary of the surface, read back from the GPU a moment ago and
// already out of date. It draws by continuing whatever structure it can make
// out in that summary. It cannot see the evaluator in this file, which checks
// the real surface at full resolution and quietly declines strokes that do not
// join what is actually there. A declined stroke leaves nothing. The worker
// never learns of it.
//
// The surface forgets slowly. Nothing survives the end of the run. The next
// run starts from a different surface that only looks as if it has a past.
//
// Matte material: charcoal, limestone, a little iron oxide. Nothing in this
// file emits light, and nothing flashes.

precision highp float;
precision highp int;
precision highp sampler2D;

// ---------------------------------------------------------------- shared
uint eph_hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float eph_h1(uvec3 v) {
    return float(eph_hash(v.x ^ eph_hash(v.y ^ eph_hash(v.z)))) * (1.0 / 4294967296.0);
}
float eph_vnoise(vec2 p, uint salt) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    uvec2 ii = uvec2(ivec2(i) + 32768);
    float a = eph_h1(uvec3(ii, salt));
    float b = eph_h1(uvec3(ii + uvec2(1u, 0u), salt));
    float c = eph_h1(uvec3(ii + uvec2(0u, 1u), salt));
    float d = eph_h1(uvec3(ii + uvec2(1u, 1u), salt));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float eph_fbm(vec2 p, uint salt) {
    float s = 0.0, a = 0.5;
    for (int k = 0; k < 4; k++) {
        s += a * eph_vnoise(p, salt + uint(k) * 101u);
        p = p * 2.03 + vec2(17.1, 5.3);
        a *= 0.5;
    }
    return s;
}
// Paper tooth: the fine grain a dry medium catches on. Scaled in surface
// pixels relative to a 1080-line reference so it reads the same at any size.
float eph_tooth(vec2 px, float ref_scale, uint salt) {
    vec2 q = px * ref_scale;
    return 0.6 * eph_vnoise(q * 0.9, salt) + 0.4 * eph_vnoise(q * 2.3, salt + 7u);
}

// ---------------------------------------------------------------- vertex: full screen
#ifdef PASS_VS_FULL
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
#endif

// ---------------------------------------------------------------- init: a surface with an apparent past
#ifdef PASS_INIT
uniform vec2 u_size;          // surface size in pixels
uniform vec4 u_mass[4];       // x, y, radius, angle (radians)
uniform int  u_nmass;
uniform uint u_salt;
uniform float u_loose;        // 0 = masses, 1 = continuous drawing
uniform float u_cap;          // darkest mark in the apparent past (dramatic 0.35: faint, but across the plate)
uniform float u_plate;        // 1: dramatic, the past reaches every part of the plate
out vec4 o_col;
// A thin line along the zero set of f, about w pixels wide.
float eph_iso(float f, float w) {
    float g = max(fwidth(f), 1e-5);
    return 1.0 - smoothstep(0.5 * w, 0.5 * w + 1.0, abs(f) / g);
}
void main() {
    vec2 uv = gl_FragCoord.xy / u_size;
    float asp = u_size.x / u_size.y;
    vec2 pa = uv * vec2(asp, 1.0);
    float ref = 1080.0 / u_size.y;
    float tooth = eph_tooth(gl_FragCoord.xy, ref, u_salt);
    float wash = 0.0, lines = 0.0;
    for (int k = 0; k < 4; k++) {
        if (k >= u_nmass) break;
        vec4 m = u_mass[k];
        vec2 d = (uv - m.xy) * vec2(asp, 1.0);
        float warp = eph_fbm(pa * 2.5 + float(k) * 7.0, u_salt + 11u);
        float r = m.z * (0.7 + 0.6 * warp);
        float fall = 1.0 - smoothstep(r * 0.3, r, length(d));
        fall *= mix(1.0, 0.5, u_loose);
        wash = max(wash, fall * (0.22 + 0.25 * eph_fbm(pa * 6.0 + float(k), u_salt + 31u)));
        // Hatching whose direction wanders with the mass.
        float ang = m.w + 1.2 * (eph_fbm(pa * 1.7 + float(k) * 3.0, u_salt + 47u) - 0.5);
        vec2 dir = vec2(cos(ang), sin(ang));
        // Zero crossings of sin every 1/80 of the height: about 13 px at 1080 lines.
        float hatch = eph_iso(sin(dot(pa, vec2(-dir.y, dir.x)) * 6.2831853 * 40.0), 1.1);
        float gate = smoothstep(0.52, 0.7, eph_fbm(pa * 4.0 + float(k) * 5.0, u_salt + 61u));
        lines = max(lines, fall * gate * hatch * 0.75);
    }
    // Older contour lines across the plate, belonging to no mass.
    float f = eph_fbm(pa * 2.2, u_salt + 57u) - 0.5;
    float contour = eph_iso(f, 1.4) * smoothstep(mix(0.35, 0.15, u_plate), mix(0.6, 0.45, u_plate), eph_fbm(pa * 3.0, u_salt + 3u));
    lines = max(lines, contour * mix(0.35, 0.6, u_loose));
    if (u_plate > 0.5) {
        // A second, fainter family of old contours and a thin worked ground
        // everywhere, so no part of the plate starts empty.
        float f2 = eph_fbm(pa * 3.1 + 17.0, u_salt + 71u) - 0.5;
        lines = max(lines, eph_iso(f2, 1.1) * 0.3 * smoothstep(0.25, 0.55, eph_fbm(pa * 2.4 + 5.0, u_salt + 73u)));
        wash = max(wash, 0.16 * smoothstep(0.3, 0.75, eph_fbm(pa * 1.4 + 9.0, u_salt + 79u)));
    }
    float catchw = smoothstep(0.3, 0.8, tooth);
    float catchl = smoothstep(0.25, 0.6, tooth);
    float c = 1.0 - (1.0 - wash * catchw) * (1.0 - lines * mix(0.4, 1.0, catchl));
    // Iron oxide, sparingly: one or two small washes.
    float ox = eph_fbm(pa * 1.6, u_salt + 91u);
    float o = smoothstep(0.70, 0.78, ox) * 0.4 * catchw;
    o_col = vec4(clamp(min(c, u_cap), 0.0, 1.0), clamp(o, 0.0, 1.0), 0.0, 0.0);
}
#endif

// ---------------------------------------------------------------- evaluator
// One output texel per worker slot. It reads the real surface around the head
// of the stroke, at full resolution, and compares that with what the worker
// believed was there. Workers never read this output; only the deposit pass
// does. Some rejections are simply lost time: nothing is drawn in their place.
#ifdef PASS_EVAL
uniform sampler2D u_surface;
uniform vec2 u_size;
uniform vec4 u_seg[48];       // p0.xy, p1.xy in uv
uniform vec4 u_segx[48];      // density the worker saw (its blurred view), weight, kind, half-width (uv of height)
uniform float u_tol;
uniform float u_outage;       // probability that a slot is simply not accepted for a while
uniform uint u_salt;
uniform uint u_epoch;         // coarse clock for outages
uniform uint u_frame;
uniform vec2 u_full;          // (density, probability): ground this full refuses most new work
out vec4 o_col;
float dens(vec2 uv) {
    vec4 s = textureLod(u_surface, clamp(uv, vec2(0.0), vec2(1.0)), 0.0);
    return clamp(s.r + s.g, 0.0, 1.0);
}
void main() {
    int i = min(int(gl_FragCoord.x), 47);   // the target is exactly 48x1
    vec4 seg = u_seg[i];
    vec4 x = u_segx[i];
    if (x.y <= 0.0) { o_col = vec4(0.0, 0.0, 0.0, 1.0); return; }
    float asp = u_size.x / u_size.y;
    vec2 d = (seg.zw - seg.xy) * vec2(asp, 1.0);
    float l = length(d);
    vec2 fw = l > 1e-6 ? d / l : vec2(1.0, 0.0);
    vec2 sd = vec2(-fw.y, fw.x);
    float r = max(x.w * 4.0, 2.0 / u_size.y);
    vec2 toUv = vec2(1.0 / asp, 1.0);
    // Ahead and to the sides only: the worker's own fresh line is behind it.
    float a = dens(seg.zw + fw * r * toUv) * 0.4
            + dens(seg.zw + sd * r * toUv) * 0.2
            + dens(seg.zw - sd * r * toUv) * 0.2
            + dens(seg.zw + (fw + sd) * 0.7 * r * toUv) * 0.1
            + dens(seg.zw + (fw - sd) * 0.7 * r * toUv) * 0.1;
    float miss = abs(a - x.x);
    float roll = eph_h1(uvec3(uint(i), u_frame / 20u, u_salt));
    bool reject = miss > u_tol && roll < 0.85;
    if (a > u_full.x && eph_h1(uvec3(uint(i) + 311u, u_frame / 20u, u_salt)) < u_full.y) reject = true;
    // A slot that is out of favour for this epoch. The cost is time.
    if (eph_h1(uvec3(uint(i) + 977u, u_epoch, u_salt)) < u_outage) reject = true;
    o_col = vec4(reject ? 0.0 : 1.0, 0.0, 0.0, 1.0);
}
#endif

// ---------------------------------------------------------------- deposit (instanced, one quad per worker)
#ifdef PASS_VS_DEPOSIT
layout(location = 0) in vec2 a_pos;   // unused; keeps attribute 0 enabled on strict drivers
uniform vec4 u_seg[48];
uniform vec4 u_segx[48];
uniform vec2 u_size;
uniform sampler2D u_verdict;
out vec2 v_local;       // along, across (in height-uv)
flat out float v_len;
flat out float v_rad;
flat out float v_weight;
flat out float v_kind;
void main() {
    int i = min(gl_InstanceID, 47);          // at most 48 instances are drawn
    vec4 seg = u_seg[i];
    vec4 x = u_segx[i];
    float ok = texelFetch(u_verdict, ivec2(i, 0), 0).r;
    float asp = u_size.x / u_size.y;
    vec2 a = seg.xy * vec2(asp, 1.0), b = seg.zw * vec2(asp, 1.0);
    vec2 d = b - a;
    float l = length(d);
    if (ok < 0.5 || x.y <= 0.0 || l > 0.05) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_local = vec2(0.0); v_len = 0.0; v_rad = 0.0; v_weight = 0.0; v_kind = 0.0;
        return;
    }
    vec2 fw = l > 1e-7 ? d / l : vec2(1.0, 0.0);
    vec2 sd = vec2(-fw.y, fw.x);
    float r = x.w + 1.5 / u_size.y;
    int vid = gl_VertexID & 3;
    float along = (vid & 1) == 0 ? -r : l + r;
    float across = (vid >> 1) == 0 ? -r : r;
    vec2 p = a + fw * along + sd * across;
    vec2 uv = p / vec2(asp, 1.0);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    v_local = vec2(along, across);
    v_len = l; v_rad = x.w; v_weight = x.y; v_kind = x.z;
}
#endif

#ifdef PASS_DEPOSIT
uniform vec2 u_size;
uniform float u_flow;
uniform uint u_salt;
uniform float u_now;
uniform int u_nlifts;
uniform vec4 u_lifts[8];      // x, y, radius, start time of recent lifts
// Ground that was lifted takes marks badly for a long time (about 15 min).
float eph_clog(vec2 uv) {
    float asp = u_size.x / u_size.y, c = 0.0;
    for (int k = 0; k < 8; k++) {
        if (k >= u_nlifts) break;
        vec4 L = u_lifts[k];
        float d = length((uv - L.xy) * vec2(asp, 1.0));
        float inside = 1.0 - smoothstep(0.85 * L.z, L.z, d);
        c = max(c, inside * exp(-max(u_now - L.w, 0.0) / 900.0));
    }
    return clamp(c, 0.0, 1.0);
}
in vec2 v_local;
flat in float v_len;
flat in float v_rad;
flat in float v_weight;
flat in float v_kind;
out vec4 o_col;
void main() {
    float t = clamp(v_local.x, 0.0, v_len);
    float d = length(vec2(v_local.x - t, v_local.y));
    float aa = 1.0 / u_size.y;
    float tooth = eph_tooth(gl_FragCoord.xy, 1080.0 / u_size.y, u_salt);
    float p;
    // v_kind: 0 charcoal line, 1 iron oxide line, 2 charcoal wash, 3 iron oxide wash.
    bool oxide = v_kind > 0.5 && (v_kind < 1.5 || v_kind > 2.5);
    float clog = u_nlifts > 0 ? eph_clog(gl_FragCoord.xy / u_size) : 0.0;
    if (v_kind > 1.5) {
        // Wash: broad and soft, and grainy because only the tooth takes it.
        float cov = 1.0 - smoothstep(0.2 * v_rad, v_rad, d);
        float catchp = smoothstep(0.35, 0.8, tooth);
        p = cov * v_weight * u_flow * 0.14 * mix(0.3, 1.0, catchp);
    } else {
        float cov = 1.0 - smoothstep(v_rad - aa, v_rad + aa, d);
        // Dry medium on a toothed plate: light pressure only catches the peaks.
        float catchp = smoothstep(0.62 - 0.35 * v_weight, 0.9 - 0.2 * v_weight, tooth);
        p = cov * v_weight * u_flow * mix(0.25, 1.0, catchp);
    }
    p *= 1.0 - 0.7 * clog;
    // Saturating blend in the driver: dst + src * (1 - dst). Charcoal lines
    // and washes are the same pigment (red); iron oxide is green. Blue marks
    // freshness for every kind.
    o_col = oxide ? vec4(0.0, p, p, 0.0) : vec4(p, 0.0, p, 0.0);
}
#endif

// ---------------------------------------------------------------- weathering (reverse-subtract blend)
#ifdef PASS_DECAY
uniform vec3 u_rate;     // probability per frame of losing one 8-bit step: charcoal, oxide, freshness
uniform uint u_frame;
uniform uint u_salt;
out vec4 o_col;
void main() {
    uvec2 q = uvec2(gl_FragCoord.xy);
    float h1 = eph_h1(uvec3(q, u_frame * 3u + u_salt));
    float h2 = eph_h1(uvec3(q.yx, u_frame * 3u + 1u + u_salt));
    float h3 = eph_h1(uvec3(q + 7u, u_frame * 3u + 2u + u_salt));
    const float step8 = 1.0 / 255.0;
    o_col = vec4(h1 < u_rate.x ? step8 : 0.0,
                 h2 < u_rate.y ? step8 : 0.0,
                 h3 < u_rate.z ? step8 : 0.0, 0.0);
}
#endif

// ---------------------------------------------------------------- lift (multiplicative blend)
// One of eight steps: inside a mass-sized region, charcoal is multiplied
// toward a quarter of what it was (unevenly) and iron oxide toward half. The
// result is a dull, uneven smear; nothing is restored and nothing is outlined.
#ifdef PASS_LIFT
uniform vec2 u_size;
uniform vec4 u_lift;          // x, y, radius, active
uniform uint u_salt;
out vec4 o_col;
void main() {
    vec2 uv = gl_FragCoord.xy / u_size;
    float asp = u_size.x / u_size.y;
    vec2 d = (uv - u_lift.xy) * vec2(asp, 1.0);
    float edge = u_lift.z * (0.85 + 0.3 * eph_fbm(d * 6.0 + vec2(u_lift.x * asp, u_lift.y) * 13.0, u_salt + 401u));
    float k = u_lift.w * (1.0 - smoothstep(0.8 * edge, edge, length(d)));
    float target = 0.25 + 0.2 * (eph_fbm(uv * 9.0, u_salt + 409u) - 0.5);
    float fc = pow(clamp(target, 0.05, 1.0), 1.0 / 8.0);
    float fo = pow(0.5, 1.0 / 8.0);
    o_col = vec4(mix(1.0, fc, k), mix(1.0, fo, k), 1.0, 1.0);
}
#endif

// ---------------------------------------------------------------- display
#ifdef PASS_DISPLAY
uniform sampler2D u_surface;
uniform vec2 u_res;       // render size in pixels (from the harness)
uniform float u_tone;     // paper lightness
uniform float u_dram;     // 1 for the dramatic composition
uniform uint u_salt;
out vec4 o_col;
void main() {
    vec2 uv = gl_FragCoord.xy / u_res;
    vec4 s = textureLod(u_surface, uv, 0.0);
    float ref = 1080.0 / u_res.y;
    float tooth = eph_tooth(gl_FragCoord.xy, ref, u_salt);
    float plate = eph_fbm(uv * vec2(u_res.x / u_res.y, 1.0) * 1.3, u_salt + 201u);
    // Limestone: a warm grey, mottled a little, never lit from within.
    vec3 paper = vec3(1.0, 0.975, 0.93) * u_tone;
    paper *= 0.93 + 0.05 * plate + 0.04 * tooth;
    vec3 charcoal = mix(vec3(0.070, 0.068, 0.066), vec3(0.055, 0.054, 0.053), u_dram);
    vec3 oxide = mix(vec3(0.50, 0.23, 0.13) * (0.6 + 0.8 * u_tone), vec3(0.60, 0.19, 0.10), u_dram);
    float c = s.r * (0.9 + 0.1 * s.b);
    vec3 col = mix(paper, charcoal, clamp(c * 0.94, 0.0, 1.0));
    col = mix(col, oxide * mix(1.0, 0.55, s.r), clamp(s.g * mix(0.8, 0.85, u_dram), 0.0, 1.0));
    float dither = eph_h1(uvec3(uvec2(gl_FragCoord.xy), u_salt + 9u)) - 0.5;
    o_col = vec4(col + dither / 255.0, 1.0);
}
#endif
