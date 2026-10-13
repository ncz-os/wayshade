// SPDX-License-Identifier: Apache-2.0
// megacity.glsl - one program for megacity_gles3. The driver prepends
// "#version 300 es" and "#define VS 1" for the vertex stage.
//
// Stateless: every input is a uniform computed by the driver as a pure function
// of (seed, t). The city is a lattice of cells hashed from the seed, walked with
// a 2D DDA; ships are raymarched hulls; small lane traffic is projected points.
// Everything is original geometry: a lattice of blocks with boulevard rims,
// mega-structures (slab arcology, terraced mass, spire cluster with sky bridges).
#ifdef VS
layout(location = 0) in vec2 a_pos;
out vec2 v_q;
void main() { v_q = a_pos; gl_Position = vec4(a_pos, 0.0, 1.0); }
#else
precision highp float;
precision highp int;
in vec2 v_q;
out vec4 o_col;

uniform vec2  u_res;
uniform vec3  u_org;        // camera in the local frame: x,z inside cell (0,0), y altitude
uniform ivec2 u_cell;       // world cell index of local cell (0,0)
uniform vec3  u_fwd, u_right, u_up;
uniform vec3  u_lens;       // tanHalfY, shiftX, shiftY (NDC)
uniform int   u_seed;
uniform float u_e;          // sun elevation parameter: -1 night ... +1 pale-reach
uniform vec3  u_sundir;
uniform float u_time;       // scene seconds
uniform vec4  u_wx0;        // rain snow sleet fog
uniform vec4  u_wx1;        // smog dust ash cloud
uniform vec4  u_wx2;        // wet wind storm -
uniform float u_flash;      // display-space luminance delta of the current flash (0..0.2)
uniform vec2  u_bolt;       // azimuth of the strike, noise seed
uniform vec4  u_look;       // exposure haze wet height
uniform vec4  u_look2;      // mega shipSaturation - -
uniform int   u_pal;        // 0 neon 1 violet 2 refinery 3 mono
uniform int   u_quality;    // 0 weak/low, 1 normal
uniform int   u_style;      // ship style: 1 paperback 0 clean
uniform vec4  u_ship_a[5];  // pos relative to camera, length (0 = none)
uniform vec4  u_ship_b[5];  // yaw, seed, kind, fade
uniform vec4  u_craft[24];  // pos relative to camera, dir*fade (0 = none)

const float CS = 64.0;
const float HMAX = 2100.0;
const float PI = 3.14159265;

