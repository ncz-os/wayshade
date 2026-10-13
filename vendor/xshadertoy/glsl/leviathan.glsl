// SPDX-License-Identifier: Apache-2.0
// leviathan.glsl -- a flight to, across and into a vessel too large to take in,
// through a gate, into a quiet room, and out to the next vessel.
// Licence: original work, MIT (matches the catalogue convention).
//
// v2 (2026-09-29) replaces the three-movement v1 (plasma lattice, light corridor,
// false-colour terrain). Design notes, the brief it follows and what was measured:
// docs/LEVIATHAN.md.
//
// One chapter (390 to 420 s, the length comes from iSeed) is five acts on one clock:
//   0-40 s     silhouette  far outside; a dark hull slides in and eats the stars
//   40-150 s   hull pass   a low glide down a lit runway toward a bay aperture
//   150-270 s  interior    a ribbed cavern, slotted light shafts, dust; speed doubles
//   270-360 s  the gate    two streak planes and a horizon slit, colour fields, a coda
//   360-end    quiet       a still chamber lit from below that thins back to the stars
// Each chapter re-seeds the vessel: proportions, landmark (ring or fins), palette,
// planet, and the side the vessel appears on (the whole ship frame is mirrored).
//
// Single pass and analytic: boxes and cylinders by slab tests, a 2D grid DDA for the
// hull relief (real parallax, not a texture), planar ribs, closed-form light shafts.
// Only the act(s) visible at the current time are evaluated; the choice depends on
// time alone, so every branch on it is uniform across the frame.
//
// Safety, enforced in the maths (a single pass cannot measure its own frame):
//   the gate is built in display space from OKLab lightness 0.26-0.78 and clamped
//   to 0.85; hue drifts at <= 0.03 Hz; every blinking light is <= 1 Hz with soft
//   edges; nothing modulates faster than 3 Hz; the camera never rolls and turns at
//   < 2 deg/s; no bright element is static for more than a minute; the output is
//   dithered (triangular, 1 LSB) against banding.
//
// Cost: the driver defines NCZ_QUALITY (0 weak, 1 mid, 2 strong GPU).

#ifndef NCZ_QUALITY
#define NCZ_QUALITY 2
#endif
#if NCZ_QUALITY == 0
#define LV_DDA_FINE 18
#define LV_DDA_COARSE 8
#define LV_SHADOW 1
#define LV_RIBS 7
#define LV_SHAFTS 3
#define LV_DUST 2
#define LV_IDDA 12
#define LV_OCT 1
#define LV_CODA 0
#elif NCZ_QUALITY == 1
#define LV_DDA_FINE 28
#define LV_DDA_COARSE 12
#define LV_SHADOW 2
#define LV_RIBS 10
#define LV_SHAFTS 4
#define LV_DUST 3
#define LV_IDDA 18
#define LV_OCT 2
#define LV_CODA 1
#define LV_CODA_STEPS 32
#else
#define LV_DDA_FINE 40
#define LV_DDA_COARSE 16
#define LV_SHADOW 4
#define LV_RIBS 14
#define LV_SHAFTS 6
#define LV_DUST 3
#define LV_IDDA 26
#define LV_OCT 2
#define LV_CODA 1
#define LV_CODA_STEPS 44
#endif

// Options. The leviathan driver defines LV_OPTIONS and uploads these three
// vectors from its option table (src/leviathan_opts.h); any other player (the
// headless smoke tester) gets the defaults: everything automatic from iSeed.
// An enum is its choice index, -1 = auto.
//   uLv0: hull, landmark, window palette, aperture side
//   uLv1: planet, interior style, randomize (0 auto: re-seed per chapter,
//         1 fixed: every chapter the same), chapter length (arc s, 0 = auto)
//   uLv2: arc scale (wall seconds per arc second), gate hue (radians, < -8 =
//         auto), gate intensity (0.3..1, always inside the caps), star density
#ifdef LV_OPTIONS
uniform vec4 uLv0;
uniform vec4 uLv1;
uniform vec4 uLv2;
#else
const vec4 uLv0 = vec4(-1.0, -1.0, -1.0, -1.0);
const vec4 uLv1 = vec4(-1.0, -1.0, 0.0, 0.0);
const vec4 uLv2 = vec4(1.0, -9.0, 1.0, 1.0);
#endif

// pick: the option's index when set, else a seeded draw among n choices
float pick(float opt, float r, float n) { return opt >= 0.0 ? opt : min(floor(r * n), n - 1.0); }

const float PI = 3.14159265;
const float TAU = 6.2831853;
const float FOCAL = 1.0;

// Act boundaries (chapter seconds). The quiet act takes whatever the chapter
// length leaves after T_QUIET.
const float T_HULL = 40.0;
const float T_INT = 150.0;
const float T_GATE = 270.0;
const float T_FIELDS = 314.0;
const float T_CODA = 331.0;
const float T_QUIET = 360.0;

// Ship frame, 1 unit ~ 5 m. The upper deck (heightfield) spans |x| < W1, y ~ 0,
// Z_DECK < z < ZW; the runway is at x = XL; the superstructure face is at z = ZW.
const float W1 = 150.0;
const float XL = 95.0;
const float HC = 12.0;
const float Z1 = 200.0;
const float Z_DECK = 80.0;
const float ZW = 1000.0;
const float VH = 800.0 / 110.0;       // runway speed: Z1 at T_HULL to ZW at T_INT
const float CELL = 4.0;
const float MODW = 24.0;
const float HF_Y0 = -14.0;
const float HF_Y1 = 26.0;
const vec3 BAYC = vec3(95.0, 16.0, 1000.0);
const vec2 BAYH = vec2(26.0, 14.0);
const float YAW0 = 0.30;
const float PITCH0 = -0.17;
const float PITCH1 = -0.105;

// Interior (cavern) frame: origin at the bay centre, +z inward.
const float RIBP = 16.0;
const float SH_P = 48.0;
const float SH_W = 3.0;
const float SH_TAN = 0.45;
const float SH_X = 9.0;

// ---------------------------------------------------------------
// Hashes and noise (integer, no sin() of large arguments)
// ---------------------------------------------------------------
uint pcg(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float u01(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }

uint gSeed;      // chapter seed
uint gRunSeed;   // run seed (stars stay put across chapters)

float h2s(ivec2 c, uint salt, uint seed) {
    uint a = uint(c.x + 1048576);
    uint b = uint(c.y + 1048576);
    return u01(pcg(a ^ pcg(b + salt * 2654435761u + seed)));
}
float h2(ivec2 c, uint salt) { return h2s(c, salt, gSeed); }
float h1(float x, uint salt) { return h2(ivec2(int(x), 7), salt); }

float vn2(vec2 p, uint salt) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    ivec2 c = ivec2(i);
    float a = h2(c, salt);
    float b = h2(c + ivec2(1, 0), salt);
    float d = h2(c + ivec2(0, 1), salt);
    float e = h2(c + ivec2(1, 1), salt);
    return mix(mix(a, b, f.x), mix(d, e, f.x), f.y);
}

float h3(ivec3 c, uint salt) {
    uint a = uint(c.x + 1048576);
    uint b = uint(c.y + 1048576);
    uint d = uint(c.z + 1048576);
    return u01(pcg(a ^ pcg(b ^ pcg(d + salt * 2654435761u + gSeed))));
}
float vn3(vec3 p, uint salt) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    ivec3 c = ivec3(i);
    return mix(mix(mix(h3(c, salt), h3(c + ivec3(1, 0, 0), salt), f.x),
                   mix(h3(c + ivec3(0, 1, 0), salt), h3(c + ivec3(1, 1, 0), salt), f.x), f.y),
               mix(mix(h3(c + ivec3(0, 0, 1), salt), h3(c + ivec3(1, 0, 1), salt), f.x),
                   mix(h3(c + ivec3(0, 1, 1), salt), h3(c + ivec3(1, 1, 1), salt), f.x), f.y), f.z);
}

// ---------------------------------------------------------------
// Colour
// ---------------------------------------------------------------
vec3 aces(vec3 c) {
    mat3 m1 = mat3(0.59719, 0.07600, 0.02840,
                   0.35458, 0.90834, 0.13383,
                   0.04823, 0.01566, 0.83777);
    mat3 m2 = mat3( 1.60475, -0.10208, -0.00327,
                   -0.53108,  1.10813, -0.07276,
                   -0.07367, -0.00605,  1.07602);
    vec3 v = m1 * c;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));
}

// OKLab lightness/chroma/hue to linear sRGB (clamped to >= 0).
vec3 lch(float L, float C, float h) {
    float a = C * cos(h);
    float b = C * sin(h);
    float l_ = L + 0.3963377774 * a + 0.2158037573 * b;
    float m_ = L - 0.1055613458 * a - 0.0638541728 * b;
    float s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    return max(vec3( 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s), 0.0);
}
vec3 toDisplay(vec3 lin) { return pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2)); }

// ---------------------------------------------------------------
// Chapter state
// ---------------------------------------------------------------
float gPix;          // radians per pixel
float gMirror;       // +1 or -1: which side the vessel is on
float gMirrorNext;
float gW0, gDH, gYB0, gW2, gH2, gLen2, gLh;
float gRing;         // 1: ring landmark, 0: fin landmark
float gPods;
float gHue0;
vec3 gSun, gSunCol, gFillDir, gFillCol, gWinA, gWinB, gTint;
vec3 gPlanetC, gPlanetA, gPlanetB;
float gPlanetR;
float gPlanetOn;
float gFoundry;
vec2 gSpire;

uint chapterSeed(float chap) {
    if (uLv1.z > 0.5) chap = 0.0;      // randomize = fixed
    uint a = uint(iSeed.x * 16777215.0);
    uint b = uint(iSeed.y * 16777215.0);
    return pcg(a ^ pcg(b + uint(chap) * 747796405u + 12345u));
}
float crand(uint cs, uint k) { return u01(pcg(cs + k * 2654435761u)); }

