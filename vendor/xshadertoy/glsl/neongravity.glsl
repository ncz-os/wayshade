// SPDX-License-Identifier: Apache-2.0
// neongravity.glsl - looking straight down a well: neon rings and spokes crowd
// toward the centre while glowing bodies spiral in along Kepler-like arcs.
//
// What it is: a stylized top view of infall, analytic per pixel, no marching.
// It replaces neongravity-0 and neongravity-1 (both ports of a third-party
// raymarcher; this is original work).
//   - The lattice lives in log-polar coordinates: rings at equal steps of
//     log r (so they crowd toward the centre), spokes at equal steps of angle
//     plus a swirl that grows toward the centre (frame dragging, by look only).
//   - Bodies: each is on a spiral orbit that starts far out and falls to the
//     rim of the horizon, then is replaced by another (a sawtooth in time,
//     hidden by a fade at both ends). The angle follows an angular speed that
//     grows as r^(-3/2), integrated in closed form (theta = theta0 + c r^(-1/2)),
//     so the motion has no history and no jumps. Each body leaves a trail: the
//     curve r(theta) behind it, tested with the exact radial distance to the arc
//     (one evaluation per body).
//   - The centre is a dark disc with a thin photon ring; the ring and the disc
//     edge are drawn with fwidth coverage.
//   - A slow global breathing of the ring spacing (director) keeps the lattice
//     from looking like a fixed texture.
//
// Cost: per body about 25 flops, one pow, one mod, one sqrt, a sin and a cos, two exp and
// two smoothstep (8 bodies at quality 0, up to 14 above); plus one log and one atan for
// the lattice, which has no loop at all.
//
// Original work (LEAD 27, 2026-09-30), Apache-2.0.

//@saver id=neongravity version=2 family=dark director=80
//@palette-native bg=#04030a body=#26e0ff accent=#ff3fa0 accent2=#9a6bff
//@option name=bodies type=int default=9 min=2 max=14 rand=6:12 group=Simulation
//@+ label="Bodies" desc="Number of glowing bodies falling in (quality 0 draws at most 8)."
//@option name=infall type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Simulation
//@+ label="Infall speed" desc="How fast the bodies fall from the outer edge to the horizon."
//@option name=trail type=float default=0.5 min=0 max=1 rand=0.3:0.75 group=Simulation
//@+ label="Trail length" desc="How much of its orbit each body draws behind it."
//@option name=rings type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Rings" desc="Number of rings in the lattice."
//@option name=spokes type=float default=0.5 min=0 max=1 rand=0.3:0.7 group=Look
//@+ label="Spokes" desc="Number of spokes in the lattice."
//@option name=swirl type=float default=0.5 min=0 max=1 rand=0.2:0.7 group=Look
//@+ label="Swirl" desc="How strongly the spokes wind up toward the centre."
//@option name=horizon type=float default=0.5 min=0 max=1 rand=0.35:0.65 group=Look
//@+ label="Horizon size" desc="Radius of the dark disc."
//@option name=brightness type=float default=1 min=0.5 max=1.6 rand=0.85:1.2 group=Look
//@+ label="Brightness" desc="Overall intensity of the neon."

#include "xst_common.glsl"