// ---- hashing / noise ------------------------------------------------------
uint ih(uint a, uint b, uint c) {
    uint h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
float uf(uint h) { return float(h & 0xffffffu) / 16777216.0; }
float hash21(vec2 p) { p = fract(p * vec2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return fract((p.x + p.y) * p.x); }
float hash31(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }
vec2 hash22(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.xx + p3.yz) * p3.zy); }
float vn(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x), mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p, int oct) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++) { if (i >= oct) break; s += a * vn(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return s;
}

// ---- global state for the frame ------------------------------------------
float gL;          // pale-reach amount 0..1
float gN;          // night amount (1.0 - gL)
float gK, gKu, gHf; // haze density, floor haze, scale height
float gCloud;
vec3  gNeon0, gNeon1, gNeon2, gNeon3, gGlow;

vec3 palGlowNight() {
    /* night air glow: each palette gets a deep, low-chroma base so the city
     * is the source of light, not the sky. neon is a dark navy-violet, the
     * "rain-soaked noir" tint. violet is cool indigo. refinery is warm
     * sodium haze. mono is steel-blue. */
    if (u_pal == 1) return vec3(0.018, 0.022, 0.060);
    if (u_pal == 2) return vec3(0.046, 0.034, 0.022);
    if (u_pal == 3) return vec3(0.020, 0.030, 0.052);
    return vec3(0.022, 0.028, 0.080);
}
void setPalette() {
    if (u_pal == 1)      { gNeon0 = vec3(0.55, 0.20, 1.00); gNeon1 = vec3(0.20, 0.35, 1.00); gNeon2 = vec3(1.00, 0.30, 0.80); gNeon3 = vec3(0.40, 0.90, 1.00); }
    else if (u_pal == 2) { gNeon0 = vec3(1.00, 0.55, 0.12); gNeon1 = vec3(0.10, 0.75, 0.70); gNeon2 = vec3(1.00, 0.35, 0.05); gNeon3 = vec3(0.90, 0.90, 0.80); }
    else if (u_pal == 3) { gNeon0 = vec3(0.90, 0.95, 1.00); gNeon1 = vec3(0.60, 0.70, 0.90); gNeon2 = vec3(1.00, 1.00, 1.00); gNeon3 = vec3(0.70, 0.80, 1.00); }
    else                 { gNeon0 = vec3(1.00, 0.15, 0.55); gNeon1 = vec3(0.45, 0.25, 1.00); gNeon2 = vec3(0.35, 0.85, 1.00); gNeon3 = vec3(0.60, 1.00, 0.30); }
}
vec3 neonPick(float r) {
    r = fract(r);
    return r < 0.34 ? gNeon0 : (r < 0.62 ? gNeon1 : (r < 0.88 ? gNeon2 : gNeon3));
}

// ---- city -------------------------------------------------------------------
struct Bld { float h1, w1, h2, w2, b0, b1, wb, id, mega; };

float district(ivec2 bk) { return vn(vec2(bk) * 0.37 + float(u_seed & 255) * 1.7); }

bool clusterPresent(ivec2 c) {
    return uf(ih(uint(c.x), uint(c.y), uint(u_seed) + 17u)) > 0.28;
}

Bld cellBld(ivec2 c) {
    Bld r; r.h1 = 0.0; r.w1 = 0.0; r.h2 = 0.0; r.w2 = 0.0; r.b0 = 0.0; r.b1 = 0.0; r.wb = 0.0; r.id = 0.0; r.mega = 0.0;
    ivec2 bk = ivec2(floor(vec2(c) / 8.0));
    ivec2 l = c - bk * 8;
    if (l.x == 0 || l.x == 7 || l.y == 0 || l.y == 7) return r;   // boulevard rim
    uint su = uint(u_seed);
    uint hb = ih(uint(bk.x), uint(bk.y), su);
    float ub = uf(hb);
    float d = district(bk);
    uint hc = ih(uint(c.x), uint(c.y), su + 17u);
    float r0 = float(hc & 0xffffu) / 65535.0, r1 = float(hc >> 16) / 65535.0;
    r.id = uf(hc >> 3);
    float hs = u_look.w;
    if (ub < u_look2.x * (0.09 + 0.24 * d)) {
        // mega block
        uint hm = ih(uint(bk.x), uint(bk.y), su + 91u);
        float m0 = uf(hm), m1 = float((hm >> 24) & 255u) / 255.0;
        int mk = int(m0 * 3.0);
        bool sw = ((hm >> 3) & 1u) == 1u;
        ivec2 lm = sw ? l.yx : l;
        r.mega = 1.0;
        r.id = m0;
        float pod = (95.0 + 90.0 * r0) * hs;
        if (mk == 0) {                      // slab arcology
            float H = (880.0 + 640.0 * m1) * hs;
            if (lm.x >= 2 && lm.x <= 5 && lm.y >= 3 && lm.y <= 4) {
                r.h1 = H; r.w1 = 31.0;
                if (lm.y == 3 && lm.x >= 3 && lm.x <= 4) { r.h2 = H * 1.10; r.w2 = 15.0; }
            } else { r.h1 = pod; r.w1 = 29.0; }
        } else if (mk == 1) {               // terraced mass, asymmetric (never a pyramid)
            float H = (800.0 + 500.0 * m1) * hs;
            if (lm.y >= 2 && lm.y <= 5) {
                float s = floor(float(lm.x - 1) / 5.0 * 5.99) / 5.0;
                r.h1 = H * (0.16 + 0.84 * s) * (0.92 + 0.16 * r0); r.w1 = 31.0;
                if (lm.x == 6) { r.h2 = r.h1 * 1.12; r.w2 = 16.0; }
            } else { r.h1 = pod * 0.6; r.w1 = 28.0; }
        } else {                            // spire cluster with sky bridges
            float H = (1000.0 + 600.0 * m1) * hs;
            if (lm.x >= 2 && lm.x <= 5 && lm.y >= 2 && lm.y <= 5) {
                if (r0 > 0.28) {
                    r.h1 = H * (0.45 + 0.55 * r1); r.w1 = 32.0 * (0.76 + 0.20 * r1);
                    if (r0 > 0.6) { r.h2 = r.h1 * 1.15; r.w2 = r.w1 * 0.55; }
                } else if (lm.x > 2 && lm.x < 5) {
                    ivec2 dx = sw ? ivec2(0, 1) : ivec2(1, 0);
                    if (clusterPresent(c - dx) && clusterPresent(c + dx)) { r.b0 = 0.58 * H; r.b1 = r.b0 + 18.0; r.wb = 31.6; }
                }
            } else { r.h1 = pod * 0.8; r.w1 = 29.0; }
        }
        return r;
    }
    float hmaxc = 110.0 + 340.0 * d * d;
    r.h1 = (42.0 + (hmaxc - 42.0) * pow(r0, 1.7)) * hs;
    if (r1 > 0.93) r.h1 *= 1.6;
    r.w1 = 32.0 * (0.60 + 0.28 * r1);
    float r2 = uf(hc >> 5);
    r.h2 = r.h1;
    if (r2 > 0.5) { r.h2 = r.h1 * (1.12 + 0.5 * r0); r.w2 = r.w1 * (0.5 + 0.25 * r1); }
    return r;
}

// trace results
float gT; vec3 gNrm; float gId, gTop, gMega; int gKind; int gPart;

bool hitBox(vec3 ro, vec3 rd, vec3 rdi, vec3 bmin, vec3 bmax, out float tn, out vec3 nrm) {
    vec3 t0 = (bmin - ro) * rdi, t1 = (bmax - ro) * rdi;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float a = max(max(tmin.x, tmin.y), tmin.z), b = min(min(tmax.x, tmax.y), tmax.z);
    if (b < a || a < 0.0) return false;
    tn = a;
    nrm = -sign(rd) * step(tmin.yzx, tmin.xyz) * step(tmin.zxy, tmin.xyz);
    return true;
}

void trace(vec3 ro, vec3 rd, float tmax) {
    gKind = 0; gT = tmax; gPart = 0; gTop = 0.0; gId = 0.0; gMega = 0.0; gNrm = vec3(0, 1, 0);
    vec3 rdi = 1.0 / rd;
    float tg = rd.y < -1e-4 ? -ro.y / rd.y : 1e9;
    float tlim = min(tg, tmax);
    ivec2 c = ivec2(floor(ro.xz / CS));
    ivec2 st = ivec2(rd.x > 0.0 ? 1 : -1, rd.z > 0.0 ? 1 : -1);
    vec2 nb = vec2(c) * CS + vec2(rd.x > 0.0 ? CS : 0.0, rd.z > 0.0 ? CS : 0.0);
    vec2 tM = (nb - ro.xz) * rdi.xz;
    vec2 tD = abs(CS * rdi.xz);
    float tcur = 0.0;
    int steps = u_quality > 0 ? 160 : 96;
    for (int i = 0; i < 160; i++) {
        if (i >= steps) break;
        if (rd.y > 0.0 && ro.y + rd.y * tcur > HMAX) break;
        if (tcur >= tlim) break;
        Bld b = cellBld(c + u_cell);
        if (b.h1 > 0.0 || b.b1 > 0.0) {
            vec2 cc = (vec2(c) + 0.5) * CS;
            float tn, best = 1e9; vec3 nr, bn = vec3(0); int part = 0; float top = 0.0;
            if (b.h1 > 0.0) {
                if (hitBox(ro, rd, rdi, vec3(cc.x - b.w1, 0.0, cc.y - b.w1), vec3(cc.x + b.w1, b.h1, cc.y + b.w1), tn, nr) && tn < best) { best = tn; bn = nr; part = 0; top = b.h1; }
                if (b.h2 > b.h1 && hitBox(ro, rd, rdi, vec3(cc.x - b.w2, b.h1, cc.y - b.w2), vec3(cc.x + b.w2, b.h2, cc.y + b.w2), tn, nr) && tn < best) { best = tn; bn = nr; part = 1; top = b.h2; }
            }
            if (b.b1 > 0.0 && hitBox(ro, rd, rdi, vec3(cc.x - b.wb, b.b0, cc.y - b.wb), vec3(cc.x + b.wb, b.b1, cc.y + b.wb), tn, nr) && tn < best) { best = tn; bn = nr; part = 2; top = b.b1; }
            if (best < 1e8 && best < tlim) {
                gKind = 1; gT = best; gNrm = bn; gId = b.id; gTop = top; gMega = b.mega; gPart = part;
                return;
            }
        }
        if (tM.x < tM.y) { tcur = tM.x; tM.x += tD.x; c.x += st.x; } else { tcur = tM.y; tM.y += tD.y; c.y += st.y; }
    }
    if (tg < tmax) { gKind = 2; gT = tg; gNrm = vec3(0, 1, 0); }
}

// ---- fog / haze ------------------------------------------------------------
float odFog(float y0, float dy, float t) {
    float a = dy / gHf;
    float base = gK * exp(-max(y0, 0.0) / gHf);
    float od = abs(a) < 1e-4 ? base * t : base * (1.0 - exp(clamp(-a * t, -30.0, 30.0))) / a;
    return od + gKu * t;
}
float odSky(float y0, float dy) {
    float a = max(dy / gHf, 1e-4);
    return min(gK * exp(-max(y0, 0.0) / gHf) / a, 30.0) + gKu * 6000.0;
}

vec3 horizonCol(vec3 rd) {
    vec3 hn = palGlowNight();
    vec3 hd = vec3(0.86, 0.90, 0.94);
    vec3 h = mix(hn, hd, gL);
    float tw = 4.0 * gL * gN;
    vec3 sd = normalize(vec3(u_sundir.x, 0.0, u_sundir.z) + 1e-4);
    float az = max(dot(normalize(vec3(rd.x, 0.0, rd.z) + 1e-4), sd), 0.0);
    h += vec3(1.0, 0.52, 0.46) * tw * 0.42 * pow(az, 3.0);
    return h;
}
vec3 fogColor(vec3 rd) {
    vec3 fc = horizonCol(rd);
    float snow = u_wx0.y, fogw = u_wx0.w, smog = u_wx1.x, dust = u_wx1.y, ash = u_wx1.z;
    float ws = fogw + smog + dust + ash + snow * 0.5;
    if (ws > 0.001) {
        vec3 nightGlow = palGlowNight() * 1.6;
        vec3 tFog = mix(nightGlow, vec3(0.74, 0.77, 0.81), gL);
        /* in rain, the haze picks up a slight warm neon undertone so the
         * light pollution from the buildings below reads back through it. */
        tFog += vec3(0.10, 0.04, 0.18) * (1.0 - gL) * u_wx0.x;
        vec3 tSmog = mix(vec3(0.18, 0.10, 0.16), vec3(0.62, 0.55, 0.38), gL);
        vec3 tDust = mix(vec3(0.16, 0.11, 0.09), vec3(0.72, 0.58, 0.38), gL);
        vec3 tAsh = mix(vec3(0.10, 0.09, 0.11), vec3(0.42, 0.42, 0.44), gL);
        vec3 tSnow = mix(vec3(0.16, 0.15, 0.24), vec3(0.85, 0.88, 0.94), gL);
        vec3 t = (tFog * fogw + tSmog * smog + tDust * dust + tAsh * ash + tSnow * snow * 0.5) / ws;
        fc = mix(fc, t, clamp(ws, 0.0, 1.0) * 0.85);
    }
    return fc;
}

// ---- sky ---------------------------------------------------------------------
vec3 skyBase(vec3 rd) {
    float y = max(rd.y, 0.0);
    /* zenith: a deep navy-violet over the noir city, a clear pale blue in
     * pale-reach. The blue-channel boost is small in pale-reach so the haze stays
     * near-white. */
    vec3 zen = mix(vec3(0.006, 0.012, 0.042), vec3(0.36, 0.52, 0.80), gL);
    vec3 c = mix(horizonCol(rd), zen, pow(y, mix(0.45, 0.55, gL)));
    float cl = gCloud;
    // sun
    float sdot = max(dot(rd, u_sundir), 0.0);
    c += vec3(1.0, 0.95, 0.85) * (pow(sdot, 600.0) * 8.0 + pow(sdot, 12.0) * 0.35) * smoothstep(-0.05, 0.1, u_sundir.y) * (1.0 - cl * 0.85);
    if (rd.y > 0.01) {
        vec2 p = rd.xz / (rd.y + 0.08) * 900.0 + vec2(u_time * 6.0 * (0.5 + u_wx2.y), u_time * 2.0);
        float f = fbm(p * 0.0012, u_quality > 0 ? 5 : 3);
        float th = 1.0 - cl * 0.85 - 0.06;
        float dens = smoothstep(th, th + 0.28, f);
        vec3 cday = mix(vec3(0.96), vec3(0.40, 0.43, 0.48), cl) * (0.55 + 0.45 * gL);
        /* night clouds keep the same hue family as the sky but never go
         * bright enough to read as pink — the noir tint carries the colour,
         * the lit windows carry the warmth. */
        vec3 cnight = palGlowNight() * (0.9 + 0.6 * cl) + vec3(0.003, 0.005, 0.012);
        vec3 ccol = mix(cnight, cday, gL);
        c = mix(c, ccol, dens * smoothstep(0.0, 0.22, rd.y) * (0.22 + 0.78 * cl));
    }
    return c;
}

// ---- shading of the city ----------------------------------------------------
vec3 ambient(vec3 n) {
    vec3 sa = mix(vec3(0.020, 0.025, 0.060), vec3(0.50, 0.58, 0.70), gL);
    vec3 ga = mix(palGlowNight() * 0.55, vec3(0.44, 0.43, 0.41), gL);
    return mix(ga, sa, 0.5 + 0.5 * n.y);
}
float sunAmount() { return smoothstep(-0.02, 0.35, u_sundir.y) * (1.0 - 0.75 * gCloud); }

float winCov(vec2 f, vec2 aa, vec4 rect) {
    vec2 a = smoothstep(rect.xy - aa, rect.xy + aa, f) - smoothstep(rect.zw - aa, rect.zw + aa, f);
    return a.x * a.y;
}

vec3 shadeBuilding(vec3 p, vec3 n, vec3 rd, float t, float pxa) {
    float id = gId;
    float pxw = max(t * pxa, 0.02);
    float wetK = u_wx2.x * u_look.z;
    // albedo
    vec3 pastel = id < 0.33 ? vec3(0.80, 0.78, 0.72) : (id < 0.66 ? vec3(0.72, 0.77, 0.82) : vec3(0.82, 0.74, 0.76));
    vec3 alb = mix(vec3(0.085, 0.09, 0.115) * (0.8 + 0.4 * fract(id * 13.0)), pastel, gL);
    vec3 col;
    vec3 sun = vec3(1.0, 0.96, 0.88) * (3.0 * sunAmount());
    float nd = max(dot(n, u_sundir), 0.0);
    vec3 light = ambient(n) + sun * nd;
    float ao = mix(0.45, 1.0, smoothstep(0.0, 140.0, p.y)) * (n.y > 0.5 ? 1.0 : 0.92);
    // facade
    vec3 emis = vec3(0.0);
    float along = abs(n.x) > 0.5 ? p.z : p.x;
    float glass = 0.0;
    if (n.y < 0.5) {
        float style = floor(fract(id * 7.31) * 4.0);
        /* Window pitch: a few small cells per floor (paperback-NOON/clear
         * look) or sparse tall verticals (noir night look). The gL-based
         * pitch and the rect narrower keep the dark navy body of every
         * tower legible against a foreground of pinprick neon, instead of
         * letting the windows bleach the upper third into candy synthwave. */
        vec2 pitch = style == 3.0 ? vec2(7.2, 8.4)
                     : style == 2.0 ? vec2(2.4, 4.2)
                     : mix(vec2(2.4, 4.2), vec2(4.2, 5.4), gN);
        vec4 rectBase = style == 1.0 ? vec4(0.0, 0.22, 1.0, 0.72)
                      : style == 2.0 ? vec4(0.28, 0.06, 0.72, 0.94)
                      : vec4(0.15, 0.20, 0.85, 0.75);
        /* In dark mode, tighten every window slot to a narrow vertical slit so
         * the body reads as dark navy. In pale-reach, keep the wide paperback
         * blocks. The mix factor is gN so day = wide, night = narrow. */
        vec4 rect = vec4(
            mix(rectBase.x + 0.05, rectBase.x, gL),  /* left edge */
            rectBase.y,                              /* top edge */
            mix(rectBase.z - 0.10, rectBase.z, gL),  /* right edge */
            rectBase.w);                             /* bottom edge */
        vec2 uvw = vec2(along, p.y);
        vec2 g = uvw / pitch, f = fract(g), cellid = floor(g);
        vec2 aa = clamp(vec2(pxw) / pitch, 0.0, 1.0);
        float cov = winCov(f, aa, rect);
        float mean = (rect.z - rect.x) * (rect.w - rect.y);
        cov = mix(cov, mean, smoothstep(0.25, 0.9, max(aa.x, aa.y)));
        float hl = hash31(vec3(cellid, id * 91.0));
        /* Lit-window fraction drops with gN so dark mode is mostly dark body
         * with pinprick neon windows, not a sea of bright pastel. */
        float litFrac = mix(0.10, 0.42, gL);
        float lit = step(1.0 - litFrac, hl);
        vec3 wc = hl < 0.5 ? vec3(1.0, 0.74, 0.42) : (hl < 0.8 ? vec3(0.55, 0.78, 1.0) : neonPick(hl * 5.0));
        /* Window luminance: in dark mode cap at 0.50 so even a fully lit
         * window does not bleach the wall (a noir tower reads as dark navy
         * with pinprick neon accents, not as a bright pastel slab). */
        float lum = mix(0.50, 0.95, gL) * gN * (0.55 + 0.45 * gN);
        emis += wc * lit * cov * lum * (0.7 + 0.3 * fract(id * 5.0));
        glass = cov;
        // glass reads darker and bluer by day and mirrors the sky
        vec3 gcol = mix(alb, vec3(0.30, 0.42, 0.55) * (0.6 + 0.4 * gCloud), gL * 0.75);
        alb = mix(alb, gcol, cov * 0.8);
        // neon strips and crown bands
        // The strip is a thin vertical neon accent. It used to be drawn for
        // ~45% of buildings and cover ~7% of every cycle, which meant the
        // bright emission wrapped much of a tall tower's facade and the body
        // stopped reading as dark navy. Both numbers are tightened so the
        // strip is a small accent on a minority of buildings, and the
        // emission amplitude is gated by gN^2 so it cannot bleach the body
        // even at peak. The crown band stays — it is the building's top
        // rim and reads as a thin colored line, never a wide ribbon.
        float nr = fract(id * 17.0);
        vec3 nc = neonPick(fract(id * 29.0));
        if (nr > 0.78) {
            float per = 14.0 + 12.0 * fract(id * 41.0);
            float s = smoothstep(0.014 + pxw / per, 0.003, abs(fract(along / per + id * 7.0) - 0.5) - 0.495);
            s *= step(p.y, gTop * 0.85) * step(8.0, p.y);
            emis += nc * s * (0.55 * gN * gN + 0.08) * (0.7 + 0.3 * sin(u_time * 0.31 + id * 40.0));
            alb = mix(alb, nc * 0.45, s * gL * 0.4);
        }
        if (nr > 0.45 && gTop - p.y < 2.0 && gTop > 60.0) {
            float b = smoothstep(2.0, 0.8, gTop - p.y);
            emis += nc * b * (0.75 * gN * gN + 0.10);
            alb = mix(alb, nc * 0.5, b * gL * 0.45);
        }
        // street glow thrown up the base of the tower
        float base = smoothstep(150.0, 0.0, p.y);
        vec3 sg = neonPick(fract(id * 53.0) + 0.17);
        emis += sg * base * 0.07 * gN * (0.35 + 0.65 * wetK);
        // vertical fins on some faces
        if (style == 2.0) alb *= 0.9 + 0.1 * smoothstep(0.3, 0.7, fract(along / 2.4));
    } else {
        // roof: plant boxes, faint grid
        float rg = smoothstep(0.02, 0.06, abs(fract(along / 9.0) - 0.5)) * 0.15 + 0.85;
        alb *= rg;
        float rb = smoothstep(4.0, 1.0, abs(p.y - gTop));
        emis += neonPick(id * 7.0) * step(0.9, fract(id * 23.0)) * 0.25 * gN;
    }
    // aircraft beacons on the tallest structures: slow pulse, never a flash
    if (gTop > 420.0 && gTop - p.y < 3.0 && n.y > 0.5) {
        float pu = 0.5 + 0.5 * sin(u_time * 1.2 + id * 30.0);
        emis += vec3(1.0, 0.1, 0.08) * pu * 0.8 * (0.3 + 0.7 * gN);
    }
    col = alb * light * ao + emis;
    // wet gloss: reflects the sky and the neon glow at grazing angles
    float fres = pow(1.0 - max(dot(n, -rd), 0.0), 4.0);
    vec3 refl = mix(horizonCol(reflect(rd, n)) * 1.5, skyBase(reflect(rd, n)), 0.5);
    col += refl * fres * (0.04 + 0.16 * wetK) * (0.4 + 0.6 * glass) * mix(1.0, 0.7, gL);
    return col;
}

vec3 shadeGround(vec3 p, vec3 rd, float t) {
    vec2 fl = floor(p.xz / CS);
    vec2 f = p.xz - fl * CS;
    ivec2 ic = ivec2(fl) + u_cell;
    float e = min(min(f.x, CS - f.x), min(f.y, CS - f.y));
    float wetK = u_wx2.x * u_look.z;
    vec3 alb = mix(vec3(0.028, 0.028, 0.034), vec3(0.50, 0.50, 0.49), gL);
    float puddle = smoothstep(0.38, 0.62, fbm(p.xz * 0.045 + 3.0, 3));
    float wm = clamp(wetK * (0.55 + 0.45 * puddle), 0.0, 1.0);
    // lane markings along the boulevards (rim cells)
    ivec2 bk = ivec2(floor(vec2(ic) / 8.0));
    ivec2 l = ic - bk * 8;
    float mark = 0.0;
    if (l.x == 0 || l.x == 7) {
        float dash = step(0.5, fract(p.z / 24.0));
        mark = dash * (1.0 - smoothstep(0.5, 1.4, abs(l.x == 0 ? f.x : f.x - CS)));
    }
    alb = mix(alb, mix(vec3(0.5, 0.5, 0.45), vec3(0.9), gL), mark * 0.8);
    vec3 light = ambient(vec3(0, 1, 0)) + vec3(1.0, 0.96, 0.88) * 3.0 * sunAmount() * u_sundir.y;
    vec3 col = alb * light;
    // street lamps
    vec2 lp = vec2(fract(p.x / 32.0) - 0.5, fract(p.z / 32.0) - 0.5) * 32.0;
    float lamp = smoothstep(2.4, 0.4, length(lp)) * step(e, 14.0);
    col += vec3(1.0, 0.80, 0.55) * lamp * 0.9 * gN;
    // neon spill from the buildings onto the street, doubled in the puddles.
    // The brief asks for a rain-soaked neon noir city, which means the spill is
    // a soft glow along the rims of the boulevard, NOT a saturated colour
    // field that turns the wet ground into a sheet of pastel. Cap the spill
    // at 0.08 in dark mode and at 0.20 in pale-reach; gate by distance from
    // buildings so the wet boulevard stays mostly dark between rims.
    vec3 nc = neonPick(vn(vec2(ic) * 0.21 + 3.0) * 1.7);
    float glow = exp(-abs(e - 9.0) * 0.12) * (0.5 + 0.5 * vn(p.xz * 0.15));
    float spill = mix(0.08, 0.20, gL);
    col += nc * glow * (0.01 + spill * wm) * gN;
    // reflection of the sky glow at grazing angles
    float fres = pow(1.0 - max(-rd.y, 0.0), 3.0);
    col += horizonCol(reflect(rd, vec3(0, 1, 0))) * 1.2 * wm * fres * mix(1.0, 0.5, gL);
    return col;
}

// ---- ships ---------------------------------------------------------------------
int gMat;
float sdBox(vec3 p, vec3 b) { vec3 q = abs(p) - b; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }
float sdCap(vec3 p, float h, float r) { p.x -= clamp(p.x, -h, h); return length(p) - r; }
float sdCylX(vec3 p, float h, float r) { vec2 d = abs(vec2(length(p.yz), p.x)) - vec2(r, h); return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }
float sdCylZ(vec3 p, float h, float r) { vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(r, h); return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }
float smin(float a, float b, float k) { float h = max(k - abs(a - b), 0.0) / k; return min(a, b) - h * h * k * 0.25; }
float part(float d, float e, int m) { if (e < d) { gMat = m; return e; } return d; }

float hullSDF(vec3 p, int kind, float sd) {
    float d = 1e3; gMat = 0;
    if (kind == 0) {                       // lozenge hauler with a row of cargo pods
        vec3 q = p; q.y *= 1.35;
        d = sdCap(q, 0.30, 0.075 + 0.012 * sd);
        vec3 pp = p - vec3(0.0, 0.088, 0.0);
        float cx = clamp(floor(pp.x / 0.12 + 0.5), -2.0, 2.0);
        pp.x -= cx * 0.12;
        d = part(d, sdBox(pp, vec3(0.046, 0.030 + 0.012 * fract(sd * 7.0 + cx), 0.05)), 1);
        d = part(d, sdBox(p - vec3(-0.35, 0.0, 0.0), vec3(0.06, 0.08, 0.11)), 3);
        vec3 en = p - vec3(-0.43, 0.0, 0.0); en.z = abs(en.z) - 0.05;
        d = part(d, sdCylX(en, 0.03, 0.032), 2);
    } else if (kind == 1) {                // spine hauler with hung tanks
        d = sdBox(p, vec3(0.46, 0.018, 0.018));
        vec3 q = p - vec3(0.0, -0.035, 0.0);
        float cx = clamp(floor(q.x / 0.17 + 0.5), -2.0, 2.0);
        q.x -= cx * 0.17;
        d = part(d, max(length(q.xy) - 0.062, abs(q.z) - 0.11) - 0.008, 1);
        d = part(d, sdBox(p - vec3(0.42, 0.012, 0.0), vec3(0.05, 0.035, 0.04)), 3);
        d = part(d, sdCylX(p - vec3(-0.47, 0.0, 0.0), 0.03, 0.03), 2);
    } else if (kind == 2) {                // flat wedge plate
        float tp = mix(0.32, 1.0, clamp(p.x + 0.5, 0.0, 1.0));
        d = sdBox(vec3(p.x, p.y, p.z / tp), vec3(0.5, 0.028, 0.16)) * tp;
        d = part(d, sdBox(p - vec3(-0.08, 0.05, 0.0), vec3(0.06, 0.03, 0.05)), 1);
        d = part(d, sdBox(p - vec3(0.14, 0.04, 0.0), vec3(0.035, 0.02, 0.03)), 1);
        vec3 en = p - vec3(-0.5, 0.0, 0.0); en.z = abs(en.z) - 0.07;
        d = part(d, sdCylX(en, 0.03, 0.03), 2);
    } else if (kind == 3) {                // twin hull with cross beams
        vec3 q = p; q.z = abs(q.z) - 0.07;
        d = sdCap(q, 0.31, 0.046);
        vec3 b = p; float bx = clamp(floor(b.x / 0.2 + 0.5), -1.0, 1.0); b.x -= bx * 0.2;
        d = part(d, sdBox(b, vec3(0.018, 0.014, 0.09)), 1);
        d = part(d, length(p - vec3(0.34, 0.03, 0.0)) - 0.05, 3);
        vec3 en = q - vec3(-0.36, 0.0, 0.0);
        d = part(d, sdCylX(en, 0.03, 0.03), 2);
    } else {                               // capital: tiered hull with a dorsal ridge
        vec3 q = p; q.y *= 1.6;
        d = sdCap(q, 0.36, 0.085);
        d = smin(d, sdBox(p - vec3(0.36, -0.005, 0.0), vec3(0.08, 0.03, 0.07)), 0.03);
        vec3 r = p - vec3(0.0, 0.085, 0.0);
        float cx = clamp(floor(r.x / 0.085 + 0.5), -4.0, 4.0);
        r.x -= cx * 0.085;
        float hh = 0.03 + 0.012 * (4.0 - cx) * 0.6 + 0.01 * fract(sd * 3.0 + cx);
        d = part(d, sdBox(r - vec3(0.0, hh * 0.5, 0.0), vec3(0.030, hh, 0.042)), 1);
        vec3 s = p; s.z = abs(s.z) - 0.105;
        d = part(d, sdBox(s - vec3(-0.05, 0.0, 0.0), vec3(0.16, 0.03, 0.04)), 3);
        vec3 en = p - vec3(-0.40, 0.0, 0.0);
        en.z = abs(en.z + 0.0) - 0.05; en.y = en.y * 1.0;
        float ez = clamp(round(p.z / 0.04), -2.0, 2.0);
        vec3 eb = p - vec3(-0.42, 0.0, ez * 0.04);
        d = part(d, sdCylX(eb, 0.03, 0.02), 2);
    }
    return d;
}
vec3 hullNormal(vec3 p, int kind, float sd) {
    vec2 e = vec2(1.0, -1.0) * 0.0015;
    return normalize(e.xyy * hullSDF(p + e.xyy, kind, sd) + e.yyx * hullSDF(p + e.yyx, kind, sd) +
                     e.yxy * hullSDF(p + e.yxy, kind, sd) + e.xxx * hullSDF(p + e.xxx, kind, sd));
}

// 1970s-80s paperback science-fiction cover palettes: three big blocks and an accent
const vec3 PB[24] = vec3[24](
    vec3(0.91, 0.85, 0.66), vec3(0.72, 0.22, 0.16), vec3(0.13, 0.22, 0.48), vec3(0.95, 0.90, 0.75),
    vec3(0.93, 0.90, 0.80), vec3(0.83, 0.62, 0.12), vec3(0.34, 0.33, 0.16), vec3(0.20, 0.30, 0.55),
    vec3(0.66, 0.78, 0.86), vec3(0.88, 0.47, 0.44), vec3(0.28, 0.16, 0.34), vec3(0.96, 0.92, 0.82),
    vec3(0.92, 0.92, 0.88), vec3(0.16, 0.30, 0.68), vec3(0.94, 0.80, 0.22), vec3(0.42, 0.10, 0.20),
    vec3(0.86, 0.78, 0.60), vec3(0.72, 0.16, 0.44), vec3(0.09, 0.10, 0.28), vec3(0.62, 0.86, 0.80),
    vec3(0.73, 0.82, 0.64), vec3(0.54, 0.25, 0.15), vec3(0.94, 0.90, 0.78), vec3(0.20, 0.34, 0.30));

vec3 satur(vec3 c, float s) { float l = dot(c, vec3(0.299, 0.587, 0.114)); return mix(vec3(l), c, s); }

vec3 shipShade(vec3 pl, vec3 nl, vec3 rdl, vec3 Ll, float sd, int kind, float pixl) {
    int pal = int(fract(sd * 5.71) * 6.0);
    vec3 zc[3];
    float rot = fract(sd * 13.13);
    int o = int(rot * 3.0);
    zc[0] = PB[pal * 4 + o % 3]; zc[1] = PB[pal * 4 + (o + 1) % 3]; zc[2] = PB[pal * 4 + (o + 2) % 3];
    vec3 acc = PB[pal * 4 + 3];
    float s1 = mix(-0.25, -0.05, fract(sd * 3.3)), s2 = mix(0.10, 0.30, fract(sd * 9.1));
    int zi = (pl.x > s1 ? 1 : 0) + (pl.x > s2 ? 1 : 0);
    vec3 alb = zc[zi];
    // a stripe panel band along the flank
    float stripe = smoothstep(0.012, 0.006, abs(pl.y - 0.012 - 0.02 * fract(sd * 17.0)));
    if (kind != 4) alb = mix(alb, acc, stripe * step(0.02, abs(nl.z) + abs(nl.x) * 0.0));
    if (gMat == 1) alb = mix(alb, acc, 0.7);
    if (gMat == 3) alb *= 0.55;
    alb = satur(alb, u_look2.y);
    float amt = u_style > 0 ? 1.0 : 0.0;
    // fixed object-space grain, faded out when finer than a pixel
    float gs = 260.0;
    float g = hash31(floor(pl * gs));
    float gf = 1.0 - smoothstep(0.35, 1.0, pixl * gs);
    float grain = (g - 0.5) * gf;
    float wrap = dot(nl, Ll) * 0.5 + 0.5;
    float sh = wrap + grain * 0.14 * amt;
    float b = sh * 4.0, bi = floor(b), bf = b - bi;
    float lv = (bi + smoothstep(0.30, 0.70, bf)) / 4.0;
    lv = mix(sh, lv, 0.62 * amt);
    float k = clamp(lv * 1.25 - 0.12, 0.0, 1.0);
    vec3 shadowC = alb * vec3(0.30, 0.27, 0.46);
    vec3 col = mix(shadowC, alb * 1.06, k);
    float rim = pow(1.0 - max(dot(nl, -rdl), 0.0), 3.0);
    col += vec3(0.96, 0.92, 0.80) * rim * 0.30 * (0.35 + 0.65 * k);
    col *= mix(0.58, 1.0, gL);
    col *= 1.0 + grain * 0.10 * amt;
    return col;
}

// ---- main -------------------------------------------------------------------
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

void main() {
    setPalette();
    gL = clamp((u_e + 1.0) * 0.5, 0.0, 1.0);
    gN = 1.0 - gL;
    gCloud = u_wx1.w;
    // haze: pale and heavy in pale-reach, clean and glowing in night, plus the weather
    float haze = u_look.y;
    gK = haze * mix(1.0 / 7000.0, 1.0 / 3000.0, gL);
    gK += u_wx0.w / 1100.0 + u_wx1.x / 1500.0 + u_wx1.y / 2200.0 + u_wx1.z / 2000.0 + u_wx0.x / 3800.0 + u_wx0.y / 2200.0 + u_wx0.z / 2800.0;
    gKu = haze * mix(1.0 / 40000.0, 1.0 / 16000.0, gL);
    gHf = mix(430.0, 240.0, clamp(u_wx0.w + u_wx1.x * 0.6, 0.0, 1.0));

    float asp = u_res.x / u_res.y;
    vec2 q = v_q;
    vec2 pl = vec2((q.x - u_lens.y) * asp, q.y - u_lens.z) * u_lens.x;
    vec3 rd = normalize(u_fwd + u_right * pl.x + u_up * pl.y);
    float pxa = 2.0 * u_lens.x / u_res.y;
    vec3 ro = u_org;
    vec3 rdt = rd;
    rdt.x = abs(rdt.x) < 1e-5 ? 1e-5 : rdt.x;
    rdt.y = abs(rdt.y) < 1e-5 ? 1e-5 : rdt.y;
    rdt.z = abs(rdt.z) < 1e-5 ? 1e-5 : rdt.z;

    float tfar = min(9000.0, 4.2 / max(gK + gKu, 1e-4));
    trace(ro, rdt, tfar);
    float tHit = 1e9;
    vec3 col;
    vec3 fc = fogColor(rd);
    if (gKind == 0) {
        vec3 s = skyBase(rd);
        float T = exp(-odSky(ro.y, rd.y));
        col = mix(fc, s, T);
        if (rd.y < 0.02) col = mix(col, fc, smoothstep(0.02, -0.02, rd.y));
        if (gKind == 0 && rd.y < 0.0) col = fc;   // beyond the range on a downward ray: the far haze
        tHit = 1e9;
    } else {
        tHit = gT;
        vec3 p = ro + rd * gT;
        vec3 surf = gKind == 1 ? shadeBuilding(p, gNrm, rd, gT, pxa) : shadeGround(p, rd, gT);
        float T = exp(-odFog(ro.y, rd.y, gT));
        col = mix(fc, surf, T);
    }

    // ships
    int nq = u_quality > 0 ? 40 : 24;
    for (int i = 0; i < 5; i++) {
        vec4 sa = u_ship_a[i];
        if (sa.w <= 0.0) continue;
        vec4 sb = u_ship_b[i];
        if (sb.w <= 0.0) continue;
        vec3 c = sa.xyz;
        float len = sa.w;
        float R = len * 0.62;
        float bproj = dot(c, rd);
        float d2 = dot(c, c) - bproj * bproj;
        if (bproj < 0.0 || d2 > R * R) continue;
        float tin = bproj - sqrt(R * R - d2);
        if (tin > tHit) continue;
        float cy = cos(sb.x), sy = sin(sb.x);
        // world -> ship: rotate about y by -yaw, then scale by 1/len (x forward)
        mat3 Rm = mat3(sy, 0.0, cy, 0.0, 1.0, 0.0, cy, 0.0, -sy);   // world -> ship-local: x along the heading
        vec3 rol = Rm * (-c) / len;
        vec3 rdl = Rm * rd;
        float tl = max(tin, 0.0) / len, tend = (bproj + sqrt(R * R - d2)) / len;
        float sd = sb.y;
        int kind = int(sb.z + 0.5);
        float dmin = 1e9, tmin = tl;
        bool hit = false;
        float epsP = max(0.0006, pxa * tl);
        for (int s = 0; s < 40; s++) {
            if (s >= nq) break;
            vec3 pp = rol + rdl * tl;
            float dd = hullSDF(pp, kind, sd);
            if (dd < dmin) { dmin = dd; tmin = tl; }
            epsP = max(0.0004, pxa * tl * 0.6);
            if (dd < epsP) { hit = true; break; }
            tl += dd * 0.85;
            if (tl > tend) break;
        }
        float alpha = 1.0;
        if (!hit) {
            if (dmin < 2.5 * epsP) { hit = true; tl = tmin; alpha = 1.0 - smoothstep(0.5 * epsP, 2.5 * epsP, dmin); }
        }
        if (!hit) continue;
        float tw = tl * len;
        if (tw > tHit) continue;
        vec3 pp = rol + rdl * tl;
        float dd = hullSDF(pp, kind, sd);   // sets gMat for this point
        int mat = gMat;
        vec3 nl = hullNormal(pp, kind, sd);
        gMat = mat;
        // key light: the sun by day, a cool overhead with magenta underglow by night
        vec3 sunl = Rm * u_sundir;
        vec3 Ll = normalize(mix(vec3(0.3, 0.8, 0.4), sunl, smoothstep(-0.1, 0.3, u_sundir.y)));
        vec3 sc = shipShade(pp, nl, rdl, Ll, sd, kind, pxa * tl);
        if (mat == 2) {
            float glowE = smoothstep(-0.44, -0.40, pp.x) < 0.5 ? 1.0 : 0.6;
            sc = vec3(0.75, 0.90, 1.0) * 2.4 * glowE;
        }
        // underglow from the neon streets
        sc += palGlowNight() * 0.9 * gN * max(-nl.y, 0.0);
        vec3 wp = ro + rd * tw;
        float T = exp(-odFog(ro.y, rd.y, tw));
        sc = mix(fc, sc, T);
        col = mix(col, sc, alpha * sb.w);
        if (alpha > 0.5) tHit = tw;
    }
    // engine haze around every live ship: a soft glow so the far ones read as lights
    for (int i = 0; i < 5; i++) {
        vec4 sa = u_ship_a[i];
        if (sa.w <= 0.0 || u_ship_b[i].w <= 0.0) continue;
        float cy = cos(u_ship_b[i].x), sy = sin(u_ship_b[i].x);
        vec3 fwdw = vec3(sy, 0.0, cy);
        vec3 pe = sa.xyz - fwdw * sa.w * 0.47;
        float bp = dot(pe, rd);
        if (bp <= 0.0 || bp > tHit) continue;
        float d = length(pe - rd * bp);
        float rr = max(sa.w * 0.035, bp * pxa * 1.5);
        float g = exp(-d * d / (rr * rr)) * u_ship_b[i].w;
        float T = exp(-odFog(ro.y, rd.y, bp));
        col += vec3(0.60, 0.80, 1.0) * g * 1.6 * T * (0.5 + 0.5 * gN);
    }
    // small lane traffic: projected points, the scale cue
    int nc = u_quality > 0 ? 24 : 12;
    for (int i = 0; i < 24; i++) {
        if (i >= nc) break;
        vec4 cr = u_craft[i];
        float fade = abs(cr.w);
        if (fade <= 0.0) continue;
        vec3 rel = cr.xyz;
        float dz = dot(rel, u_fwd);
        if (dz < 4.0 || dz > tHit) continue;
        vec2 qc = vec2(dot(rel, u_right), dot(rel, u_up)) / dz;
        float dpx = length(qc - pl) / pxa;
        float rad = max(1.0, 3.0 / (dz * pxa));
        if (dpx > rad * 4.0) continue;
        float g = exp(-dpx * dpx / (rad * rad)) * fade;
        bool toward = cr.w < 0.0;
        vec3 lc = toward ? vec3(1.0, 0.92, 0.78) : vec3(1.0, 0.15, 0.10);
        float T = exp(-odFog(ro.y, rd.y, dz));
        col += lc * g * (1.2 * gN + 0.05) * T;
        col = mix(col, col * 0.35, g * gL * 0.7 * T);   // by day: a dark speck
    }

    // weather in front of the scene
    float rainA = u_wx0.x, snowA = u_wx0.y, sleetA = u_wx0.z, dustA = u_wx1.y, ashA = u_wx1.z;
    vec2 sp = pl / u_lens.x;
    float wind = u_wx2.y;
    vec3 pcol = mix(fc * 1.6 + vec3(0.06, 0.05, 0.09), vec3(0.86, 0.90, 0.95), gL);
    int nl = u_quality > 0 ? 3 : 2;
    if (rainA + sleetA > 0.01) {
        for (int k = 0; k < 3; k++) {
            if (k >= nl) break;
            float dk = k == 0 ? 6.0 : (k == 1 ? 22.0 : 70.0);
            if (tHit < dk) continue;
            float fk = float(k);
            vec2 g = sp * vec2(16.0 + 22.0 * fk, 3.2 + 1.6 * fk);
            g.x += sp.y * (0.5 + 1.5 * wind) * (16.0 + 22.0 * fk) * 0.25;
            g.y += u_time * (5.0 + 2.5 * fk) * (1.0 + 0.6 * sleetA);
            vec2 cell = floor(g), f = fract(g);
            float r = hash21(cell + fk * 17.3);
            float dens = (rainA + 0.6 * sleetA) * (0.36 - 0.07 * fk);
            float pres = step(1.0 - dens, r);
            float xr = hash21(cell.yx + 3.7) * 0.6 + 0.2;
            float sx = 1.0 - smoothstep(0.0, 0.10 - 0.02 * fk, abs(f.x - xr));
            float lenY = sleetA > rainA ? 0.35 : 0.55;
            float sy = smoothstep(0.0, 0.2, f.y) * (1.0 - smoothstep(lenY, 1.0, f.y));
            col += pcol * (sx * sy * pres * (0.62 - 0.12 * fk));
        }
    }
    if (snowA + sleetA * 0.5 > 0.01) {
        for (int k = 0; k < 3; k++) {
            if (k >= nl + 1 && u_quality == 0) break;
            float dk = k == 0 ? 5.0 : (k == 1 ? 18.0 : 60.0);
            if (tHit < dk) continue;
            float fk = float(k);
            vec2 g = sp * (7.0 + 8.0 * fk);
            g.y += u_time * (0.45 + 0.2 * fk);
            g.x += sin(u_time * 0.5 + g.y * 0.8 + fk) * 0.35 + u_time * 0.12 * wind * (1.0 + fk);
            vec2 cell = floor(g), f = fract(g) - 0.5;
            vec2 off = (hash22(cell + fk * 7.0) - 0.5) * 0.6;
            float rad = 0.10 + 0.10 * hash21(cell + 5.0) - 0.02 * fk;
            float flake = smoothstep(rad, rad * 0.35, length(f - off));
            float dens = (snowA + sleetA * 0.4) * (0.55 - 0.10 * fk);
            flake *= step(1.0 - dens, hash21(cell + 9.0 + fk));
            vec3 sc = mix(fc * 1.5 + 0.08, vec3(0.95, 0.97, 1.0), gL * 0.9 + 0.1);
            col = mix(col, sc, flake * 0.85);
        }
    }
    if (dustA > 0.01) {
        vec2 g = sp * 34.0; g.x += u_time * (0.9 + 1.6 * wind); g.y += sin(u_time * 0.3 + g.x * 0.1) * 0.6;
        vec2 cell = floor(g), f = fract(g) - 0.5;
        float m = smoothstep(0.16, 0.02, length(f - (hash22(cell) - 0.5) * 0.5)) * step(1.0 - 0.22 * dustA, hash21(cell + 3.0));
        col = mix(col, mix(vec3(0.20, 0.14, 0.11), vec3(0.78, 0.62, 0.40), gL), m * 0.55);
    }
    if (ashA > 0.01) {
        for (int k = 0; k < 2; k++) {
            float fk = float(k);
            vec2 g = sp * (9.0 + 9.0 * fk);
            g.y += u_time * (0.35 + 0.25 * fk);
            g.x += sin(u_time * 0.4 + g.y * 0.9 + fk * 2.0) * 0.5;
            vec2 cell = floor(g), f = fract(g) - 0.5;
            vec2 off = (hash22(cell + fk * 3.0) - 0.5) * 0.5;
            float m = smoothstep(0.13, 0.03, length((f - off) * vec2(1.0, 1.7))) * step(1.0 - 0.32 * ashA, hash21(cell + 1.0 + fk));
            col = mix(col, mix(fc * 1.2 + 0.03, vec3(0.16, 0.16, 0.18), gL), m * 0.7);
        }
    }

    // tone map
    float expo = u_look.x * mix(1.0, 1.22, gL);
    col = aces(col * expo);
    col = pow(col, vec3(1.0 / 2.2));
    // lightning: added in display space with a spatial profile <= 1, so the frame
    // luminance can never rise by more than u_flash whatever the scene is.
    if (u_flash > 0.0) {
        float sky = tHit > 1e8 ? 1.0 : 0.0;
        float az = atan(rd.x, rd.z);
        float da = abs(mod(az - u_bolt.x + PI, 2.0 * PI) - PI);
        float glow = exp(-da * da / 0.9) * smoothstep(0.0, 0.3, rd.y);
        float prof = clamp(0.30 + 0.40 * sky + 0.30 * glow, 0.0, 1.0);
        float el = atan(rd.y, length(rd.xz));
        float jag = 0.012 * (vn(vec2(el * 26.0, u_bolt.y)) - 0.5) * 2.0 + 0.006 * sin(el * 90.0 + u_bolt.y);
        float bolt = smoothstep(0.006, 0.0, abs(da + jag)) * smoothstep(0.03, 0.15, el) * (1.0 - smoothstep(0.5, 0.85, el)) * sky;
        prof = max(prof, bolt);
        col += vec3(0.86, 0.90, 1.0) * (u_flash * prof);
    }
    // fixed dither (not animated)
    col += (hash21(gl_FragCoord.xy) - 0.5) / 255.0;
    o_col = vec4(clamp(col, 0.0, 1.0), 1.0);
}
#endif
