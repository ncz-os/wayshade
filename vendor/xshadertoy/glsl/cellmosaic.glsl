// SPDX-License-Identifier: Apache-2.0
// Title:  Cell Mosaic
// Desc:   A vivid cellular mosaic that fills the entire frame. Irregular
//         convex cells, each a saturated flat-ish colour, with visible
//         boundaries between them. Cells drift and slowly rearrange so
//         the tessellation is never static.
//
//         Original work. Published mathematics only (Worley/Voronoi
//         cellular noise; jittered-grid acceleration; integer hashing
//         via prime-multiplication + xor). No reference to any
//         external shader source — see vendor/xshadertoy/PORTED.md
//         note on clean-room derivation.
//
//         Per-run variation driven entirely by iSeed: palette family
//         (hue base + saturation/value curves), cell density (grid
//         scale), drift speed, and the strength of a domain-warp
//         pre-pass that distorts cell shape from strictly convex to
//         slightly elongated. Two runs at the same frame index will
//         differ in palette AND topology AND drift, not merely in
//         animation phase.

#define PI          3.14159265359
#define TAU         6.28318530718
#define HASH_PRIME1 1597334673u    /* 32-bit prime multipliers for */
#define HASH_PRIME2 3812015801u    /* the integer hash below. */

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Integer hash. 2D -> 1D, deterministic, fast.                       */
/*                                                                    */
/* Reference mathematics: Houlbert's "Texture filtering" survey cites  */
/* Teschner et al. ("Optimized Spatial Hashing for Collision Detection */
/* of Deformable Objects", VMV 2003) as the canonical 3D-into-1D       */
/* integer hash pattern (bit-shift + multiply + xor). We use the same  */
/* scheme in 2D. The output is in [0, 1) by dividing by 2^32.          */
/* ----------------------------------------------------------------- */
float hash21(vec2 p) {
    uvec2 ip = uvec2(int(p.x) | 0, int(p.y) | 0);
    uint h = ip.x * HASH_PRIME1 ^ (ip.y * HASH_PRIME2);
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;
    return float(h) * (1.0 / 4294967296.0);
}

/* 2D -> 2D hash: two independent scalars via two different xor masks.
 * Both components live in [0, 1)^2. */
vec2 hash22(vec2 p) {
    return vec2(hash21(p), hash21(p + vec2(17.13, 91.71)));
}

/* ----------------------------------------------------------------- */
/* HSV -> RGB. Standard formula (Foley-vanDam, "Computer Graphics:    */
/* Principles and Practice", 1990, ch. 13). All three components in   */
/* [0,1]; h in [0,1) cycles the hue wheel.                            */
/* ----------------------------------------------------------------- */
vec3 hsv2rgb(vec3 hsv) {
    vec3 k = vec3(1.0, 2.0 / 3.0, 1.0 / 3.0);
    vec3 p = abs(fract(hsv.xxx + k) * 6.0 - 3.0);
    return hsv.z * mix(vec3(1.0), saturate(p - 1.0), hsv.y);
}

/* ----------------------------------------------------------------- */
/* Jittered-grid Voronoi (Worley 1996; Bridson 2007 sec. 4.1).         */
/*                                                                    */
/* Given a coordinate p in 2D, find the jittered vertex in the        */
/* neighbouring 3x3 grid cells (9 candidates) whose distance to p     */
/* is smallest (F1) and second-smallest (F2). The cell identity is    */
/* the integer cell coordinate of the winner — used to derive the     */
/* per-cell hue, drift direction, and warp magnitude.                 */
/* ----------------------------------------------------------------- */
struct Voronoi {
    float f1;       /* nearest distance      */
    float f2;       /* second-nearest dist.  */
    vec2  cell;     /* cell index of winner  */
};

