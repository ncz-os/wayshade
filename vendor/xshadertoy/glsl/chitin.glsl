// SPDX-License-Identifier: Apache-2.0
// chitin.glsl - an original, near-monochrome study of an impossible carapace:
// lit relief of ribs, conduits and pores, in the manner of biomechanical
// machinery. Nothing here depicts a recognizable thing.
//
// What it is: a procedural 2.5D heightfield, lit from a low raking light. It is
// a picture of engineered-looking relief, not a simulation. Every frame at
// motion time t is a pure function of t (stateless).
//
// Rules the design keeps:
//   - Scale hierarchy, fixed as fractions of the screen height (2 units):
//       rib period      1/3   (PR)
//       conduit period  1/12  (PC)
//       pore period     1/60  (PP)
//     rib : conduit : pore = 20 : 5 : 1 in period, so the three read as three
//     distinct sizes and never blur into one texture.
//   - Noise only warps. The structure is geometric (ridges, half-cylinders,
//     pits, all with analytic derivatives). Noise never adds height or color;
//     it only displaces the domain the geometry is drawn in, in exactly THREE
//     octaves (each a 2-channel displacement with an analytic Jacobian, so the
//     shading normal is exact under the warp).
//   - Bilateral break: the picture is mirrored about a slightly wandering
//     vertical axis, then broken by `asymmetry` (0.05..0.15): side amplitudes
//     differ, some conduits and pores exist on one side only, and the finest
//     warp octave is not mirrored.
//   - Palette: near-monochrome (bone, gunmetal, charcoal, slate, verdigris,
//     steel green). One muted accent tone on a small share of conduit runs
//     (`accent` <= 0.08 of the runs, far less of the picture). No saturated
//     red, no blue-white.
//   - Not to be read as: cave, rock, intestine, anything gross. Conduits are
//     straight, rigid runs cut at joints; nothing is wet, wrapped or coiling;
//     there are no faces, eyes, hands or gore. A pore is one pit with one
//     rim, never a pair or a target.
//
// Forms (presets Nave, Conduit, Lathe, Comb, Sump): the same three layers
// arranged as an arched vault of ribs on a spinal column (nave), bundled runs
// along chevron ribs (conduit), concentric turned ribs with radial runs (lathe), vertical
// spines with tooth rows between them (comb), or chevrons converging downward on
// a trench (sump). Grids of crossing ribs and runs are avoided on purpose: they
// read as woven panelling, not as anatomy.
//
// Cost: 6 noise evaluations (3 octaves x 2 channels), the layered relief once
// plus 1-2 shadow taps of the ribs and conduits only, one pore lattice lookup.
//
// Original work (LEAD 29, 2026-09-30), Apache-2.0.

//@saver id=chitin version=1 family=chitin director=90
//@palette-native bg=#0b0b0c body=#d9d3c1 accent=#7fa398 accent2=#a0876a
//@option name=form type=enum choices=nave,conduit,lathe,comb,sump default=nave rand=nave|conduit|comb|lathe group=Look
//@+ label="Form" desc="How the ribs, conduits and pores are arranged: nave (arched vault), conduit (bundled runs on chevron ribs), lathe (concentric turned ribs with radial runs), comb (spines with tooth rows), sump (chevrons converging on a trench)."
//@option name=relief type=float default=1 min=0.5 max=1.6 rand=0.85:1.25 group=Look
//@+ label="Relief" desc="Height of the relief as seen in the raking light."
//@option name=warp type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Warp" desc="How far the structure is bent out of true by the three-octave domain warp. 0 is ruled and rigid."
//@option name=asymmetry type=float default=0.1 min=0.05 max=0.15 rand=0.06:0.14 group=Simulation
//@+ label="Asymmetry" desc="How far the two halves differ. The picture is mirrored, then broken by this much."
//@option name=conduits type=float default=0.65 min=0.2 max=1 rand=0.45:0.85 group=Simulation
//@+ label="Conduits" desc="Share of conduit runs that are present."
//@option name=pores type=float default=0.6 min=0 max=1 rand=0.35:0.8 group=Simulation
//@+ label="Pores" desc="Share of the pore lattice that is pitted."
//@option name=sheen type=float default=0.5 min=0 max=1 rand=0.25:0.8 group=Look
//@+ label="Sheen" desc="Material, from dry bone (0) to polished gunmetal (1)."
//@option name=accent type=float default=0.05 min=0 max=0.08 rand=0.02:0.07 group=Look
//@+ label="Accent" desc="Share of conduit runs painted in the muted second tone. Never more than 8 percent."
//@option name=light type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Light direction" desc="Where the raking light comes from: 0.5 is straight above, the ends are from either side."
//@option name=light-drift type=float default=0.35 min=0 max=1 rand=0.15:0.5 group=Motion
//@+ label="Light drift" desc="How far the light slowly swings back and forth. 0 holds it still."
//@option name=scroll type=float default=0.4 min=0 max=1 rand=0.2:0.6 group=Motion
//@+ label="Scroll" desc="Speed of the slow travel along the structure (lathe: the surface turns slowly around the column)."