void setChapter(float chap) {
    uint cs = chapterSeed(chap);
    gSeed = cs;
    gRunSeed = pcg(uint(iSeed.x * 16777215.0) + 99u);
    gMirror = uLv0.w >= 0.0 ? (uLv0.w > 0.5 ? 1.0 : -1.0) : (crand(cs, 1u) < 0.5 ? -1.0 : 1.0);
    gMirrorNext = uLv0.w >= 0.0 ? gMirror : (crand(chapterSeed(chap + 1.0), 1u) < 0.5 ? -1.0 : 1.0);
    // hull family: 0 slab, 1 tiered (tall superstructure, deep drop), 2 long (lean)
    float hull = pick(uLv0.x, crand(cs, 18u), 3.0);
    gW0 = 250.0 + 45.0 * crand(cs, 2u) - (hull == 2.0 ? 30.0 : 0.0);
    gDH = 30.0 + 25.0 * crand(cs, 3u) + (hull == 1.0 ? 20.0 : 0.0);
    gYB0 = -gDH - 90.0 - 45.0 * crand(cs, 4u);
    gW2 = 155.0 + 50.0 * crand(cs, 5u);
    gH2 = 100.0 + 60.0 * crand(cs, 6u) + (hull == 1.0 ? 50.0 : 0.0) - (hull == 2.0 ? 20.0 : 0.0);
    gLen2 = 380.0 + 220.0 * crand(cs, 7u);
    gLh = 2600.0 + 800.0 * crand(cs, 8u) + (hull == 2.0 ? 900.0 : 0.0);
    gRing = pick(uLv0.y, crand(cs, 9u) * 0.91, 2.0) < 0.5 ? 1.0 : 0.0;    // 0 ring, 1 fins
    gPods = crand(cs, 10u) < 0.7 ? 1.0 : 0.0;
    gHue0 = uLv2.y > -8.0 ? uLv2.y : TAU * crand(cs, 11u);

    gSun = normalize(vec3(-0.5, 0.33, 0.83));
    gSunCol = vec3(1.0, 0.93, 0.84) * 3.0;
    // window palette: 0 warm, 1 cool, 2 sodium, 3 white, 4 harmonic (a generated
    // colourway: windows at the chapter hue and its complement, the gate hue and
    // the interior lamps follow it)
    float pal = pick(uLv0.z, crand(cs, 12u), 5.0);
    float warm = crand(cs, 19u);
    gWinA = mix(vec3(1.0, 0.78, 0.50), vec3(1.0, 0.86, 0.66), warm);
    gWinB = mix(vec3(0.62, 0.80, 1.0), vec3(0.85, 0.92, 1.0), warm);
    if (pal == 1.0) { vec3 k = gWinA; gWinA = gWinB; gWinB = k; }
    if (pal == 2.0) { gWinA = vec3(1.0, 0.62, 0.28); gWinB = vec3(1.0, 0.80, 0.55); }
    if (pal == 3.0) { gWinA = vec3(0.95, 0.95, 1.0); gWinB = vec3(1.0, 0.9, 0.78); }
    if (pal == 4.0) {
        float hh = TAU * crand(cs, 20u);
        gWinA = lch(0.86, 0.09, hh) * 1.6;
        gWinB = lch(0.86, 0.07, hh + PI * (0.8 + 0.4 * crand(cs, 21u))) * 1.6;
        if (uLv2.y <= -8.0) gHue0 = hh + 0.6;
    }
    gTint = mix(vec3(0.95, 0.97, 1.02), vec3(1.03, 0.99, 0.94), crand(cs, 13u));

    // planet: 0 ocean, 1 desert, 2 ice, 3 banded giant, 4 none
    float pk = uLv1.x >= 0.0 ? (uLv1.x + 0.5) / 5.0 * 1.25 : crand(cs, 14u);
    gPlanetOn = pk < 1.0 ? 1.0 : 0.0;
    if (pk < 0.3) {        // ocean world
        gPlanetA = vec3(0.05, 0.14, 0.32); gPlanetB = vec3(0.30, 0.34, 0.22);
    } else if (pk < 0.55) { // desert
        gPlanetA = vec3(0.42, 0.24, 0.12); gPlanetB = vec3(0.62, 0.46, 0.30);
    } else if (pk < 0.8) {  // ice
        gPlanetA = vec3(0.45, 0.58, 0.68); gPlanetB = vec3(0.80, 0.86, 0.90);
    } else {               // banded giant
        gPlanetA = vec3(0.50, 0.38, 0.28); gPlanetB = vec3(0.78, 0.66, 0.50);
    }
    vec3 pd = normalize(vec3(0.62 + 0.15 * crand(cs, 15u), -0.40 - 0.08 * crand(cs, 16u), 0.68));
    gPlanetR = 4600.0 + 1400.0 * crand(cs, 17u);
    gPlanetC = pd * 12500.0;
    gFillDir = pd;
    gFillCol = mix(gPlanetA, gPlanetB, 0.5) * 0.2 * gPlanetOn + vec3(0.01, 0.012, 0.018) * (1.0 - gPlanetOn);
    // interior: 0 hangar (daylight shafts), 1 foundry (amber shafts)
    gFoundry = pick(uLv1.y, crand(cs, 30u), 2.0);

    // The spire stands on the sun line from the runway at t = 95 s, so it
    // eclipses the sun for about two seconds.
    vec2 lxz = normalize(gSun.xz);
    float u = 95.0 - T_HULL;
    vec2 cam = vec2(XL + 3.0 * (1.0 - cos(0.05 * u)), Z1 + VH * u);
    gSpire = cam + lxz * 110.0;
}

// ---------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------
// Coverage of lines of width w every period, antialiased with footprint fp
// (world units per pixel); fades to the mean when the lines get sub-pixel.
float gridLine(float x, float period, float w, float fp) {
    float d = abs(fract(x / period + 0.5) - 0.5) * period;
    float cov = 1.0 - smoothstep(0.5 * w - 0.5 * fp, 0.5 * w + 0.5 * fp, d);
    return mix(cov, w / period, smoothstep(0.25 * period, 0.6 * period, fp));
}

// Soft blink <= 1 Hz: on for about a third of the period, 0.12-period edges.
float blink(float t, float hz, float phase) {
    float x = fract(t * hz + phase);
    return smoothstep(0.0, 0.12, x) * (1.0 - smoothstep(0.30, 0.42, x));
}

vec3 nz(vec3 v) {
    return vec3(abs(v.x) < 1e-6 ? 1e-6 : v.x, abs(v.y) < 1e-6 ? 1e-6 : v.y, abs(v.z) < 1e-6 ? 1e-6 : v.z);
}

vec2 boxT(vec3 ro, vec3 ird, vec3 bmin, vec3 bmax, out vec3 n) {
    vec3 t0 = (bmin - ro) * ird;
    vec3 t1 = (bmax - ro) * ird;
    vec3 tn = min(t0, t1);
    vec3 tf = max(t0, t1);
    float a = max(max(tn.x, tn.y), tn.z);
    float b = min(min(tf.x, tf.y), tf.z);
    n = -sign(ird) * step(tn.yzx, tn) * step(tn.zxy, tn);
    return vec2(a, b);
}

// Screen-space glow of a point light at p, occluded beyond tOcc.
float glow(vec3 ro, vec3 rd, vec3 p, float tOcc, float size) {
    vec3 o = p - ro;
    float tp = dot(o, rd);
    if (tp < 0.5 || tp > tOcc) return 0.0;
    float d = length(o - rd * tp);
    float r = max(size, tp * gPix * 1.2);
    return (size * size) / (r * r) * exp(-d * d / (r * r));
}

// ---------------------------------------------------------------
// Exterior: the vessel
// ---------------------------------------------------------------
const int M_NONE = 0;
const int M_HULL = 1;
const int M_HF_TOP = 2;
const int M_HF_SIDE = 3;
const int M_WALL = 4;
const int M_AP = 5;
const int M_RING = 6;
const int M_CRAFT = 7;
const int M_FLAT = 8;
const int M_MOD_TOP = 9;

float hT;
vec3 hN;
int hM;
float hId;
vec3 hBmin, hBmax;
ivec2 hCell;

void tryBox(vec3 ro, vec3 ird, vec3 a, vec3 b, int mat, float id) {
    vec3 n;
    vec2 t = boxT(ro, ird, a, b, n);
    if (t.x < t.y && t.x > 0.0 && t.x < hT) {
        hT = t.x; hN = n; hM = mat; hId = id; hBmin = a; hBmax = b;
    }
}

// Ring landmark: a cylinder shell around the hull axis (0, -60), z in [zr, zr + 36].
const float RING_Z = 450.0;
const float RING_IN = 318.0;
const float RING_OUT = 332.0;
const float RING_Y = -60.0;
void tryRing(vec3 ro, vec3 rd) {
    vec2 o = ro.xy - vec2(0.0, RING_Y);
    vec2 d = rd.xy;
    float A = dot(d, d);
    float B = dot(o, d);
    float z0 = RING_Z;
    float z1 = RING_Z + 36.0;
    // caps
    if (abs(rd.z) > 1e-5) {
        for (int i = 0; i < 2; i++) {
            float zc = i == 0 ? z0 : z1;
            float t = (zc - ro.z) / rd.z;
            if (t > 0.0 && t < hT) {
                float r = length(o + d * t);
                if (r > RING_IN && r < RING_OUT) {
                    hT = t; hN = vec3(0.0, 0.0, i == 0 ? -1.0 : 1.0); hM = M_RING; hId = 0.0;
                }
            }
        }
    }
    if (A < 1e-8) return;
    for (int i = 0; i < 2; i++) {
        float R = i == 0 ? RING_OUT : RING_IN;
        float C = dot(o, o) - R * R;
        float disc = B * B - A * C;
        if (disc < 0.0) continue;
        float sq = sqrt(disc);
        for (int j = 0; j < 2; j++) {
            float t = (-B + (j == 0 ? -sq : sq)) / A;
            if (t > 0.0 && t < hT) {
                float z = ro.z + rd.z * t;
                if (z > z0 && z < z1) {
                    vec2 q = o + d * t;
                    hT = t; hN = vec3(normalize(q) * (i == 0 ? 1.0 : -1.0), 0.0); hM = M_RING; hId = 1.0;
                }
            }
        }
    }
}