Voronoi voronoi(vec2 p, float time, vec2 seed_dir) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    Voronoi v;
    v.f1 = 1e9;
    v.f2 = 1e9;
    /* scan the 3x3 neighbourhood; one of these is guaranteed to hold
     * the closest vertex regardless of how the jitter went. */
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 g = vec2(float(i), float(j));
            vec2 cell = ip + g;
            /* jitter offset: base + per-vertex drift. Jitter is
             * multiplied by 0.95 so the vertex can land anywhere
             * within the unit cell including near the boundary;
             * this is what makes the cells irregularly shaped
             * rather than axis-aligned rectangles. */
            vec2 jitter = hash22(cell) - 0.5;
            /* slow drift so cells rearrange over a ~30s timescale */
            vec2 drift = 0.5 * vec2(
                sin(time * (0.07 + 0.13 * hash21(cell))),
                cos(time * (0.05 + 0.11 * hash21(cell + 7.3)))
            );
            /* vertex position in CELL-LOCAL coords (relative to
             * cell centre at g + 0.5). NO fract wrap here -- the
             * wrap was a bug, it made every cell identical to
             * cell (0,0) and broke the Voronoi topology. The 3x3
             * neighbour scan handles drift that exceeds the cell
             * boundary automatically. */
            vec2 r = g + 0.5 + 0.95 * jitter + drift * seed_dir.x - p;
            float d = dot(r, r);
            if (d < v.f1) {
                v.f2 = v.f1;
                v.f1 = d;
                v.cell = cell;
            } else if (d < v.f2) {
                v.f2 = d;
            }
        }
    }
    v.f1 = sqrt(v.f1);
    v.f2 = sqrt(v.f2);
    return v;
}

/* ----------------------------------------------------------------- */
/* mainImage — the cellmosaic composition.                            */
/* ----------------------------------------------------------------- */
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* aspect-corrected, centred coords; we map to a fixed cell scale
     * in pixels-per-cell so density doesn't depend on resolution. */
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;

    /* --- per-run variation derived from iSeed --- */
    /* iSeed is four floats in [0,1); we use each channel for a
     * different axis of variation so two runs differ in palette AND
     * density AND drift AND warp. */
    float density_lo = 5.0;       /* ~5 cells across the short axis at the sparse end */
    float density_hi = 16.0;      /* dense pack at the high end      */
    float cells_per_unit = mix(density_lo, density_hi,
                              mix(0.30, 0.85, iSeed.x));

    /* palette base: spread hues across the wheel but cluster them
     * near the iSeed-driven base so the family is recognisable. */
    float hue_base   = iSeed.y;
    float hue_spread = 0.55 + 0.40 * iSeed.z;   /* 0.55..0.95 of a turn */
    float sat        = mix(0.65, 0.95, iSeed.w); /* saturated per spec */
    float val        = mix(0.78, 0.98, fract(iSeed.x + iSeed.w));

    /* domain warp: a low-frequency sinusoidal distortion of the
     * Voronoi input coords. Strength varies per run; the frequency
     * is keyed off cells_per_unit so each cell still reads as a
     * single Voronoi region but the boundaries curve. */
    float warp_strength = mix(0.03, 0.15, fract(iSeed.y + 0.3));
    vec2 warp = warp_strength * vec2(
        sin(uv.y * 4.5 + iSeed.x * 7.0),
        cos(uv.x * 3.7 + iSeed.z * 5.0)
    );

    /* drift axis: scale per-vertex drift speed by a per-run factor */
    float drift_scale = mix(0.6, 1.4, fract(iSeed.z * 1.7));
    vec2 seed_dir = vec2(drift_scale, drift_scale * 0.83);

    /* --- compute Voronoi at the warped, scaled coord --- */
    float t = iTime * 0.25;            /* slow master clock */
    /* Apply warp BEFORE scaling by cells_per_unit, so the warp
     * magnitude is a fraction of the screen rather than a fraction
     * of the lattice. This keeps the cell topology intact while
     * still letting the cells stretch and curve. */
    vec2 p = (uv + warp) * cells_per_unit;
    Voronoi v = voronoi(p, t, seed_dir);

    /* --- boundary: F2-F1 small means we are near a cell wall --- */
    float edge = v.f2 - v.f1;
    float line_width = 0.06 + 0.04 * iSeed.w;
    float wall = smoothstep(line_width, 0.0, edge);  /* 1 at wall, 0 deep */

    /* --- per-cell colour --- */
    float h = hue_base + hue_spread * hash21(v.cell);
    /* gentle per-cell value wiggle so flat-ish ≠ perfectly flat */
    float cell_v = val * (0.88 + 0.12 * hash21(v.cell + 5.7));
    vec3 cell_rgb = hsv2rgb(vec3(fract(h), sat, cell_v));

    /* darken at the wall so boundaries read clearly without
     * flashing; wall colour pulls toward a darker version of the
     * neighbouring cell colour, not toward black, to preserve
     * colourfulness at the cell edges. */
    vec3 wall_rgb = cell_rgb * 0.35;
    vec3 col = mix(cell_rgb, wall_rgb, wall);

    /* vignette: gentle, prevents the very corners from feeling
     * washed-out at high iSeed values. Smoothstep, not a step, so
     * no banding. */
    float vig = smoothstep(1.20, 0.40, length(uv));
    col *= mix(0.85, 1.0, vig);

    fragColor = vec4(col, 1.0);
}