#include "xst_common.glsl"

#define PR (2.0 / 3.0)    // rib period,     1/3  of the screen height
#define PC (2.0 / 12.0)   // conduit period, 1/12 of the screen height
#define PP (2.0 / 60.0)   // pore period,    1/60 of the screen height

#define AR 0.070          // rib height (uv units)
#define AC 0.026          // conduit height
#define AP 0.006          // pore depth
#define ASYM_K 0.9        // weight of the un-mirrored warp octave per unit asymmetry/0.10

#define SCR (iTime * 0.02 * OPT_SCROLL)

float hsh(float i, float s) { return xst_hash21(vec2(i, s + 1000.0 * iSeed.x)); }
float cf(float x, out float id) { id = floor(x + 0.5); return x - id; }

// rounded ridge: f in [-0.5, 0.5] periods, half width w; returns (h, dh/df)
vec2 ridge(float f, float w) {
    float t = f / w;
    if (abs(t) >= 1.0) return vec2(0.0);
    float u = 1.0 - t * t;
    float s = sqrt(u);
    return vec2(u * s, -3.0 * s * t / w);
}
// rib section: a narrow crest on a broader, lower foot
vec2 rib(float f, float w) { return ridge(f, w) + 0.30 * ridge(f, 2.2 * w); }
// half-cylinder run, tubes touching: f in [-0.5, 0.5]; returns (h, dh/df)
vec2 tube(float f) {
    float t = clamp(2.0 * f, -0.97, 0.97);
    float s = sqrt(1.0 - t * t);
    return vec2(s - 0.243, abs(2.0 * f) < 0.97 ? -2.0 * t / s : 0.0);
}

// A spinal column on the mirror axis: a waisted, segmented column (vertebra-like),
// one per 3 conduit widths. Adds height and gradient into s.
void spine(vec2 q, float sa, inout float h, inout vec2 g, inout float rib) {
    // a continuous column with waisted segments (never isolated ovals: those read as eyes)
    float per = 3.0 * PC;
    float ph = 6.2831853 * (q.y - SCR) / per;
    float wd = 0.10;
    float xw = q.x / wd;
    if (abs(xw) < 1.0) {
        float d = 1.0 - xw * xw;
        float m = 0.62 + 0.38 * cos(ph);
        float a = AR * 1.3 * sa;
        h += a * d * d * m;
        g += a * vec2(-4.0 * d * xw / wd * m, d * d * (-0.38 * sin(ph)) * 6.2831853 / per);
        rib = max(rib, d * m);
    }
}

struct Surf {
    float h;    // detail height (ribs, conduits; no base curvature)
    vec2  g;    // dh/dq including base curvature
    float rib;  // rib share 0..1
    float cnd;  // conduit share 0..1
    float acc;  // 1 on a run that carries the accent tone
    float bas;  // extra cavity 0..1 (drains)
    float tone; // per-cell tone jitter
    vec2  pc;   // pore lattice coordinates
    vec2  pj;   // d pc / d q per axis
};