// Heightfield: modules of 6 x 6 cells; kinds 0 plating, 1 block, 2 sunken, 3 tall, 4 trench.
float modHeight(ivec2 m, out float kind) {
    if (m.x == -3) { kind = 4.0; return -12.0; }
    if (m.x == 3 || m.x == 4) { kind = 0.0; return 0.0; }   // the runway
    float r = h2(m, 21u);
    float r2 = h2(m, 22u);
    if (r < 0.40) { kind = 0.0; return 0.0; }
    if (r < 0.74) { kind = 1.0; return (m.x == 2 || m.x == 5) ? 3.0 + 3.0 * r2 : 2.5 + 7.0 * r2; }
    if (r < 0.86) { kind = 2.0; return -5.0; }
    if (m.x == 2 || m.x == 5) { kind = 1.0; return 3.0 + 3.0 * r2; }
    kind = 3.0;
    return 4.0;
}
float cellHeight(ivec2 c) {
    ivec2 m = ivec2(floor(vec2(c) / 6.0));
    ivec2 l = c - m * 6;
    float kind;
    float hm = modHeight(m, kind);
    float g = h2(c, 23u);
    bool edge = l.x == 0 || l.y == 0 || l.x == 5 || l.y == 5;
    if (kind == 0.0) return g > 0.84 ? 0.9 : (g > 0.62 ? 0.35 : 0.0);
    if (kind == 1.0) return edge ? 0.35 * step(0.7, g) : hm + (g > 0.76 ? 0.8 : 0.0);
    if (kind == 2.0) return edge ? 0.0 : hm;
    if (kind == 4.0) return (l.x == 0 || l.x == 5) ? 1.2 * step(0.5, g) : -12.0;
    bool core = (l.x == 2 || l.x == 3) && (l.y == 2 || l.y == 3);
    return core ? 12.0 + 12.0 * h2(m, 24u) : (edge ? 0.0 : hm + 0.5 * step(0.6, g));
}
float modTop(ivec2 m) {
    float kind;
    float hm = modHeight(m, kind);
    if (kind == 0.0) return 0.3;
    if (kind == 1.0) return hm + 0.3;
    if (kind == 2.0) return 0.0;
    if (kind == 4.0) return -12.0;
    return 4.2;
}

// Hall floor blocks (height above the floor), 6-unit cells.
float floorCell(ivec2 c) {
    if (abs(float(c.x) + 0.5) * 6.0 < 16.0) return 0.0;              // the central runway
    ivec2 m = ivec2(floor(vec2(c) / 4.0));
    float mk = h2(m, 25u);
    float g = h2(c, 26u);
    if (mk < 0.35) return g > 0.9 ? 1.2 : 0.0;                          // open deck
    if (mk < 0.7) return g > 0.3 ? 1.5 + 2.0 * floor(g * 3.0) : 0.0;    // stores, stacked
    ivec2 l = c - m * 4;
    bool edge = l.x == 0 || l.y == 0;                                    // machinery with aisles
    return edge ? 0.0 : 4.0 + 5.0 * h2(m, 27u) + (g > 0.8 ? 1.5 : 0.0);
}

// Grid DDA over column cells of size cs in xz; heights from cellHeight (cs = CELL)
// or modTop (cs = MODW). status: 1 hit, 0 left [t0, t1], 2 ran out of steps at tOut.
float floorCell(ivec2 c);
// mode 0: deck cells, 1: deck modules, 2: hall floor (heights scaled by hs)
int traceGrid(vec3 ro, vec3 rd, float t0, float t1, vec3 nEnter, int mode, float hs,
              out float tOut, out vec3 nOut, out ivec2 cOut) {
    float cs = mode == 1 ? MODW : (mode == 2 ? 6.0 : CELL);
    vec3 p = ro + rd * t0;
    vec2 st = vec2(rd.x < 0.0 ? -1.0 : 1.0, rd.z < 0.0 ? -1.0 : 1.0);
    vec2 ird = 1.0 / max(abs(rd.xz), vec2(1e-6));
    vec2 cp = p.xz / cs + st * 1e-4;
    ivec2 c = ivec2(floor(cp));
    vec2 nextB = (vec2(c) + max(st, 0.0)) * cs;
    vec2 tMax = t0 + (nextB - p.xz) * st * ird;
    vec2 tDel = cs * ird;
    float t = t0;
    vec3 n = nEnter;
    tOut = t1; nOut = vec3(0.0, 1.0, 0.0); cOut = c;
    int steps = mode == 1 ? LV_DDA_COARSE : (mode == 2 ? LV_IDDA : LV_DDA_FINE);
    for (int i = 0; i < 64; i++) {
        if (i >= steps) { tOut = t; return 2; }
        float h = mode == 0 ? cellHeight(c) : (mode == 1 ? modTop(c) : -22.0 + hs * floorCell(c));
        float yIn = ro.y + rd.y * t;
        if (yIn < h) { tOut = t; nOut = n; cOut = c; return 1; }
        float tx = min(min(tMax.x, tMax.y), t1);
        if (rd.y < 0.0) {
            float th = (h - ro.y) / rd.y;
            if (th <= tx) { tOut = th; nOut = vec3(0.0, 1.0, 0.0); cOut = c; return 1; }
        }
        if (tx >= t1) { tOut = t1; return 0; }
        if (tMax.x < tMax.y) {
            t = tMax.x; tMax.x += tDel.x; c.x += int(st.x); n = vec3(-st.x, 0.0, 0.0);
        } else {
            t = tMax.y; tMax.y += tDel.y; c.y += int(st.y); n = vec3(0.0, 0.0, -st.y);
        }
    }
    tOut = t;
    return 2;
}

// Craft: four small ships near the aperture; one leads the camera down the runway.
vec3 craftPos(int i, float t) {
    if (i == 0) return vec3(XL - 9.0, HC + 5.0, Z1 + 40.0 + (VH + 1.5) * (t - T_HULL));
    float fi = float(i);
    float a = 0.05 * t + fi * 2.1;
    return BAYC + vec3((fi - 2.0) * 34.0 + 10.0 * sin(a), 8.0 * sin(0.7 * a + fi), -40.0 - 30.0 * fi + 12.0 * cos(a));
}
const vec3 CRAFT_R = vec3(1.3, 0.6, 2.6);
void tryCraft(vec3 ro, vec3 rd, float t) {
    for (int i = 0; i < 4; i++) {
        vec3 c = craftPos(i, t);
        vec3 o = (ro - c) / CRAFT_R;
        vec3 d = rd / CRAFT_R;
        float A = dot(d, d);
        float B = dot(o, d);
        float C = dot(o, o) - 1.0;
        float disc = B * B - A * C;
        if (disc > 0.0) {
            float tt = (-B - sqrt(disc)) / A;
            if (tt > 0.0 && tt < hT) {
                hT = tt; hM = M_CRAFT; hId = float(i);
                hN = normalize((o + d * tt) / CRAFT_R);
            }
        }
    }
}

// Every primitive except the heightfield.
void sceneBoxes(vec3 ro, vec3 rd, vec3 ird, float t, bool withCraft) {
    tryBox(ro, ird, vec3(-gW0, gYB0, 0.0), vec3(gW0, -gDH, gLh), M_HULL, 0.0);
    tryBox(ro, ird, vec3(-W1, -gDH, Z_DECK), vec3(W1, HF_Y0, ZW), M_HULL, 1.0);
    tryBox(ro, ird, vec3(-W1, -gDH, ZW), vec3(W1, 0.0, gLh - 200.0), M_HULL, 2.0);
    tryBox(ro, ird, vec3(-gW2, 0.0, ZW), vec3(gW2, gH2, ZW + gLen2), M_HULL, 3.0);
    tryBox(ro, ird, vec3(-0.6 * gW2, gH2, ZW + 80.0), vec3(0.6 * gW2, gH2 + 45.0, ZW + gLen2 - 60.0), M_HULL, 4.0);
    tryBox(ro, ird, vec3(-0.5 * gW0, gYB0 - 50.0, 300.0), vec3(0.5 * gW0, gYB0, gLh - 300.0), M_HULL, 5.0);
    tryBox(ro, ird, vec3(-0.8 * gW0, gYB0 + 15.0, -110.0), vec3(0.8 * gW0, -gDH + 20.0, 0.0), M_HULL, 6.0);
    tryBox(ro, ird, vec3(-0.45 * gW0, gYB0 + 20.0, gLh), vec3(0.45 * gW0, -gDH - 10.0, gLh + 220.0), M_HULL, 7.0);
    if (gPods > 0.5) {
        tryBox(ro, ird, vec3(gW0, -gDH - 70.0, 600.0), vec3(gW0 + 40.0, -gDH - 15.0, 1500.0), M_HULL, 8.0);
        tryBox(ro, ird, vec3(-gW0 - 40.0, -gDH - 70.0, 600.0), vec3(-gW0, -gDH - 15.0, 1500.0), M_HULL, 8.0);
    }
    // the sun spire and its mast
    tryBox(ro, ird, vec3(gSpire.x - 3.0, 0.0, gSpire.y - 3.0), vec3(gSpire.x + 3.0, 58.0, gSpire.y + 3.0), M_HULL, 9.0);
    tryBox(ro, ird, vec3(gSpire.x - 0.5, 58.0, gSpire.y - 0.5), vec3(gSpire.x + 0.5, 80.0, gSpire.y + 0.5), M_HULL, 9.0);
    if (gRing > 0.5) {
        tryRing(ro, rd);
        tryBox(ro, ird, vec3(-6.0, 0.0, RING_Z + 12.0), vec3(6.0, RING_Y + RING_IN + 2.0, RING_Z + 24.0), M_HULL, 10.0);
        tryBox(ro, ird, vec3(gW0, RING_Y - 4.0, RING_Z + 10.0), vec3(RING_IN + 2.0, RING_Y + 4.0, RING_Z + 26.0), M_HULL, 10.0);
        tryBox(ro, ird, vec3(-RING_IN - 2.0, RING_Y - 4.0, RING_Z + 10.0), vec3(-gW0, RING_Y + 4.0, RING_Z + 26.0), M_HULL, 10.0);
    } else {
        for (int i = 0; i < 3; i++) {
            float x = (float(i) - 1.0) * 0.45 * gW2;
            tryBox(ro, ird, vec3(x - 2.5, gH2 + 45.0, ZW + 110.0), vec3(x + 2.5, gH2 + 150.0 - 30.0 * abs(float(i) - 1.0), ZW + 320.0), M_HULL, 11.0);
        }
    }
    if (withCraft) tryCraft(ro, rd, t);
}

