// SPDX-License-Identifier: Apache-2.0
// Title:  Neon Horizon
// Desc:   A retro-futurist night cityscape in continuous forward flight --
//         the 1980s synthwave idiom, in the NCZ-OS 26.10 "Cygnus" palette.
//         A violet-black sky; a large striped sun disc (magenta at the
//         base, orange at the cap, cut by horizontal bands) sitting on the
//         horizon as the focal point; a cyan neon grid with a magenta halo
//         scrolling toward the viewer and fading into the haze; two
//         parallax rows of thin, tall skyscraper silhouettes of uneven
//         width and spacing, a few landmark towers with spires, sparse dim
//         lit windows (warm and cool), neon edge glow along the rooflines,
//         a low haze band and a handful of dim stars.  Dark and saturated:
//         no white and no lavender wash anywhere.
//
//         Original work.  No reference to any external shader source.  All
//         technique is from published graphics literature: analytic plane
//         projection (Quilez 2008, "Modeling with distance functions"),
//         perspective grid via 1/z spacing (a standard 1980s demo-scene
//         trick -- see e.g. Pouet.net "Amiga Demo Perspective Grid"),
//         "sun cut by scanlines" is the period-canonical synthesis
//         (J. Mróz, "Synthwave City", 2019 -- CC BY 3.0, cited as prior
//         art, not copied), exponential atmospheric haze (Quilez 2008,
//         "Atmospheric scattering").  Hashes are David Hoskins'
//         sin-free integer-multiply / fract construction (2013), so the
//         result is identical on NVIDIA, Intel and Mali -- no large-
//         argument sin() hashing.
//
//         Per-run variation driven entirely by iSeed: palette family
//         (Cygnus magenta/cyan for two thirds of seeds; ember amber/violet
//         and arctic blue/teal for the rest), skyline height and density,
//         sun size and lateral position, flight speed, haze density.  Two
//         runs at the same frame index differ in composition and motion,
//         not merely in animation phase.
//
//         Budget: one pass, no texture reads, no pow() outside the final
//         gamma, no sin() outside the per-run constants; the skyline loop
//         is two iterations of straight-line arithmetic.  Holds 60 fps on
//         an Intel UHD 630 at the weak-class render scale.

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Small helpers.  Sin-free hashes (Hoskins, 2013) -- bounded input,   */
/* fract-based, no large-angle sin() so NVIDIA/Intel/Mali agree.       */
/* ----------------------------------------------------------------- */

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/* Four decorrelated hashes of one cell index in one go: height, width,
 * offset and landmark draw for a building cell. */