// The three layers other than pores, in the mirrored, warped frame q.
// side = sgn * asymmetry (signed): tilts amplitudes and presence toward one half.
Surf macro(vec2 q, float side) {
    Surf s;
    s.h = 0.0; s.g = vec2(0.0); s.rib = 0.0; s.cnd = 0.0; s.acc = 0.0; s.bas = 0.0;
    s.tone = 0.5; s.pc = q; s.pj = vec2(1.0);
    float sa = 1.0 + 1.0 * side;
    float dens = OPT_CONDUITS + 0.75 * side;
    float accp = OPT_ACCENT * 1.0;

#if OPT_FORM == OPT_FORM_NAVE
    float k = 0.22 + 0.14 * DIR_P1;
    float b = (q.y - SCR + k * q.x * q.x) / PR;
    float bi; float fb = cf(b, bi);
    vec2 r = rib(fb, 0.075 + 0.02 * hsh(bi, 5.0));
    float tap = 1.25 - 0.55 * smoothstep(0.0, 1.6, q.x);    // ribs thin toward their tips
    float ra = AR * (0.75 + 0.5 * hsh(bi, 1.0)) * sa * tap;
    s.h += ra * r.x;
    s.g += ra * r.y * vec2(2.0 * k * q.x, 1.0) / PR;
    s.rib = min(r.x, 1.0);
    spine(q, sa, s.h, s.g, s.rib);
    s.tone = hsh(floor(b), 6.0);
    float a = q.x / PC; float ai; float fa = cf(a, ai);
    float bc = floor(b);
    float on = step(hsh(ai * 13.0 + bc * 7.0, 3.0), dens);
    vec2 t = tube(fa);
    float cyc = (q.y - SCR) / (PC * 2.0); float cyi; float cyf = cf(cyc, cyi);
    vec2 col = ridge(cyf, 0.07);                       // collars every two conduit widths
    float ca = AC * on * sa;
    float cm = 1.0 + 0.55 * col.x;
    s.h += ca * t.x * cm; s.g += ca * vec2(t.y * cm / PC, t.x * 0.55 * col.y / (PC * 2.0));
    s.cnd = on * smoothstep(0.0, 0.2, t.x);
    s.acc = on * step(hsh(ai * 5.0 + bc * 11.0, 8.0), accp);

#elif OPT_FORM == OPT_FORM_CONDUIT || OPT_FORM == OPT_FORM_SUMP
#if OPT_FORM == OPT_FORM_SUMP
    float kc = -0.42;                                  // chevrons converge downward on a trench
    float spn = -1.0;
#else
    float kc = 0.55;
    float spn = 1.0;
#endif
    float b = (q.y - SCR + kc * q.x) / PR;
    float bi; float fb = cf(b, bi);
    vec2 r = rib(fb, 0.07);
    float ra = AR * 0.6 * (0.8 + 0.4 * hsh(bi, 1.0)) * sa;
    s.h += ra * r.x;
    vec2 gb = vec2(kc, 1.0) / PR;
    s.g += ra * r.y * gb;
    s.rib = min(r.x, 1.0);
    spine(q, spn * sa, s.h, s.g, s.rib);
    s.tone = hsh(floor(b), 6.0);
    float c = b * 4.0; float ji; float fj = cf(c, ji);
    float jj = ji - 4.0 * floor(ji / 4.0);
    float sidx = floor((q.x - kc * (q.y - SCR)) / 0.9);
    float on = step(0.5, jj) * step(hsh(ji * 3.1 + sidx * 17.0, 3.0), dens + 0.15);
    vec2 t = tube(fj);
    float ac = (q.x - kc * (q.y - SCR)) / 0.42; float aci; float acf = cf(ac, aci);
    vec2 col = ridge(acf, 0.06);                       // collars along each run
    float cm = 1.0 + 0.55 * col.x;
    vec2 galong = vec2(1.0, -kc) / 0.42;
    float ca = AC * 1.15 * on * sa;
    s.h += ca * t.x * cm; s.g += ca * (t.y * cm * 4.0 * gb + t.x * 0.55 * col.y * galong);
    s.cnd = on * smoothstep(0.0, 0.2, t.x);
    s.acc = on * step(hsh(ji * 5.0 + sidx * 11.0, 8.0), accp);

#elif OPT_FORM == OPT_FORM_LATHE
    // turned work: concentric ribs about a hub far below the frame, radial runs, the spine on the axis
    vec2 dd = q - vec2(0.0, -2.1);
    float rr = length(dd);
    vec2 dir = dd / rr;
    float b = (rr - SCR) / PR;
    float bi; float fb = cf(b, bi);
    vec2 r = rib(fb, 0.075 + 0.02 * hsh(bi, 5.0));
    float tap = 1.25 - 0.55 * smoothstep(0.0, 1.6, q.x);
    float ra = AR * (0.75 + 0.5 * hsh(bi, 1.0)) * sa * tap;
    s.h += ra * r.x; s.g += ra * r.y * dir / PR;
    s.rib = min(r.x, 1.0);
    s.tone = hsh(floor(b), 6.0);
    spine(q, sa, s.h, s.g, s.rib);
    float ka = 2.2 / PC;
    float a = atan(dd.x, dd.y) * ka; float ai; float fa = cf(a, ai);
    vec2 gang = vec2(dd.y, -dd.x) / (rr * rr) * ka;
    float bc = floor(b);
    float on = step(hsh(ai * 13.0 + bc * 7.0, 3.0), dens);
    vec2 t = tube(fa);
    float cyc = (rr - SCR) / (PC * 2.0); float cyi; float cyf = cf(cyc, cyi);
    vec2 col = ridge(cyf, 0.07);
    float ca = AC * on * sa;
    float cm = 1.0 + 0.55 * col.x;
    s.h += ca * t.x * cm; s.g += ca * (t.y * cm * gang + t.x * 0.55 * col.y * dir / (PC * 2.0));
    s.cnd = on * smoothstep(0.0, 0.2, t.x);
    s.acc = on * step(hsh(ai * 5.0 + bc * 11.0, 8.0), accp);

#elif OPT_FORM == OPT_FORM_COMB
    float y = q.y - SCR;
    float b = q.x / PR;
    float bi; float fb = cf(b, bi);
    vec2 r = rib(fb, 0.06);
    float ra = AR * 1.1 * (0.8 + 0.4 * hsh(bi, 1.0)) * sa;
    s.h += ra * r.x; s.g.x += ra * r.y / PR;
    s.rib = min(r.x, 1.0);
    float bc = floor(b); float u = b - bc;
    s.tone = hsh(bc, 6.0);
    float a = y / PC; float ai; float fa = cf(a, ai);
    float L = 0.30 + 0.62 * hsh(ai, 20.0 + bc);
    float on = step(hsh(ai * 3.0 + bc * 29.0, 21.0), dens * 1.15);
    float e = clamp((u - (L - 0.10)) / 0.10, 0.0, 1.0);
    float m = 1.0 - e * e * (3.0 - 2.0 * e);
    float dm = -6.0 * e * (1.0 - e) / 0.10 / PR;
    vec2 t = tube(fa);
    float ca = AC * on * sa;
    s.h += ca * t.x * m;
    s.g += ca * vec2(t.x * dm, t.y * m / PC);
    s.cnd = on * m * smoothstep(0.0, 0.2, t.x);
    s.acc = on * step(hsh(ai * 5.0 + bc * 11.0, 8.0), accp);

#endif
    return s;
}