// Windows on a vertical face: decks of small lit windows in runs, the odd run
// dark, and every fifth deck a bright promenade. Level of detail by footprint:
// single windows; then each run as a line of light along its deck; then only
// the promenades as lines (6 units apart, the way a lit liner reads at night);
// then the face mean, with a slow large-scale variation.
vec3 windows(vec2 uv, float fp, float dens) {
    vec2 pitch = vec2(1.0, 1.2);
    vec2 g = uv / pitch;
    ivec2 id = ivec2(floor(g));
    vec2 f = fract(g) - 0.5;
    bool prom = (id.y - 5 * (id.y / 5)) == 0;
    float pOn = prom ? 0.85 : 0.3 + 0.25 * dens;
    float rowH = h2(ivec2(id.y, 3), 12u);
    float runLen = 6.0 + 26.0 * rowH;
    float rx = floor((g.x + 97.0 * rowH) / runLen);
    float runOn = step(1.0 - pOn, h2(ivec2(int(rx), id.y), 11u));
    float lit = step(0.35, h2(id, 13u)) * runOn * (prom ? 1.5 : 1.0);
    vec2 fpc = vec2(fp) / pitch;
    vec2 hs = vec2(0.18, 0.12);
    vec2 cov = 1.0 - smoothstep(hs - 0.5 * fpc, hs + 0.5 * fpc, abs(f));
    float w = lit * cov.x * cov.y;
    w = mix(w, runOn * (prom ? 1.5 : 1.0) * 0.65 * 2.0 * hs.x * cov.y, smoothstep(0.35, 0.9, fpc.x));
    float pOrd = 0.3 + 0.25 * dens;
    float ordMean = pOrd * 0.65 * 4.0 * hs.x * hs.y;
    // promenades: a line every 6 units (the run pattern along x is averaged)
    float promRun = 0.85;
    float promLine = gridLine(uv.y - 0.6, 6.0, 0.29, fp) * 6.0 / 0.29;   // peak ~1 in the line
    float farW = ordMean * 0.8 + promRun * 1.5 * 0.65 * 2.0 * hs.x * min(promLine * 0.29 / 6.0 * (6.0 / 1.2), 1.0) * 0.5;
    w = mix(w, farW, smoothstep(0.4, 1.0, fpc.y));
    float big = 0.6 + 0.8 * vn2(uv / vec2(70.0, 30.0), 15u);
    float meanAll = (ordMean * 0.8 + 0.85 * 1.5 * 0.65 * 4.0 * hs.x * hs.y * 0.2) * big;
    w = mix(w, meanAll, smoothstep(2.4, 4.0, fp));
    float cool = step(0.72, h2(id, 14u));
    vec3 c = mix(gWinA, gWinB, mix(cool, 0.28, smoothstep(0.35, 0.9, fpc.x)));
    return c * w * 3.6;
}

// Shadow toward the sun from the superstructure, the spire and the heightfield.
float sunShadow(vec3 p, bool onDeck) {
    vec3 ird = 1.0 / gSun;
    vec3 n;
    vec2 t = boxT(p, ird, vec3(-gW2, 0.0, ZW), vec3(gW2, gH2, ZW + gLen2), n);
    float s = (t.x < t.y && t.y > 0.0) ? 0.0 : 1.0;
    t = boxT(p, ird, vec3(gSpire.x - 3.0, 0.0, gSpire.y - 3.0), vec3(gSpire.x + 3.0, 80.0, gSpire.y + 3.0), n);
    if (t.x < t.y && t.y > 0.0) s = 0.0;
    if (onDeck && s > 0.0) {
        vec2 lxz = normalize(gSun.xz);
        float slope = gSun.y / length(gSun.xz);
        for (int i = 0; i < LV_SHADOW; i++) {
            float d = 3.0 * pow(2.2, float(i));
            vec2 q = p.xz + lxz * d;
            if (abs(q.x) < W1 && q.y > Z_DECK && q.y < ZW) {
                float h = cellHeight(ivec2(floor(q / CELL)));
                s = min(s, smoothstep(-0.6, 0.6, p.y + d * slope - h));
            }
        }
    }
    return s;
}

vec3 hullLight(vec3 n, vec3 rd, float sh, float seam) {
    float ndl = max(dot(n, gSun), 0.0) * sh;
    vec3 lit = gSunCol * ndl + gFillCol * max(dot(n, gFillDir), 0.0) + vec3(0.016, 0.018, 0.026) * (1.0 + 1.5 * max(n.y, 0.0));
    vec3 rf = reflect(rd, n);
    float sp = pow(max(dot(rf, gSun), 0.0), 40.0) * sh * (1.0 - 0.6 * seam);
    return lit + gSunCol * sp * 0.6;
}

// A hull surface: panels and seams, windows on vertical faces, lights on decks.
vec3 shadeHull(vec3 ro, vec3 rd, float t, vec3 n, int mat, float id, float tNow) {
    vec3 p = ro + rd * t;
    float fp = t * gPix / max(abs(dot(n, rd)), 0.2);
    bool vert = abs(n.y) < 0.5;
    vec2 fuv = !vert ? p.xz : (abs(n.x) > 0.5 ? vec2(p.z, p.y) : vec2(p.x, p.y));
    vec2 per = vert ? vec2(8.0, 3.6) : vec2(4.0, 4.0);
    float ph = h2(ivec2(floor(fuv / per)) + ivec2(int(id) * 131, 0), 31u);
    ph = mix(ph, 0.5, smoothstep(0.15 * per.y, 0.5 * per.y, fp));
    float seam = max(gridLine(fuv.x, per.x, 0.14, fp), gridLine(fuv.y, per.y, 0.12, fp));
    // module patchwork: plate batches of different age, readable from far away
    float mp = h2(ivec2(floor(fuv / 24.0)) + ivec2(int(id) * 77, 0), 34u);
    mp = mix(mp, 0.5, smoothstep(6.0, 16.0, fp));
    float alb = mix(0.045, 0.12, ph) * mix(0.55, 1.35, mp) * (1.0 - 0.45 * seam);
    vec3 emis = vec3(0.0);
    bool deck = mat == M_HF_TOP || mat == M_HF_SIDE || mat == M_FLAT || mat == M_MOD_TOP;
    float sh = sunShadow(p + n * 0.05, deck);

    if (vert) {
        float dens = (mat == M_WALL) ? 1.0 : 0.4;
        float wh = 1.0;
        emis += windows(fuv + vec2(id * 57.0, 0.0), fp, dens) * wh;
        // a chain of marker lights just under the top edge of every hull block
        if (mat == M_HULL) {
            float dy = abs(p.y - (hBmax.y - 1.5));
            float r = 0.25 + 0.8 * fp;
            float kx = floor(fuv.x / 6.0 + 0.5);
            vec2 d = vec2(fuv.x - kx * 6.0, dy);
            emis += gWinA * exp(-dot(d, d) / (r * r)) * (0.25 * 0.25) / (r * r) * 5.0;
        }
    } else if (n.y > 0.5) {
        // sparse deck lights at cell centres
        vec2 c = fuv / 4.0;
        vec2 f = fract(c) - 0.5;
        float on = step(0.9, h2(ivec2(floor(c)), 32u));
        float r = 0.06 + 0.5 * fp / 4.0;
        emis += gWinA * on * exp(-dot(f, f) / (r * r)) * (0.06 * 0.06) / (r * r) * 4.0;
        // flat decks away from the relief: rows of lights along the hull
        if (mat == M_HULL) {
            float dx = abs(fract(p.x / 20.0 + 0.5) - 0.5) * 20.0;
            float kz = floor(p.z / 3.0 + 0.5);
            vec2 d = vec2(dx, p.z - kz * 3.0);
            float rr = 0.2 + 0.8 * fp;
            float rowOn = step(0.4, h2(ivec2(floor(p.x / 20.0 + 0.5), int(floor(p.z / 60.0))), 35u));
            emis += gWinA * rowOn * exp(-dot(d, d) / (rr * rr)) * (0.2 * 0.2) / (rr * rr) * 8.0;
        }
        // runway guide lights: two rows at XL +- 20, a chase toward the bay (0.5 Hz each)
        if (deck && abs(p.x - XL) < 24.0 && p.z < ZW) {
            float side = p.x < XL ? XL - 20.0 : XL + 20.0;
            float kz = floor(p.z / 10.0 + 0.5);
            vec2 d = vec2(p.x - side, p.z - kz * 10.0);
            float rr = 0.3 + 0.7 * fp;
            float pulse = 0.35 + 0.65 * blink(tNow - kz * 0.25, 0.5, 0.0);
            emis += vec3(1.0, 0.72, 0.42) * pulse * exp(-dot(d, d) / (rr * rr)) * (0.3 * 0.3) / (rr * rr) * 6.0;
        }
    }
    // stern: idle engine nozzles
    if (id == 6.0 && n.z < -0.5) {
        for (int i = 0; i < 3; i++) {
            vec2 c = vec2((float(i) - 1.0) * 0.5 * gW0, 0.5 * (gYB0 + 15.0 - gDH + 20.0));
            float r = length(p.xy - c) / (0.22 * gW0);
            emis += vec3(0.55, 0.75, 1.0) * (smoothstep(1.0, 0.7, r) * (0.4 + 0.6 * r * r)) * 1.2;
        }
    }
    // spire beacon
    if (id == 9.0 && p.y > 76.0) emis += vec3(1.0, 0.2, 0.1) * blink(tNow, 0.5, 0.3) * 4.0;

    vec3 col = alb * gTint * hullLight(n, rd, sh, seam) + emis;

    // Sun glint on box edges that face the sun: a thin lit rim outlines the hull.
    if (mat == M_HULL || mat == M_WALL) {
        vec3 dmin = p - hBmin;
        vec3 dmax = hBmax - p;
        vec3 an = abs(n);
        float w = 0.3 + 1.5 * fp;
        vec3 gl = (vec3(1.0) - an) * (exp(-dmin / w) * max(-gSun, 0.0) + exp(-dmax / w) * max(gSun, 0.0));
        col += gSunCol * 0.18 * (gl.x + gl.y + gl.z) * sh;
    }
    return col;
}