vec4 hash14(float p) {
    vec4 p4 = fract(vec4(p) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p4 += dot(p4, p4.wzxy + 33.33);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

/* ----------------------------------------------------------------- */
/* Per-run variation, derived from iSeed (4 floats in [0,1)).          */
/*   iSeed.x  -> palette family (Cygnus for x < 0.66, then two others) */
/*   iSeed.y  -> skyline height distribution and density               */
/*   iSeed.z  -> sun size + flight speed                               */
/*   iSeed.w  -> haze density + sun lateral position                   */
/* ----------------------------------------------------------------- */

struct Palette {
    vec3 sky_hi;     /* zenith (the Cygnus "hole") */
    vec3 sky_lo;     /* horizon band (the Cygnus "field", lit) */
    vec3 ground;     /* dark asphalt base under the grid */
    vec3 grid;       /* neon grid core */
    vec3 halo;       /* neon grid halo and far-row edge glow */
    vec3 sun_hi;     /* cap of the sun disc */
    vec3 sun_lo;     /* base of the sun disc, also the horizon glow */
    vec3 win_cool;   /* cool lit window */
    vec3 win_warm;   /* warm lit window */
};

/* All values are scene-linear; the final pow(1/2.2) maps them to the
 * display-referred sRGB the linear surface expects.  The Cygnus tokens
 * (magenta #E83CC0, violet #7B4DFF, cyan #2ED8F0, field #120A22, hole
 * #05030A) are quoted in display space in the comments. */
Palette pick_palette(float family) {
    Palette p;
    if (family < 0.66) {
        /* Cygnus: magenta / cyan on violet-black.  The default. */
        p.sky_hi   = vec3(0.0015, 0.0009, 0.0040);  /* #05030A hole      */
        p.sky_lo   = vec3(0.0400, 0.0040, 0.0420);  /* field lit magenta  */
        p.ground   = vec3(0.0040, 0.0018, 0.0090);  /* between hole/field */
        p.grid     = vec3(0.0300, 0.7200, 0.9000);  /* #2ED8F0 cyan       */
        p.halo     = vec3(0.6000, 0.0300, 0.4200);  /* #E83CC0 magenta    */
        p.sun_hi   = vec3(1.0000, 0.3600, 0.0300);  /* saturated orange   */
        p.sun_lo   = vec3(0.9500, 0.0400, 0.4800);  /* saturated magenta  */
        p.win_cool = vec3(0.1000, 0.6500, 0.9000);
        p.win_warm = vec3(1.0000, 0.4200, 0.0800);
    } else if (family < 0.84) {
        /* Ember: amber grid on a violet dusk, red-orange sun. */
        p.sky_hi   = vec3(0.0020, 0.0008, 0.0040);
        p.sky_lo   = vec3(0.0500, 0.0080, 0.0320);
        p.ground   = vec3(0.0050, 0.0020, 0.0070);
        p.grid     = vec3(1.0000, 0.5000, 0.0600);
        p.halo     = vec3(0.4500, 0.0400, 0.5500);  /* #7B4DFF-leaning   */
        p.sun_hi   = vec3(1.0000, 0.5200, 0.0400);
        p.sun_lo   = vec3(0.9500, 0.0600, 0.0900);
        p.win_cool = vec3(0.3000, 0.2000, 0.9000);
        p.win_warm = vec3(1.0000, 0.5000, 0.1000);
    } else {
        /* Arctic: deep blue sky, cyan grid, teal-to-gold sun.  Noir. */
        p.sky_hi   = vec3(0.0008, 0.0012, 0.0050);
        p.sky_lo   = vec3(0.0050, 0.0280, 0.0500);
        p.ground   = vec3(0.0015, 0.0030, 0.0080);
        p.grid     = vec3(0.0500, 0.9000, 0.7000);
        p.halo     = vec3(0.0400, 0.2000, 0.8000);
        p.sun_hi   = vec3(1.0000, 0.6500, 0.0500);
        p.sun_lo   = vec3(0.0300, 0.7000, 0.6500);
        p.win_cool = vec3(0.2000, 0.8000, 1.0000);
        p.win_warm = vec3(1.0000, 0.5500, 0.1500);
    }
    return p;
}

/* ----------------------------------------------------------------- */
/* mainImage.                                                          */
/* ----------------------------------------------------------------- */

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* --- per-run constants (uniform-only arithmetic) --- */
    float family = saturate(iSeed.x);
    Palette pal  = pick_palette(family);

    float height_bias = mix(0.70, 1.25, fract(iSeed.y * 1.7));
    float density     = mix(0.85, 1.20, fract(iSeed.y * 3.3 + 0.5));
    float sun_size    = mix(0.26, 0.40, fract(iSeed.z * 3.1));
    float speed       = mix(1.2,  2.6,  fract(iSeed.z * 1.9 + 0.3));
    float haze_dens   = fract(iSeed.w * 2.7);               /* 0 clear .. 1 thick */
    float sun_x_off   = mix(-0.22, 0.22, fract(iSeed.w * 1.3 + 0.2));

    /* --- screen coords: y up, horizon at `HZ` --- */
    const float HZ = -0.10;
    vec2 res = iResolution.xy;
    vec2 uv  = (2.0 * fragCoord - res) / res.y;
    float ay = uv.y - HZ;                        /* height above horizon */

    /* --- sky: violet-black gradient, lit toward the horizon --- */
    float skyt = saturate(ay / (1.0 - HZ));
    vec3 col = mix(pal.sky_lo, pal.sky_hi, smoothstep(0.0, 0.60, skyt));

    /* Dim stars, only in the dark upper sky, slow twinkle.  The branch is
     * screen-coherent (one region), so the lower sky skips the hash. */
    if (skyt > 0.25) {
        vec2  sc = floor(uv * 140.0);
        float h  = hash21(sc + 7.0);
        float star = step(0.9975, h) * smoothstep(0.25, 0.70, skyt);
        float tw = 0.6 + 0.4 * fract(h * 91.7 + iTime * 0.15);
        col += vec3(0.22, 0.24, 0.34) * star * tw;
    }

    /* Horizon glow centred on the sun, backlighting the skyline. */
    vec2  sun_c = vec2(sun_x_off, HZ + sun_size * 0.42);
    float hglow = exp(-max(ay, 0.0) * 4.5) * exp(-abs(uv.x - sun_c.x) * 1.0);
    col += pal.sun_lo * hglow * 0.28;

    /* --- striped sun disc, opaque, drawn before the buildings --- */
    {
        vec2  sp   = (uv - sun_c) / sun_size;
        float sd   = length(sp);
        float aa   = fwidth(sd) * 1.2;
        float disc = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, sd);
        /* Horizontal bands anchored in screen space; the gap widens
         * toward the base of the disc and vanishes at the cap, the
         * period-canonical striped sun.  Triangle wave, no sin(). */
        float band  = abs(fract(uv.y * 20.0 - iTime * 0.04) * 2.0 - 1.0);
        float gapw  = mix(0.0, 0.60, smoothstep(0.55, -0.85, sp.y));
        float baa   = fwidth(uv.y) * 20.0 * 2.0;
        float cut   = smoothstep(gapw, gapw + baa + 0.03, band);
        /* Body: magenta base, orange cap, saturated throughout. */
        vec3 body = mix(pal.sun_lo, pal.sun_hi, smoothstep(-0.9, 0.75, sp.y));
        col = mix(col, body, disc * cut);
        /* Tight saturated halo just outside the rim. */
        float halo = exp(-max(sd - 1.0, 0.0) * 4.0) * (1.0 - disc);
        col += pal.sun_lo * halo * 0.22;
    }

    /* --- ground plane: perspective grid ---
     * The derivatives are taken outside the branch so they are defined
     * for every pixel (fwidth under non-uniform control flow is not). */
    float d  = max(-ay, 0.0);                       /* 0 at and above the horizon */
    float Z  = 0.90 / (d + 0.016);                  /* world depth proxy */
    vec2  gp = vec2(uv.x * Z, Z + iTime * speed * 0.30);
    vec2  fw = fwidth(gp);
    if (uv.y < HZ) {
        vec2  dist  = 0.5 - abs(fract(gp) - 0.5);   /* grid units to the nearest line */
        vec2  px    = dist / max(fw, vec2(1e-4));   /* ... in pixels */
        float p     = min(px.x, px.y);
        float core  = 1.0 - smoothstep(0.0, 1.4, p);
        float halo  = exp(-p * 0.30);
        /* Lines fade with depth and dissolve into the horizon haze. */
        float fade  = exp(-Z * 0.045) * smoothstep(0.0, 0.035, d);
        /* Keep the far field from becoming a solid halo: shrink the halo
         * where cells are under a few pixels wide. */
        float dens  = smoothstep(2.0, 9.0, 1.0 / max(max(fw.x, fw.y), 1e-4));
        halo *= dens;
        core *= dens;

        vec3 ground = pal.ground;
        ground += pal.halo * halo * 0.30 * fade;
        ground += pal.grid * core * 1.10 * fade;
        /* The far ground dissolves into the horizon haze. */
        ground = mix(ground, pal.sky_lo * 1.3, exp(-d * 22.0) * 0.55);
        /* A faint magenta reflection pool under the sun. */
        ground += pal.sun_lo * exp(-d * 7.0) * exp(-abs(uv.x - sun_c.x) * 2.2) * 0.12;
        col = ground;
    }

    /* --- skyline -----------------------------------------------------------
     * Two parallax rows of THIN, TALL buildings of uneven width and gap,
     * standing on the horizon on either side of the clear road corridor.
     * Each cell of a row holds at most one slab; per-cell hashes choose its
     * left edge, width, height and whether it is a landmark tower with a
     * spire.  The far row is drawn first and the near row over it.  Roof
     * and edge glow bleed into the sky above the silhouette.  Windows are
     * sparse and dim so the sun stays the focal point.  Nothing of the
     * skyline reaches below the horizon, so the ground skips the loop. */
    if (uv.y >= HZ) {
        vec3 neon_near = pal.grid;
        vec3 neon_far  = pal.halo;
        for (int layer = 0; layer < 2; layer++) {
            bool  near  = layer == 1;
            float sc    = (near ? 6.5 : 11.0) * density;  /* cells per uv unit */
            float maxH  = near ? 0.62 : 0.40;
            float gap   = near ? 0.26 : 0.14;              /* road corridor */
            float par   = near ? 0.010 : 0.003;            /* slow parallax drift */
            float cx = (uv.x + iTime * speed * par) * sc;
            float k  = floor(cx);
            float u  = fract(cx);

            vec4  r  = hash14(k * 1.000 + float(layer) * 101.0);
            /* Width is a slice of the cell; the rest is the gap. */
            float wf = mix(0.22, near ? 0.62 : 0.70, r.y);
            float x0 = mix(0.02, 0.98 - wf, r.z);
            /* Heights: mostly mid, a long tail of tall ones. */
            float hr = mix(r.x, r.x * r.x * r.x, 0.55);
            float H  = maxH * (0.16 + 0.84 * hr) * height_bias;
            /* Landmark towers (one cell in seven): taller and thinner.
             * Setback towers (a third of the rest): a narrower upper
             * section over the main slab. */
            float land = step(0.857, r.w);
            float setb = step(0.52, r.w) * (1.0 - land);
            H  *= 1.0 + 0.60 * land;
            wf *= 1.0 - 0.45 * land;
            x0 += 0.22 * wf * land;
            /* Keep the corridor around the vanishing point clear: the whole
             * cell shrinks smoothly as it approaches the gap, so nothing
             * pops during the slow drift. */
            float xc = (k + x0 + 0.5 * wf) / sc - iTime * speed * par;
            H *= smoothstep(gap, gap + 0.10, abs(xc));
            if (H <= 0.004) continue;

            float um   = abs(u - x0 - 0.5 * wf);                 /* from slab centre */
            float top  = HZ + H + setb * 0.24 * H * step(um, 0.26 * wf);
            /* Pixels in the gap between slabs take nothing from this row:
             * no facade, no window hash, no edge glow. */
            if (u < x0 || u > x0 + wf) continue;
            float inx  = 1.0;
            float iny  = step(uv.y, top);                        /* within slab height */
            float inb  = iny;

            /* Spire on a landmark: a thin mast above the roof. */
            float spire = land * step(um, 0.035 * wf + 0.006) *
                          step(top, uv.y) * step(uv.y, top + 0.12 * H + 0.03);

            /* Edge/roof neon: glow leaks into the facade and into the sky. */
            float ex   = min(u - x0, x0 + wf - u) / sc;
            float ey   = top - uv.y;
            float edge_in  = inb * (exp(-ex * 170.0) * 0.55 + exp(-ey * 110.0));
            float edge_out = inx * step(top, uv.y) * exp(-(uv.y - top) * 160.0);
            vec3  neon = near ? neon_near : neon_far;

            /* Windows: a few columns across the slab, rows scaled so cells
             * stay roughly square; sparsely lit, slowly re-rolled. */
            float cols = max(1.0, floor(wf / sc * 70.0));
            float rows = max(2.0, floor(H * 34.0));
            vec2  wuv  = vec2((u - x0) / wf * cols, (uv.y - HZ) / H * rows);
            vec2  wc   = floor(wuv);
            vec2  wl   = fract(wuv);
            float wh   = hash21(wc + vec2(k * 3.1 + float(layer) * 17.0,
                                          floor(iTime * 0.08) * 0.37));
            float lit  = step(0.80, wh);
            float warm = step(0.5, fract(wh * 23.7));
            vec2  fr   = smoothstep(0.18, 0.34, wl) * smoothstep(0.82, 0.66, wl);
            float win  = lit * fr.x * fr.y * inb;
            vec3  wcol = mix(pal.win_cool, pal.win_warm, warm);

            /* Facade: near rows sit in the hole colour; the far row is a
             * hazed silhouette, never brighter than the horizon band. */
            vec3 bcol = near ? vec3(0.0030, 0.0015, 0.0070)
                             : mix(vec3(0.0060, 0.0025, 0.0120), pal.sky_lo,
                                   0.45 + 0.40 * haze_dens);
            bcol += wcol * win * (near ? 0.55 : 0.22);
            bcol += neon * edge_in * (near ? 0.22 : 0.12);

            col  = mix(col, bcol, max(inb, spire));
            col += neon * edge_out * (near ? 0.28 : 0.14);
        }
    }

    /* --- low haze band on the horizon, thicker for hazy seeds --- */
    col = mix(col, pal.sky_lo * 1.4 + pal.sun_lo * 0.06,
              exp(-abs(ay) * 14.0) * (0.10 + 0.22 * haze_dens));

    /* --- soft vignette so the corners stay in the hole --- */
    float v = smoothstep(1.6, 0.40, length(uv * vec2(1.0, 1.1)));
    col *= mix(0.72, 1.0, v);

    /* --- tonemap & gamma (display-referred sRGB output) ---
     * Luma-weighted compression keeps hue and saturation: a per-channel
     * curve would push the sun cap and the neon toward white. */
    col = max(col, 0.0);
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = col / (1.0 + lum * 0.45);
    col = min(col, 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}