// Pore lattice: staggered pits, each with one rim. Returns (height, dh/dp).
vec3 pores(vec2 p, float dens, float fade) {
    vec2 g = p / PP;
    vec2 cA = floor(g) + 0.5, cB = floor(g + 0.5);
    vec2 dA = g - cA, dB = g - cB;
    bool useA = dot(dA, dA) <= dot(dB, dB);
    vec2 c = useA ? cA : cB;
    vec2 d = useA ? dA : dB;
    float hk = xst_hash21(c + vec2(useA ? 0.0 : 53.0, 97.0 * iSeed.z));
    // pits come in perforated panels (8 x 8 lattice cells); a few holes missing
    float panel = xst_hash21(floor(c / 8.0) + 211.0 + 97.0 * iSeed.w);
    float on = step(panel, dens * 0.8) * step(hk, 0.93);
    float R = 0.20 * (0.85 + 0.30 * fract(hk * 37.0));
    float r = length(d) + 1e-5;
    float u = r / R;
    float h = 0.0, dh = 0.0;
    if (u < 1.0) { float t = 1.0 - u * u; h = -t * t; dh = 4.0 * u * t; }
    float e = (u - 1.02) / 0.14;
    if (u < 1.9) { float ex = exp(-e * e); h += 0.32 * ex; dh += 0.32 * ex * (-2.0 * e / 0.14); }
    float k = on * fade * AP;
    return vec3(k * h, k * (d / r) * (dh / R) / PP);
}