// ---------------------------------------------------------------
// Sky: stars, planet, sun
// ---------------------------------------------------------------
vec3 stars(vec3 rdw) {
    vec3 a = abs(rdw);
    vec2 uv;
    int face;
    float ma;
    if (a.x >= a.y && a.x >= a.z) { uv = rdw.yz / a.x; face = rdw.x > 0.0 ? 0 : 1; ma = a.x; }
    else if (a.y >= a.z) { uv = rdw.xz / a.y; face = rdw.y > 0.0 ? 2 : 3; ma = a.y; }
    else { uv = rdw.xy / a.z; face = rdw.z > 0.0 ? 4 : 5; ma = a.z; }
    vec3 col = vec3(0.0);
    for (int l = 0; l < 2; l++) {
        float dens = l == 0 ? 70.0 : 210.0;
        vec2 g = uv * dens;
        ivec2 id = ivec2(floor(g)) + ivec2(face * 4099 + l * 50021, 0);
        vec2 f = fract(g);
        float h = h2s(id, 51u, gRunSeed);
        vec2 pos = 0.25 + 0.5 * vec2(h2s(id, 52u, gRunSeed), h2s(id, 53u, gRunSeed));
        float px = gPix * dens / ma;
        float r = max(px * 0.75, 0.02);
        vec2 d = f - pos;
        float dn = clamp(uLv2.w, 0.25, 2.0);
        float b = l == 0 ? pow(smoothstep(1.0 - 0.14 * dn, 1.0, h), 3.0) * 1.6 : pow(smoothstep(1.0 - 0.28 * dn, 1.0, h), 4.0) * 0.35;
        float tint = h2s(id, 54u, gRunSeed);
        vec3 tc = mix(vec3(1.0, 0.82, 0.66), vec3(0.72, 0.84, 1.0), tint);
        col += tc * b * exp(-dot(d, d) / (r * r));
    }
    // a faint band of the galaxy
    vec3 bn = normalize(vec3(0.3, 0.8, -0.5));
    float bd = dot(rdw, bn);
    float band = exp(-bd * bd * 14.0);
#if NCZ_QUALITY > 0
    band *= 0.45 + 0.9 * vn3(rdw * 7.0, 55u);
#endif
    col += vec3(0.020, 0.018, 0.026) * band;
    return col;
}

vec3 sky(vec3 ro, vec3 rd, vec3 rdw, float sunVis) {
    vec3 col = stars(rdw);
    // planet
    vec3 oc = ro - gPlanetC;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - gPlanetR * gPlanetR;
    float disc = b * b - c;
    float closest = sqrt(max(dot(oc, oc) - b * b, 0.0));
    if (gPlanetOn < 0.5) {
        float sd = dot(rd, gSun);
        col += gSunCol * smoothstep(0.99994, 0.99997, sd) * 6.0 * sunVis;
    } else if (disc > 0.0 && -b - sqrt(disc) > 0.0) {
        float t = -b - sqrt(disc);
        vec3 n = normalize(oc + rd * t);
        float ndl = dot(n, gSun);
        float land = vn3(n * 3.0, 61u);
        float cl = vn3(n * 9.0 + vec3(0.0, 0.0, 0.002 * iTime), 62u);
#if LV_OCT > 1
        land = 0.65 * land + 0.35 * vn3(n * 7.0, 63u);
        cl = 0.6 * cl + 0.4 * vn3(n * 21.0, 64u);
#endif
        vec3 base = mix(gPlanetA, gPlanetB, smoothstep(0.45, 0.6, land));
        base = mix(base, vec3(0.85), smoothstep(0.52, 0.72, cl) * 0.8);
        float day = smoothstep(-0.08, 0.25, ndl);
        col = base * (max(ndl, 0.0) * 1.1 + 0.004) * gSunCol * 0.45 * day + base * 0.002;
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col += vec3(0.25, 0.45, 0.9) * rim * smoothstep(-0.2, 0.3, ndl) * 0.6;
    } else {
        float hh = (closest - gPlanetR) / (0.035 * gPlanetR);
        if (hh > 0.0 && b < 0.0) {
            vec3 pn = normalize(oc + rd * (-b));
            col += vec3(0.25, 0.45, 0.9) * exp(-hh * 2.5) * smoothstep(-0.25, 0.3, dot(pn, gSun)) * 0.35;
        }
        float sd = dot(rd, gSun);
        col += gSunCol * smoothstep(0.99994, 0.99997, sd) * 6.0 * sunVis;
    }
    return col;
}

float sunVisibility(vec3 ro, float t) {
    float keepT = hT; vec3 keepN = hN; int keepM = hM; float keepId = hId;
    hT = 1e9; hM = M_NONE;
    sceneBoxes(ro, gSun, 1.0 / nz(gSun), t, false);
    float v = hM == M_NONE ? 1.0 : 0.0;
    hT = keepT; hN = keepN; hM = keepM; hId = keepId;
    return v;
}

// ---------------------------------------------------------------
// Interior: the hall behind the aperture
// ---------------------------------------------------------------
// A hangar far wider than it is tall (a symmetric tube read as "concentric
// rings" in the first render): flat floor 22 below the aperture centre, ceiling
// 46 above, side walls at +-120, chamfered upper corners. Ribs are arches over
// walls and ceiling, never across the floor; the floor carries machinery and
// stores as a grid of blocks with real relief.
const float IF_Y = -22.0;
const float IC_Y = 46.0;
const float IW_X = 120.0;
const float ICELL = 6.0;

vec3 hallPlane(int k) {
    if (k == 0) return vec3(0.0, -1.0, -IF_Y);
    if (k == 1) return vec3(0.0, 1.0, IC_Y);
    if (k == 2) return vec3(1.0, 0.0, IW_X);
    if (k == 3) return vec3(-1.0, 0.0, IW_X);
    if (k == 4) return vec3(0.6, 0.8, 84.8);
    return vec3(-0.6, 0.8, 84.8);
}
float archD(vec2 q) {
    float d = q.y - IC_Y;
    d = max(d, abs(q.x) - IW_X);
    return max(d, dot(vec2(abs(q.x), q.y), vec2(0.6, 0.8)) - 84.8);
}
vec2 archN(vec2 q) {
    float a = q.y - IC_Y;
    float b = abs(q.x) - IW_X;
    float c = dot(vec2(abs(q.x), q.y), vec2(0.6, 0.8)) - 84.8;
    if (a >= b && a >= c) return vec2(0.0, 1.0);
    if (b >= c) return vec2(sign(q.x), 0.0);
    return vec2(0.6 * sign(q.x), 0.8);
}

vec3 shaftCol() { return mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.72, 0.45), gFoundry); }

// Shaft k: a slot in the ceiling at z = (k + 0.5) SH_P, x = xc +- SH_X, its beam
// tilted forward by SH_TAN. Returns (on * strength, xc).
vec2 shaftInfo(float k) {
    float on = step(0.25, h1(k, 33u)) * (0.6 + 0.4 * h1(k, 34u));
    float xc = (h1(k, 35u) > 0.5 ? 1.0 : -1.0) * (22.0 + 45.0 * h1(k, 36u));
    return vec2(on, xc);
}
float shaftLit(vec3 p) {
    float g = p.z - (IC_Y - p.y) * SH_TAN;
    float k = floor(g / SH_P);
    if (k < 0.0) return 0.0;
    vec2 si = shaftInfo(k);
    float dz = abs(g - (k + 0.5) * SH_P);
    return si.x * (1.0 - smoothstep(SH_W - 0.6, SH_W + 0.6, dz)) * (1.0 - smoothstep(SH_X - 1.0, SH_X + 1.0, abs(p.x - si.y)));
}

vec3 shafts(vec3 ro, vec3 rd, float th) {
    float g0 = ro.z - (IC_Y - ro.y) * SH_TAN;
    float gd = rd.z + rd.y * SH_TAN;
    if (abs(gd) < 1e-4) gd = 1e-4;
    float dir = gd > 0.0 ? 1.0 : -1.0;
    float k0 = dir > 0.0 ? max(floor(g0 / SH_P), 0.0) : floor(g0 / SH_P);
    float acc = 0.0;
    for (int i = 0; i < LV_SHAFTS; i++) {
        float k = k0 + float(i) * dir;
        if (k < 0.0) continue;
        vec2 si = shaftInfo(k);
        float tx0 = 0.0;
        float tx1 = th;
        if (abs(rd.x) > 1e-5) {
            float a = (si.y - SH_X - ro.x) / rd.x;
            float b = (si.y + SH_X - ro.x) / rd.x;
            tx0 = max(tx0, min(a, b));
            tx1 = min(tx1, max(a, b));
        } else if (abs(ro.x - si.y) > SH_X) {
            continue;
        }
        float zc = (k + 0.5) * SH_P;
        float ta = (zc - SH_W - g0) / gd;
        float tb = (zc + SH_W - g0) / gd;
        float t0 = max(min(ta, tb), tx0);
        float t1 = min(max(ta, tb), tx1);
        float len = max(t1 - t0, 0.0);
        float ym = ro.y + rd.y * 0.5 * (t0 + t1);
        float fall = 0.3 + 0.7 * smoothstep(IF_Y, IC_Y, ym);
#if NCZ_QUALITY == 2
        // density modulation along the beam: slow drifting haze
        fall *= 0.7 + 0.6 * vn2(vec2(ym * 0.15, k * 3.1 + iTime * 0.05), 37u);
#endif
        acc += len * si.x * fall;
    }
    return shaftCol() * acc * 0.010;
}

// Dust motes on planes every DZ ahead (three depths), brighter inside a shaft.
vec3 dust(vec3 ro, vec3 rd, float th) {
    vec3 acc = vec3(0.0);
    if (rd.z < 0.05) return acc;
    const float DZ = 6.0;
    const float CW = 2.5;
    float base = max(floor(ro.z / DZ), 0.0);
    for (int i = 1; i <= LV_DUST; i++) {
        float li = base + float(i);
        float tl = (li * DZ - ro.z) / rd.z;
        if (tl > th) break;
        vec2 q = ro.xy + rd.xy * tl;
        ivec2 id = ivec2(floor(q / CW)) + ivec2(int(li) * 7919, 0);
        float h = h2(id, 41u);
        vec2 pos = (vec2(floor(q / CW)) + 0.2 + 0.6 * vec2(h2(id, 42u), h2(id, 43u))) * CW;
        vec2 f = q - pos;
        float r = max(0.03, tl * gPix * 0.9);
        float fade = smoothstep(4.0, 8.0, tl) * step(0.55, h);
        float lit = 0.35 + 2.5 * shaftLit(vec3(pos, li * DZ));
        acc += vec3(0.9, 0.85, 0.75) * fade * lit * exp(-dot(f, f) / (r * r)) * (0.03 * 0.03) / (r * r) * 0.8;
    }
    return acc;
}