#if NCZ_QUALITY == 0
#define MAXB 8
#else
#define MAXB 14
#endif

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float r = length(uv);
    float th = atan(uv.y, uv.x);
    float rh = mix(0.10, 0.24, OPT_HORIZON);            // horizon radius
    float lr = log(max(r, 1e-3) / rh);                  // log radius from the horizon

    // ---- lattice ----------------------------------------------------------
    float ringN = mix(4.0, 9.0, OPT_RINGS) * (1.0 + 0.08 * sin(0.03 * DIR_PH0));
    float u = lr * ringN / 2.0;                         // ring coordinate: one ring per unit
    float nsp = floor(mix(10.0, 28.0, OPT_SPOKES) + 0.5);
    float wind = OPT_SWIRL * 2.2 * rh / max(r, rh) + 0.03 * DIR_PH1;     // radians of wind-up
    float sp = (th + wind) * nsp / XST_TAU;             // spoke coordinate (seam: nsp integer)
    // pixel footprints in lattice units, analytic (fwidth of atan is wrong at its seam)
    float px = xst_px();
    float rc = max(r, 1e-3);
    float wu = px * ringN / (2.0 * rc);
    float dwind = (r > rh) ? OPT_SWIRL * 2.2 * rh / (rc * rc) : 0.0;
    float ws = px * (nsp / (XST_TAU * rc) + dwind * nsp / XST_TAU) + 1e-5;
    float ring = 1.0 - smoothstep(0.0, 1.0, (0.5 - abs(fract(u) - 0.5)) / (1.4 * wu));
    float spoke = 1.0 - smoothstep(0.0, 1.0, (0.5 - abs(fract(sp) - 0.5)) / (1.0 * ws));
    // lattice fades where cells drop below ~2 px
    float okr = 1.0 - smoothstep(0.25, 0.5, wu);
    float oks = 1.0 - smoothstep(0.25, 0.5, ws);
    float glow_r = exp(-pow((0.5 - abs(fract(u) - 0.5)) * 11.0, 2.0));
    float glow_s = exp(-pow((0.5 - abs(fract(sp) - 0.5)) * 11.0, 2.0));

    float rad = smoothstep(0.0, 2.6, lr);               // 0 at the horizon .. 1 far
    vec3 lat_col = mix(PAL_ACCENT, PAL_BODY, smoothstep(0.0, 0.7, rad));
    lat_col = mix(lat_col, PAL_ACCENT2, smoothstep(0.55, 1.0, rad) * 0.7);
    float fade_out = 1.0 - 0.55 * smoothstep(1.2, 3.2, lr);   // dim toward the frame edge
    vec3 col = PAL_BG * (1.0 + 1.6 * exp(-lr * 0.9));
    col += lat_col * (ring * okr * 0.7 + spoke * oks * 0.45
                      + (glow_r * okr * 0.22 + glow_s * oks * 0.12)) * fade_out * 0.55;

    // ---- infalling bodies ---------------------------------------------------
    float R0 = 1.55, R1 = rh * 1.25;
    float nb = float(min(OPT_BODIES, MAXB));
    float speed = 0.02 + 0.05 * OPT_INFALL;
    float tl = mix(0.35, 1.3, OPT_TRAIL);                // trail extent in radians of orbit
    for (int k = 0; k < MAXB; k++) {
        if (float(k) >= nb) break;
        float fk = float(k);
        float per = 1.0 / (speed * (0.8 + 0.5 * fract(iSeed.x * (3.0 + fk))));
        float ph = fract(iTime / per + fract(fk * 0.6180339 + iSeed.y));
        float rk = mix(R0, R1, pow(ph, 1.35));           // radius of the head
        // angle: theta = theta0 + c * r^(-1/2) (Keplerian integral), wound so the
        // last stretch makes about two turns
        float thk = 6.2832 * fract(fk * 0.7548777 + iSeed.z) + 5.0 * inversesqrt(rk);
        float env = smoothstep(0.0, 0.10, ph) * (1.0 - smoothstep(0.86, 1.0, ph));
        // pixel relative to the head, along the orbit: dth = how far behind the head
        float dth = mod(thk - th + XST_PI, XST_TAU) - XST_PI;   // [-pi, pi), > 0 = behind
        // the trail: for a point at angular offset dth behind the head, the radius
        // there was larger: r_trail(dth) = rk / (1 - dth * kk) with kk from dr/dtheta
        // (theta = th0 + 5 r^-1/2  ->  r = (5 / (theta - th0))^2 ... use its inverse)
        float s = 5.0 * inversesqrt(rk) - dth;           // 5 r_trail^(-1/2)
        float rt = (s > 0.5) ? 25.0 / (s * s) : 1e3;
        float behind = step(0.0, dth) * (1.0 - smoothstep(0.0, tl, dth));
        // distance from this pixel to the trail arc, in units of r (radial gap)
        float gap = abs(r - rt) / max(r, 0.05);
        float width = 0.014 + xst_px() / max(r, 0.05);
        float trail_i = exp(-gap * gap / (width * width)) * behind * (1.0 - dth / tl) * 0.75;
        // head: a small glow at the head position
        vec2 hp = rk * vec2(cos(thk), sin(thk));
        float hd = length(uv - hp);
        float head_i = exp(-hd * hd / (0.00045 + 0.00012 * rk * rk));
        vec3 bc = mix(PAL_BODY, PAL_ACCENT, fract(fk * 0.37 + iSeed.w));
        bc = mix(bc, vec3(1.0), head_i * 0.5);
        col += bc * (trail_i * 1.7 + head_i * 1.4) * env * OPT_BRIGHTNESS;
    }

    // ---- horizon disc and photon ring ------------------------------------------
    float d0 = r - rh;
    float disc = xst_aastep(0.0, d0);
    float photon = exp(-pow(d0 / (0.008 + 2.0 * xst_px()), 2.0));
    col = mix(vec3(0.0), col, disc);
    col += mix(PAL_ACCENT, vec3(1.0), 0.35) * photon * 0.9 * OPT_BRIGHTNESS;
    // soft accretion haze just outside
    col += PAL_ACCENT2 * exp(-max(d0, 0.0) * 9.0) * 0.10 * disc;

    fragColor = xst_output(col, fragCoord);
}