// Three octaves of two-channel warp displacement with analytic Jacobian.
// Octaves 1 and 2 are evaluated in the mirrored frame (symmetric); octave 3
// is not mirrored and carries the bilateral break.
void warp3(vec2 pm, vec2 pu, float sgn, out vec2 w, out mat2 Jw) {
    vec2 o = iSeed.xy * 61.0;
    vec2 ph = vec2(0.020 * DIR_PH0, 0.015 * DIR_PH1);
    w = vec2(0.0);
    Jw = mat2(0.0);
    // octave 1 (symmetric)
    float f = 0.7;
    vec3 nx = xst_noised(pm * f + o + ph), ny = xst_noised(pm * f + o + ph + 37.3);
    w += vec2(nx.x, ny.x);
    Jw += mat2(f * sgn * vec2(nx.y, ny.y), f * vec2(nx.z, ny.z));
    // octave 2 (symmetric)
    f = 1.3;
    nx = xst_noised(pm * f + o * 1.7 - ph); ny = xst_noised(pm * f + o * 1.7 - ph + 37.3);
    w += 0.5 * vec2(nx.x, ny.x);
    Jw += 0.5 * mat2(f * sgn * vec2(nx.y, ny.y), f * vec2(nx.z, ny.z));
    // octave 3 (not mirrored: the bilateral break)
    f = 2.6;
    float a3 = 0.17 * ASYM_K * OPT_ASYMMETRY / 0.10;
    nx = xst_noised(pu * f + o * 2.9 + ph.yx); ny = xst_noised(pu * f + o * 2.9 + ph.yx + 37.3);
    w += a3 * vec2(nx.x, ny.x);
    Jw += a3 * mat2(f * vec2(nx.y, ny.y), f * vec2(nx.z, ny.z));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 p = xst_uv(fragCoord);
    float cx = 0.10 * (DIR_P0 - 0.5);
    float sgn = p.x >= cx ? 1.0 : -1.0;
    vec2 pm = vec2(abs(p.x - cx), p.y);
    vec2 pu = vec2(p.x - cx, p.y);
    float side = sgn * OPT_ASYMMETRY;

    // warp: q is the frame the geometry is drawn in; J = dq/dp (screen)
    float A = (0.03 + 0.11 * OPT_WARP) * (0.85 + 0.3 * DIR_P4);
#if OPT_FORM == OPT_FORM_CONDUIT
    A *= 0.45;                                          // runs stay rigid: no worm reading
#endif
    vec2 w; mat2 Jw;
    warp3(pm, pu, sgn, w, Jw);
    vec2 q = pm + A * w;
    mat2 J = mat2(sgn, 0.0, 0.0, 1.0) + A * Jw;

    // relief
    Surf S = macro(q, side);
    float pdens = OPT_PORES + 0.6 * side;
    float pfade = xst_octave_fade(1.0 / PP);
    vec3 pr = pores(S.pc, pdens, pfade);
    float pmask = (1.0 - smoothstep(0.05, 0.35, S.rib)) * (1.0 - 0.8 * S.cnd) * (1.0 - S.bas);
    float ph = pr.x * pmask;
    vec2 gq = S.g + pr.yz * S.pj * pmask;
    float h0 = S.h + ph;
    vec2 gp = gq * J;                                  // gradient wrt the screen
    vec3 n = normalize(vec3(-OPT_RELIEF * gp, 1.0));

    // light
    float th = 1.5708 + (OPT_LIGHT - 0.5) * 2.2 + OPT_LIGHT_DRIFT * 0.6 * sin(0.04 * DIR_PH3);
    float el = 0.50 + 0.10 * DIR_P5;
    vec2 ld = vec2(cos(th), sin(th));
    vec3 L = normalize(vec3(ld * cos(el), sin(el)));
    float tanE = tan(el);

    // two soft shadow taps of the ribs and conduits toward the light
    vec2 qo = vec2(ld.x * sgn, ld.y);
    Surf S1 = macro(q + qo * 0.06, side);
    float sh = smoothstep(-0.004, 0.014, (h0 + 0.06 * tanE) - S1.h);
#if NCZ_QUALITY > 0
    Surf S2 = macro(q + qo * 0.14, side);
    sh *= mix(1.0, smoothstep(-0.004, 0.014, (h0 + 0.14 * tanE) - S2.h), 0.85);
#endif

    // occlusion: grooves and pits are dark
    float cav = clamp(-ph / AP, 0.0, 1.0);
    float ao = mix(0.55, 1.0, smoothstep(0.0, 0.45, S.h / (AR + 0.5 * AC)));
    ao *= 1.0 - 0.5 * cav * pmask;
    ao *= 1.0 - 0.5 * S.bas;

    // material
    float sheen = OPT_SHEEN;
    vec3 alb = PAL_BODY * mix(0.06, 0.035, sheen);
    alb = mix(alb, PAL_BODY * mix(0.75, 0.40, sheen), smoothstep(0.1, 0.6, S.rib));
    alb *= 0.90 + 0.20 * S.tone;
    alb = mix(alb, PAL_ACCENT2 * 0.30, 0.8 * S.acc * S.cnd);                 // muted accent run
    float low = clamp(cav * pmask + S.bas + 0.6 * (1.0 - smoothstep(0.0, 0.3, S.h / AR)) * S.cnd, 0.0, 1.0);
    alb = mix(alb, PAL_ACCENT * 0.22, 0.20 * low);                     // patina in the recesses

    float ndl = dot(n, L);
    float diff = clamp((ndl + 0.15) / 1.15, 0.0, 1.0);
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 Hh = normalize(L + V);
    float shin = mix(24.0, 140.0, sheen);
    float spec = pow(max(dot(n, Hh), 0.0), shin) * mix(0.05, 0.8, sheen);
    vec3 Rv = reflect(-V, n);
    float fres = 0.04 + 0.96 * pow(1.0 - max(n.z, 0.0), 5.0);
    vec3 sky = mix(PAL_BG * 1.5, PAL_BODY * 0.55, smoothstep(-0.4, 0.9, Rv.y));
    vec3 envr = sky * (0.12 + 1.0 * sheen) * (0.30 + 1.5 * fres);
    vec3 specc = mix(vec3(0.9), PAL_BODY, 0.7 * sheen);

    vec3 amb = PAL_BODY * 0.02 + PAL_BG * 1.5;
    vec3 col = alb * (amb * ao + vec3(1.0, 0.98, 0.94) * 2.1 * diff * sh * mix(0.6, 1.0, ao))
             + specc * spec * sh * ao + envr * ao;

    // depth cue: the nave and conduit forms recede toward the top of the frame
#if OPT_FORM == OPT_FORM_NAVE || OPT_FORM == OPT_FORM_CONDUIT
    col = mix(col, PAL_BG * 2.0, 0.35 * smoothstep(-0.2, 1.1, p.y));
#endif
    col *= 1.0 - 0.28 * dot(p * 0.5, p * 0.5);

    fragColor = xst_output(col, fragCoord);
}