// Panel lamps: machinery lights on a surface with face coordinates fuv.
vec3 lamps(vec2 fuv, vec2 per, float fp, float blur, float tNow, uint salt) {
    vec2 pc = fuv / per;
    ivec2 pid = ivec2(floor(pc));
    vec2 f = (fract(pc) - 0.5) * per;
    float on = step(0.78, h2(pid, salt));
    float hue = h2(pid, salt + 1u);
    vec3 lc = hue < 0.5 ? vec3(1.0, 0.62, 0.30) : (hue < 0.8 ? vec3(0.45, 0.85, 1.0) : vec3(1.0, 0.25, 0.18));
    float breathe = h2(pid, salt + 2u) > 0.85 ? 0.5 + 0.5 * sin(TAU * (0.2 + 0.4 * h2(pid, salt + 3u)) * tNow) : 1.0;
    vec2 ls = vec2(0.5, 0.22);
    vec2 lcov = 1.0 - smoothstep(ls - 0.5 * fp, ls + 0.5 * fp, abs(f));
    float lamp = mix(lcov.x * lcov.y, 4.0 * ls.x * ls.y / (per.x * per.y), smoothstep(1.0, 3.0, fp));
    float lineU = (1.0 - smoothstep(ls.y - 0.5 * fp, ls.y + 0.5 * fp, abs(f.y))) * 0.25;
    return lc * on * breathe * mix(lamp, lineU, blur) * 2.2;
}

vec3 interior(vec3 ro, vec3 rd, float blur, float tNow) {
    float shrink = 1.0 - blur;
    // hall walls
    float tw = 4000.0;
    int wk = 1;
    for (int k = 0; k < 6; k++) {
        vec3 pl = hallPlane(k);
        float dn = dot(rd.xy, pl.xy);
        if (dn > 1e-4) {
            float tt = (pl.z - dot(ro.xy, pl.xy)) / dn;
            if (tt < tw) { tw = tt; wk = k; }
        }
    }
    // ribs: arches every RIBP, heavy every 4th, a spar every 12th
    float tr = tw;
    int rib = 0;
    float ribK = 0.0;
    float ribDepth = 0.0;
    vec2 ribN = vec2(0.0);
    if (rd.z > 0.03 && shrink > 0.02) {
        float k0 = max(floor(ro.z / RIBP) + 1.0, 1.0);
        for (int i = 0; i < LV_RIBS; i++) {
            float kz = k0 + float(i);
            float tk = (kz * RIBP - ro.z) / rd.z;
            if (tk > tr) break;
            bool heavy = mod(kz, 4.0) < 0.5;
            float depth = (heavy ? 12.0 : 6.0) * shrink;
            float w = heavy ? 5.0 : 2.5;
            vec2 q = ro.xy + rd.xy * tk;
            if (archD(q) > -depth && q.y > IF_Y) { tr = tk; rib = 1; ribK = kz; ribDepth = depth; break; }
            if (heavy && mod(kz, 12.0) < 0.5 && abs(q.y - 30.0) < 2.0 * shrink && abs(q.x) < IW_X) {
                tr = tk; rib = 3; ribK = kz; break;
            }
            float tb = tk + w / rd.z;
            if (tb < tr) {
                vec2 q2 = ro.xy + rd.xy * tb;
                if (archD(q2) > -depth && q2.y > IF_Y) {
                    tr = tk + 0.5 * w / rd.z; rib = 2; ribK = kz; ribDepth = depth;
                    ribN = -archN(q2);
                    break;
                }
            }
        }
    }
    float th = min(tr, tw);
    // floor relief
    int fl = 0;
    vec3 fn = vec3(0.0, 1.0, 0.0);
    if (rd.y < 0.0) {
        float ttop = (IF_Y + 11.0 * shrink + 0.01 - ro.y) / rd.y;
        if (ttop < th) {
            float tt;
            vec3 nn;
            ivec2 cc;
            int st = traceGrid(ro, rd, max(ttop, 0.0), th, vec3(0.0, 1.0, 0.0), 2, shrink, tt, nn, cc);
            if (st == 1) { th = tt; fl = 1; fn = nn; rib = 0; }
            else if (st == 2) {
                float tp = (IF_Y - ro.y) / rd.y;
                if (tp < th) { th = tp; fl = 2; rib = 0; }
            }
        }
    }
    vec3 p = ro + rd * th;
    float fp = th * gPix;
    vec3 col;
    vec3 amber = vec3(1.0, 0.62, 0.30);
    vec3 cyan = vec3(0.45, 0.85, 1.0);
    if (fl > 0) {
        bool top = fn.y > 0.5;
        float fpn = fp / max(abs(dot(fn, rd)), 0.2);
        vec2 fuv = top ? p.xz : (abs(fn.x) > 0.5 ? vec2(p.z, p.y) : vec2(p.x, p.y));
        float seam = max(gridLine(fuv.x, top ? 6.0 : 3.0, 0.1, fpn), gridLine(fuv.y, top ? 6.0 : 2.0, 0.1, fpn));
        float ph = mix(h2(ivec2(floor(fuv / 3.0)), 44u), 0.5, smoothstep(1.0, 3.0, fpn));
        float alb = mix(0.08, 0.18, ph) * (1.0 - 0.4 * seam);
        float lit = 0.035 + 0.02 * fn.y + 2.2 * shaftLit(p);
        col = vec3(alb * lit);
        if (top && p.y < IF_Y + 0.1 && abs(p.x) < 16.0) {
            // runway lights down the middle of the hall
            float kz = floor(p.z / 4.0 + 0.5);
            vec2 d = vec2(abs(p.x) - 12.0, (p.z - kz * 4.0) * shrink);
            float rr = 0.18 + 0.7 * fpn;
            col += vec3(0.9, 0.8, 0.6) * exp(-dot(d, d) / (rr * rr)) * (0.18 * 0.18) / (rr * rr) * 1.4;
        } else if (!top) {
            col += lamps(fuv, vec2(3.0, 2.0), fpn, blur, tNow, 45u) * 0.8;
        } else {
            vec2 c = fract(p.xz / 3.0) - 0.5;
            float on = step(0.9, h2(ivec2(floor(p.xz / 3.0)), 49u));
            float r = 0.05 + 0.5 * fpn / 3.0;
            col += vec3(0.4, 1.0, 0.6) * on * exp(-dot(c, c) / (r * r)) * (0.05 * 0.05) / (r * r) * 1.5;
        }
    } else if (rib == 1 || rib == 3) {
        bool heavy = mod(ribK, 4.0) < 0.5;
        float d = archD(p.xy);
        float along = p.x + (p.y > 20.0 ? p.y : 0.0);
        float seam = gridLine(along, 6.0, 0.15, fp);
        col = vec3(0.16) * (1.0 - 0.4 * seam) * (0.05 + 1.5 * shaftLit(p));
        if (rib == 1) {
            float e = abs(d + ribDepth);
            float bar = 1.0 - smoothstep(0.3, 0.3 + fp, e);
            if (heavy) col += amber * bar * 1.4;
            float dots = gridLine(along, 3.0, 0.4, fp) * (1.0 - smoothstep(0.5, 0.5 + fp, abs(d + ribDepth - 1.2)));
            if (!heavy) col += cyan * dots * 1.0;
        } else {
            col += amber * gridLine(p.x, 4.0, 0.5, fp) * (1.0 - smoothstep(0.4, 0.4 + fp, abs(p.y - 28.4))) * 1.2;
        }
    } else if (rib == 2) {
        col = vec3(0.12) * (0.03 + 0.05 * max(ribN.y, 0.0));
    } else {
        vec3 pl = hallPlane(wk);
        vec3 n = vec3(-pl.xy, 0.0);
        float u = dot(p.xy, vec2(-pl.y, pl.x));
        float fpn = fp / max(abs(dot(n, rd)), 0.2);
        float seam = max(gridLine(u, 5.5, 0.12, fpn), gridLine(p.z, 8.0, 0.12, fpn * (1.0 + 4.0 * blur)));
        float ph = mix(h2(ivec2(floor(vec2(u / 5.5, p.z / 8.0))) + ivec2(wk * 1000, 0), 36u), 0.5, smoothstep(1.5, 5.0, fpn));
        float alb = mix(0.08, 0.18, ph) * (1.0 - 0.4 * seam);
        col = vec3(alb * (0.03 + 0.02 * max(n.y, 0.0) + 1.5 * shaftLit(p)));
        col += lamps(vec2(u, p.z) + vec2(float(wk) * 131.0, 0.0), vec2(5.5, 8.0), fpn, blur, tNow, 37u);
        if (wk == 1) {
            // the slots the shafts come through
            float g = p.z - (IC_Y - p.y) * SH_TAN;
            float k = floor(g / SH_P);
            vec2 si = shaftInfo(k);
            float dz = abs(g - (k + 0.5) * SH_P);
            float slot = (1.0 - smoothstep(SH_W - fpn, SH_W + fpn, dz)) * (1.0 - smoothstep(SH_X - fpn, SH_X + fpn, abs(p.x - si.y)));
            col += shaftCol() * slot * si.x * 2.2 * step(0.0, k);
        }
    }
    // distance haze toward a warm glow at the far end
    float haze = 1.0 - exp(-th * 0.0022);
    vec3 farCol = mix(vec3(0.045, 0.032, 0.028), vec3(0.09, 0.06, 0.04), blur);
    col = mix(col, farCol, haze);
    col += vec3(1.0, 0.75, 0.45) * pow(max(rd.z, 0.0), 60.0) * 0.25 * haze;
    col += shafts(ro, rd, th);
    col += dust(ro, rd, th);
    // the horizon slit forming ahead as the gate approaches
    col += vec3(1.0, 0.9, 0.8) * blur * blur * exp(-abs(rd.y) * 40.0) * smoothstep(0.0, 0.5, rd.z) * 0.6;
    return col;
}

// ---------------------------------------------------------------
// Camera paths
// ---------------------------------------------------------------
void camExterior(float t, out vec3 pos, out float yaw, out float pitch) {
    if (t < T_HULL) {
        float s = t / T_HULL;
        vec3 P0 = vec3(XL + 900.0, 380.0, -700.0);
        vec3 P1 = vec3(XL, HC, Z1);
        vec3 m0 = (P1 - P0) * 1.2;
        vec3 m1 = vec3(0.0, 0.0, VH * T_HULL);
        float s2 = s * s;
        float s3 = s2 * s;
        pos = (2.0 * s3 - 3.0 * s2 + 1.0) * P0 + (s3 - 2.0 * s2 + s) * m0
            + (-2.0 * s3 + 3.0 * s2) * P1 + (s3 - s2) * m1;
        float e = smoothstep(0.0, 1.0, s);
        yaw = mix(YAW0, 0.0, e);
        pitch = mix(PITCH0, PITCH1, e);
    } else {
        float u = t - T_HULL;
        float settle = 1.0 - smoothstep(T_INT - 30.0, T_INT - 2.0, t);
        pos = vec3(XL + 3.0 * (1.0 - cos(0.05 * u)) * settle,
                   HC + 1.5 * (1.0 - cos(0.037 * u)) * settle,
                   Z1 + VH * u);
        yaw = 0.035 * sin(0.04 * u) * settle;
        pitch = PITCH1 + 0.02 * sin(0.03 * u) * settle;
    }
}

float smoothInt(float u) {
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return u - 0.5;
    return u * u * u - 0.5 * u * u * u * u;
}
// Distance travelled inside the cavern since T_INT: VH, easing to 2.1 VH by 262 s,
// then the run into the gate (to about 12 VH by 276 s).
float cavernZ(float t) {
    float u = t - T_INT;
    float v0 = VH;
    float v1 = 2.1 * VH;
    float v2 = 12.0 * VH;
    return v0 * u + (v1 - v0) * 112.0 * smoothInt(u / 112.0) + (v2 - v1) * 16.0 * smoothInt((t - 258.0) / 16.0);
}

vec3 camBasis(float yaw, float pitch, vec2 uv) {
    vec3 fwd = vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
    vec3 right = normalize(vec3(fwd.z, 0.0, -fwd.x));
    vec3 up = cross(fwd, right);
    return normalize(uv.x * right + uv.y * up + FOCAL * fwd);
}

// ---------------------------------------------------------------
// Acts
// ---------------------------------------------------------------
vec3 actExterior(vec2 uv, float t) {
    vec3 ro;
    float yaw, pitch;
    camExterior(t, ro, yaw, pitch);
    vec2 uvs = vec2(gMirror * uv.x, uv.y);
    vec3 rd = camBasis(yaw, pitch, uvs);
    rd = nz(rd);
    vec3 rdw = vec3(rd.x * gMirror, rd.y, rd.z);
    vec3 ird = 1.0 / rd;

    float sunVis = sunVisibility(ro, t);
    hT = 1e9; hM = M_NONE; hN = vec3(0.0, 1.0, 0.0); hId = 0.0;
    sceneBoxes(ro, rd, ird, t, true);

    // the upper deck: heightfield with real relief near the camera, module
    // blocks further out, a flat textured deck beyond
    vec3 vn;
    vec2 vt = boxT(ro, ird, vec3(-W1, HF_Y0, Z_DECK), vec3(W1, HF_Y1, ZW), vn);
    if (vt.x < vt.y && vt.y > 0.0 && vt.x < hT) {
        float t0 = max(vt.x, 0.0);
        float t1 = min(vt.y, hT);
        float cellPx = CELL / max(t0 * gPix, 1e-6);
        float modPx = MODW / max(t0 * gPix, 1e-6);
        float tt;
        vec3 nn;
        ivec2 cc;
        int st = 2;
        float tc = t0;
        vec3 nEnter = vt.x > 0.0 ? vn : vec3(0.0, 1.0, 0.0);
        bool done = false;
        if (cellPx > 1.5) {
            st = traceGrid(ro, rd, t0, t1, nEnter, 0, 1.0, tt, nn, cc);
            if (st == 1) { hT = tt; hN = nn; hCell = cc; hM = nn.y > 0.5 ? M_HF_TOP : M_HF_SIDE; done = true; }
            else if (st == 0) done = true;
            tc = tt;
        }
        if (!done && modPx > 1.5) {
            st = traceGrid(ro, rd, tc, t1, tc == t0 ? nEnter : vec3(0.0, 1.0, 0.0), 1, 1.0, tt, nn, cc);
            if (st == 1) { hT = tt; hN = nn; hCell = cc; hM = nn.y > 0.5 ? M_MOD_TOP : M_HF_SIDE; done = true; }
            else if (st == 0) done = true;
            tc = tt;
        }
        if (!done && rd.y < 0.0) {
            float tp = (0.2 - ro.y) / rd.y;
            if (tp > tc && tp < t1) { hT = tp; hN = vec3(0.0, 1.0, 0.0); hM = M_FLAT; }
        }
    }

    // the superstructure face: windows, and the aperture
    if (hM == M_HULL && hId == 3.0 && hN.z < -0.5) {
        vec3 p = ro + rd * hT;
        vec2 q = abs(p.xy - BAYC.xy);
        hM = (q.x < BAYH.x && q.y < BAYH.y) ? M_AP : M_WALL;
    }

    vec3 col;
    if (hM == M_NONE) {
        col = sky(ro, rd, rdw, sunVis);
    } else if (hM == M_AP) {
        col = interior(ro - BAYC, rd, 0.0, t);
    } else if (hM == M_CRAFT) {
        vec3 p = ro + rd * hT;
        col = vec3(0.35) * hullLight(hN, rd, sunShadow(p, false), 0.0);
    } else {
        col = shadeHull(ro, rd, hT, hN, hM, hId, t);
        if (hM == M_WALL) {
            // aperture rim: a lit frame and approach lights around the bay
            vec3 p = ro + rd * hT;
            vec2 q = abs(p.xy - BAYC.xy) - BAYH;
            float fr = max(q.x, q.y);
            float fp = hT * gPix / max(abs(rd.z), 0.2);
            col += vec3(1.0, 0.85, 0.65) * (1.0 - smoothstep(0.6, 0.6 + fp, abs(fr - 0.6))) * 1.5;
            float kk = floor((p.x + p.y) / 4.0);
            col += vec3(1.0, 0.3, 0.2) * (1.0 - smoothstep(0.35, 0.35 + fp, abs(fr - 3.0)))
                 * gridLine(p.x + p.y, 4.0, 1.2, fp) * blink(t, 0.5, 0.1 * mod(kk, 5.0)) * 3.0;
        }
    }
    float tOcc = hM == M_NONE ? 1e9 : hT;
    // craft running lights (white nose, red tail blinking at 0.7 Hz)
    for (int i = 0; i < 4; i++) {
        vec3 c = craftPos(i, t);
        col += vec3(1.0, 0.95, 0.9) * glow(ro, rd, c + vec3(0.0, 0.0, 2.8), tOcc + 0.5, 0.35) * 30.0;
        col += vec3(1.0, 0.15, 0.1) * glow(ro, rd, c + vec3(0.0, 0.7, -2.4), tOcc + 0.5, 0.3)
             * blink(t, 0.7, 0.23 * float(i)) * 40.0;
    }
    // nav strobes along the deck edges: red to port, green to starboard, 0.5 Hz
    for (int s = 0; s < 2; s++) {
        float ex = s == 0 ? -W1 : W1;
        // closest approach of the ray to the edge line x = ex, y = 0.6
        vec2 o = ro.xy - vec2(ex, 0.6);
        float dd = dot(rd.xy, rd.xy);
        float tl = dd > 1e-6 ? -dot(o, rd.xy) / dd : 0.0;
        float z = ro.z + rd.z * max(tl, 0.0);
        float k = floor(z / 40.0 + 0.5);
        if (k * 40.0 > Z_DECK && k * 40.0 < ZW) {
            vec3 lp = vec3(ex, 0.6, k * 40.0);
            vec3 lc = s == 0 ? vec3(1.0, 0.15, 0.1) : vec3(0.2, 1.0, 0.35);
            col += lc * glow(ro, rd, lp, tOcc + 1.0, 0.4) * blink(t, 0.5, 0.05 * k) * 40.0;
        }
    }
    // lens glare of the sun (an analytic stand-in for bloom)
    float sd = max(dot(rd, gSun), 0.0);
    col += gSunCol * (pow(sd, 400.0) * 0.5 + pow(sd, 24.0) * 0.025) * sunVis;

    // fade in from the stars at the start of the chapter
    float fade = smoothstep(0.5, 6.0, t);
    return mix(stars(rdw), col, fade);
}

vec3 actInterior(vec2 uv, float t, float blur) {
    float z = cavernZ(t);
    float u = t - T_INT;
    vec3 ro = vec3(-18.0 * sin(0.012 * u) * (1.0 - blur), -4.0 + 3.0 * smoothstep(0.0, 60.0, u), z);
    float yaw = 0.03 * sin(0.017 * u) * (1.0 - blur);
    float pitch = PITCH1 * (1.0 - smoothstep(0.0, 20.0, u)) + 0.015 * sin(0.013 * u);
    vec2 uvs = vec2(gMirror * uv.x, uv.y);
    vec3 rd = camBasis(yaw, pitch, uvs);
    rd = nz(rd);
    return interior(ro, rd, blur, t);
}

// The gate: two planes of streaks above and below a horizon slit. Built in
// display space with capped OKLab lightness so the luminance limits hold.
vec3 actGate(vec2 uv, float t) {
    float tg = t - T_GATE;
    float y = uv.y - 0.015 * sin(tg * 0.021);      // the slit drifts very slowly
    float ay = max(abs(y), 0.004);
    float u = uv.x / ay;
    float v = 1.0 / ay + tg * 5.5;
    float side = y > 0.0 ? 1.0 : 0.0;
    float n1 = vn2(vec2(u * 2.2 + side * 37.0, v * 0.05), 71u);
    float n2 = vn2(vec2(u * 7.0 + side * 91.0, v * 0.16), 72u);
    float s = 0.62 * n1 + 0.38 * n2;
    float fpv = gPix / (ay * ay) * 0.16;
    float fpu = gPix * (1.0 + abs(u)) / ay * 7.0;
    float contrast = 1.0 - smoothstep(0.4, 1.2, max(fpv, fpu));
    // dissolve into colour fields toward T_FIELDS
    float diss = smoothstep(T_FIELDS - 10.0, T_FIELDS + 2.0, t);
    s = mix(0.45, s, contrast * (1.0 - diss));
    float hue = gHue0 + 0.55 * vn2(vec2(u * 0.25 + side * 13.0, v * 0.004), 73u) * TAU + TAU * 0.03 * tg;
    float gi = clamp(uLv2.z, 0.3, 1.0);
    float L = 0.30 + 0.46 * gi * smoothstep(0.2, 0.85, s);
    float C = (0.10 + 0.06 * s) * (0.6 + 0.4 * gi);
    vec3 c = toDisplay(lch(L, C, hue));
    // brighter where the flow is fast (near the frame edges), dimmer near the slit
    c *= 0.82 + 0.18 * smoothstep(0.0, 0.35, ay);
    // the slit
    float slit = exp(-ay * ay / 0.0009);
    c = mix(c, toDisplay(lch(0.9, 0.03, gHue0 + 0.5)), slit * 0.8 * clamp(uLv2.z, 0.3, 1.0) * (1.0 - diss));
    return c;
}

// Colour fields and drifting soft panels (display space).
vec3 actFields(vec2 uv, float t) {
    float tf = t - T_FIELDS;
    vec2 ab = vec2(0.0);
    float wsum = 0.0;
    float L = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        vec2 c = vec2(0.6 * sin(0.013 * tf + fi * 1.7), 0.35 * cos(0.017 * tf + fi * 2.3));
        float r = 0.55 + 0.25 * sin(fi * 3.1 + 0.01 * tf);
        float w = exp(-dot(uv - c, uv - c) / (r * r));
        float h = gHue0 + fi * 1.4 + TAU * 0.02 * tf;
        ab += w * vec2(cos(h), sin(h));
        L += w * (0.44 + 0.12 * fi / 3.0);
        wsum += w;
    }
    ab /= max(wsum, 1e-3);
    L /= max(wsum, 1e-3);
    // soft panels
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        vec2 c = vec2(0.7 * sin(0.011 * tf + fi * 2.5), 0.3 * sin(0.015 * tf + fi * 1.3));
        vec2 hs = vec2(0.22 + 0.08 * fi, 0.13 + 0.05 * fi);
        vec2 q = abs(uv - c) - hs;
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        float m = 1.0 - smoothstep(-0.02, 0.06, d);
        L += 0.08 * m;
        float h = gHue0 + 3.0 + fi * 0.9 + TAU * 0.02 * tf;
        ab = mix(ab, vec2(cos(h), sin(h)), 0.35 * m);
    }
    L = clamp(L, 0.28, 0.76);
    float C = 0.11 * clamp(length(ab) * 1.3, 0.3, 1.0);
    float h = atan(ab.y, ab.x);
    return toDisplay(lch(L, C, h));
}

#if LV_CODA
float codaH(vec2 p) {
    float h = 0.0;
    float a = 1.0;
    vec2 q = p * 0.3;
    for (int i = 0; i < LV_OCT; i++) {
        h += a * (1.0 - abs(vn2(q, 80u + uint(i)) * 2.0 - 1.0));
        q = q * 2.03 + 1.7;
        a *= 0.45;
    }
    return h * 2.2 - 1.6 + 0.8 * vn2(p * 0.06, 83u);
}
// False-colour terrain, the gate's coda (display space, capped lightness).
vec3 actCoda(vec2 uv, float t) {
    float tc = t - T_CODA;
    vec3 ro = vec3(0.0, 4.5, tc * 3.0);
    vec3 rd = normalize(vec3(uv.x, uv.y - 0.3, 1.0));
    vec3 skyC = toDisplay(lch(0.30 + 0.12 * uv.y, 0.08, gHue0 + 2.2 + TAU * 0.02 * tc));
    if (rd.y > -0.01) return skyC;
    float tt = 0.5;
    bool hit = false;
    for (int i = 0; i < LV_CODA_STEPS; i++) {
        vec3 p = ro + rd * tt;
        float h = codaH(p.xz);
        if (p.y < h) { hit = true; break; }
        tt += max(0.05 + 0.01 * tt, (p.y - h) * 0.45);
        if (tt > 60.0) break;
    }
    if (!hit) return skyC;
    vec3 p = ro + rd * tt;
    float h = codaH(p.xz);
    float e = 0.1;
    vec3 n = normalize(vec3(h - codaH(p.xz + vec2(e, 0.0)), e, h - codaH(p.xz + vec2(0.0, e))));
    float shade = 0.5 + 0.5 * clamp(dot(n, normalize(vec3(0.5, 1.0, 0.3))), 0.0, 1.0);
    float band = abs(fract(h * 1.3) - 0.5) * 2.0;           // solarised: lightness folds back
    float L = 0.28 + 0.46 * mix(band, 0.5, smoothstep(10.0, 40.0, tt)) * shade;
    float hue = gHue0 + 1.6 * h + TAU * 0.02 * tc;
    vec3 c = toDisplay(lch(L, 0.13, hue));
    return mix(c, skyC, smoothstep(25.0, 60.0, tt));
}
#endif

// The quiet chamber: still, low contrast, lit evenly from below. It thins from
// the ceiling down, softly, back to the stars; the camera ends where the next
// chapter begins, so the stars do not jump.
vec3 actQuiet(vec2 uv, float t, float tEnd) {
    float u = t - T_QUIET;
    float yaw = gMirrorNext * YAW0 - 0.018 * (tEnd - t);
    vec3 ro = vec3(2.0 * sin(0.013 * u), 6.5, -3.0 + 2.0 * cos(0.011 * u));
    vec3 rd = camBasis(yaw, PITCH0, uv);
    rd = nz(rd);
    vec3 bmax = vec3(28.0, 22.0, 28.0);
    vec3 bmin = vec3(-28.0, 0.0, -28.0);
    vec3 tx = (mix(bmin, bmax, step(0.0, rd)) - ro) / rd;
    float th = min(min(tx.x, tx.y), tx.z);
    vec3 p = ro + rd * th;
    vec3 col;
    vec2 fuv;
    vec3 warmW = vec3(1.0, 0.96, 0.9);
    if (th == tx.y && rd.y < 0.0) {
        float fp = th * gPix / max(-rd.y, 0.2);
        fuv = p.xz;
        float seam = max(gridLine(p.x, 4.0, 0.08, fp), gridLine(p.z, 4.0, 0.08, fp));
        float e = 0.2 * (0.95 + 0.05 * h2(ivec2(floor(p.xz / 4.0)), 91u)) * (1.0 - 0.3 * seam);
        col = warmW * e;
    } else if (th == tx.y) {
        float fp = th * gPix / max(rd.y, 0.2);
        fuv = p.xz;
        col = warmW * (0.012 + 0.006 * max(gridLine(p.x, 7.0, 0.1, fp), gridLine(p.z, 7.0, 0.1, fp)));
    } else {
        float a = th == tx.x ? p.z : p.x;
        float fp = th * gPix / 0.4;
        fuv = vec2(a, p.y) + (th == tx.x ? 0.0 : 300.0);
        float seam = max(gridLine(a, 4.0, 0.06, fp), gridLine(p.y, 5.5, 0.06, fp));
        float light = 0.13 * exp(-p.y / 8.0) + 0.01;
        col = warmW * light * (0.95 + 0.05 * h2(ivec2(floor(vec2(a / 4.0, p.y / 5.5))), 92u)) * (1.0 - 0.25 * seam);
    }
    float ft = tEnd - 15.0 + 7.0 * (1.0 - p.y / 22.0) + 2.5 * vn2(fuv * 0.08, 93u);
    float alpha = 1.0 - smoothstep(ft, ft + 5.0, t);
    return mix(stars(rd), col, alpha);
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    gPix = 1.0 / (iResolution.y * FOCAL);

    // arc time: wall time / arc scale; chapter length in arc seconds (>= 380)
    float arcScale = clamp(uLv2.x, 0.7, 2.0);   // >= 0.7 keeps every blink <= 1 Hz (fastest is 0.7 Hz arc)
    float chapLen = uLv1.w >= 380.0 ? min(uLv1.w, 900.0) : 390.0 + 30.0 * iSeed.z;
    float at = iTime / arcScale;
    float chap = floor(at / chapLen);
    float t = at - chap * chapLen;
    setChapter(chap);

    vec3 col;
    if (t < T_INT) {
        col = aces(actExterior(uv, t));
    } else if (t < T_GATE + 6.0) {
        float expo = mix(1.0, 2.6, smoothstep(T_INT, T_INT + 3.0, t));
        float blur = smoothstep(250.0, 272.0, t);
        col = aces(actInterior(uv, t, blur) * expo);
        if (t > T_GATE - 2.0) col = mix(col, actGate(uv, t), smoothstep(T_GATE - 2.0, T_GATE + 6.0, t));
    } else if (t < T_FIELDS + 2.0) {
        col = actGate(uv, t);
        if (t > T_FIELDS - 8.0) col = mix(col, actFields(uv, t), smoothstep(T_FIELDS - 8.0, T_FIELDS + 2.0, t));
    } else if (t < T_QUIET - 4.0) {
#if LV_CODA
        col = actFields(uv, t);
        if (t > T_CODA - 3.0) col = mix(col, actCoda(uv, t), smoothstep(T_CODA - 3.0, T_CODA + 3.0, t));
#else
        col = actFields(uv, t);
#endif
    } else {
        vec3 q = aces(actQuiet(uv, t, chapLen));
        if (t < T_QUIET + 4.0) {
#if LV_CODA
            vec3 prev = actCoda(uv, t);
#else
            vec3 prev = actFields(uv, t);
#endif
            q = mix(prev, q, smoothstep(T_QUIET - 4.0, T_QUIET + 4.0, t));
        }
        col = q;
    }
    // gate caps: peak display value 0.85 while the gate family is on screen
    float gateW = smoothstep(T_GATE - 2.0, T_GATE + 6.0, t) * (1.0 - smoothstep(T_QUIET - 4.0, T_QUIET + 4.0, t));
    col = min(col, vec3(mix(1.0, 0.85, gateW)));

    // vignette (darkening only) and triangular dither against banding
    col *= 1.0 - 0.18 * dot(uv, uv);
    ivec2 pc = ivec2(fragCoord);
    float dz = h2s(pc, 101u, 7u) + h2s(pc, 102u, 7u) - 1.0;
    col += dz / 255.0;
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
