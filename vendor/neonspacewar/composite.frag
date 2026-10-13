// SPDX-License-Identifier: Apache-2.0
#version 300 es
/* composite.frag — final post-process for the neon vector
 *                  rock-shooter.
 *
 * Reads the trail FBO (an RGBA8 buffer where each frame added its
 * line entities additively over the previous faded contents) and
 * applies, in order:
 *
 *   1. Sample with a 5-tap blur kernel to fake bloom.
 *   2. Channel-split the result for chromatic aberration: sample
 *      R/G/B at slightly different offsets. The offset scales with
 *      u_chroma and u_wave so it intensifies with on-screen action.
 *   3. Apply a per-palette colour wash: remap the luminance through
 *      a per-launch palette so the whole frame drifts in hue.
 *   4. Warp the UVs radially outward from each active shockwave
 *      (u_shockwaves: vec4 array of (x, y, age, strength)).
 *   5. Tone-map + gamma + clamp to [0,1] for the default fb.
 *
 * Performance: 12 texture taps per pixel is the budget. On Intel
 * UHD at 1920x1080, the lavafield hack (also fullscreen quad +
 * 4-5 tap SDF) runs at vsync; this stays in the same budget.
 */

precision mediump float;

in vec2 v_uv;
out vec4 o_col;

uniform sampler2D u_trail;
uniform vec2  u_resolution;
uniform float u_time;
uniform float u_palette_phase;
uniform float u_palette_idx;
uniform float u_chroma;
uniform float u_bloom;
uniform float u_wave;
uniform vec4  u_shockwaves[8];   /* xy=origin, z=age[0..1], w=strength */
uniform int   u_shockwave_count;

/* -------------------------------------------------------------------- */
/* HSL → RGB                                                              */
/* -------------------------------------------------------------------- */
vec3 hsl_to_rgb(float h, float s, float l) {
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float hh = mod(h * 6.0, 6.0);
    float x = c * (1.0 - abs(mod(hh, 2.0) - 1.0));
    vec3 rgb;
    if      (hh < 1.0) rgb = vec3(c, x, 0.0);
    else if (hh < 2.0) rgb = vec3(x, c, 0.0);
    else if (hh < 3.0) rgb = vec3(0.0, c, x);
    else if (hh < 4.0) rgb = vec3(0.0, x, c);
    else if (hh < 5.0) rgb = vec3(x, 0.0, c);
    else               rgb = vec3(c, 0.0, x);
    float m = l - 0.5 * c;
    return rgb + m;
}

/* -------------------------------------------------------------------- */
/* Palette                                                               */
/* -------------------------------------------------------------------- */
/* Six named palettes, indexed by u_palette_idx (set per launch). Each
 * one is defined by a stop list of (hue, sat, lum). The shader reads
 * the input luminance, maps through palette_idx and palette_phase
 * drift, and outputs a colour wash. */
struct Palette { vec3 c0; vec3 c1; vec3 c2; };

Palette palette_a(int idx) {
    /* Each entry: three palette stops, evaluated in HSL. */
    if (idx == 0) {
        /* Cyan/magenta electric. */
        return Palette(vec3(0.50, 0.6, 0.20),
                       vec3(0.85, 0.7, 0.50),
                       vec3(0.10, 0.8, 0.65));
    } else if (idx == 1) {
        /* Classic arcade green-on-black-ish, but warm. */
        return Palette(vec3(0.30, 0.7, 0.18),
                       vec3(0.45, 0.7, 0.45),
                       vec3(0.10, 0.7, 0.30));
    } else if (idx == 2) {
        /* Hot pink → orange → red. */
        return Palette(vec3(0.93, 0.7, 0.55),
                       vec3(0.05, 0.8, 0.55),
                       vec3(0.98, 0.7, 0.45));
    } else if (idx == 3) {
        /* Ultraviolet. */
        return Palette(vec3(0.70, 0.6, 0.20),
                       vec3(0.78, 0.7, 0.50),
                       vec3(0.62, 0.6, 0.35));
    } else if (idx == 4) {
        /* Acid yellow / red warning. */
        return Palette(vec3(0.13, 0.9, 0.55),
                       vec3(0.07, 0.8, 0.45),
                       vec3(0.98, 0.8, 0.45));
    } else {
        /* Psychedelic — wraps around the hue wheel. */
        return Palette(vec3(0.00, 0.8, 0.40),
                       vec3(0.50, 0.8, 0.55),
                       vec3(0.85, 0.8, 0.45));
    }
}

/* Map luminance l in [0,1] to a palette colour, with hue drift from
 * u_palette_phase. */
vec3 palette_map(float l, float phase, int idx) {
    Palette p = palette_a(idx);
    /* Stop positions are 0, 0.5, 1.0 (c0, c1, c2). */
    float t = clamp(l, 0.0, 1.0);
    vec3 a, b;
    float local;
    if (t < 0.5) { a = p.c0; b = p.c1; local = t * 2.0; }
    else         { a = p.c1; b = p.c2; local = (t - 0.5) * 2.0; }
    /* Drift hue linearly over the whole frame's lifetime. */
    vec3 ah = a;
    vec3 bh = b;
    ah.x = mod(a.x + phase * 0.6, 1.0);
    bh.x = mod(b.x + phase * 0.6, 1.0);
    vec3 ca = hsl_to_rgb(ah.x, ah.y, ah.z);
    vec3 cb = hsl_to_rgb(bh.x, bh.y, bh.z);
    return mix(ca, cb, local);
}

/* -------------------------------------------------------------------- */
/* Bloom (5-tap separable-ish)                                           */
/* -------------------------------------------------------------------- */
vec3 bloom_sample(vec2 uv) {
    /* 5-tap small kernel for the bright halo around hot lines. */
    vec2 px = 1.0 / u_resolution;
    vec3 c = texture(u_trail, uv).rgb * 0.36;
    c += texture(u_trail, uv + vec2( 1.5,  0.0) * px).rgb * 0.16;
    c += texture(u_trail, uv + vec2(-1.5,  0.0) * px).rgb * 0.16;
    c += texture(u_trail, uv + vec2( 0.0,  1.5) * px).rgb * 0.16;
    c += texture(u_trail, uv + vec2( 0.0, -1.5) * px).rgb * 0.16;
    return c;
}

/* -------------------------------------------------------------------- */
/* Chromatic aberration                                                   */
/* -------------------------------------------------------------------- */
vec3 chroma_sample(vec2 uv, vec2 dir, float strength) {
    /* Sample R/G/B with progressive offset along dir; gives the
     * classic "lens fringe" look. Strength scales linearly. */
    vec2 px = 1.0 / u_resolution;
    vec2 o = dir * px * strength;
    vec3 c;
    c.r = bloom_sample(uv - o * 1.2).r;
    c.g = bloom_sample(uv          ).g;
    c.b = bloom_sample(uv + o * 1.2).b;
    return c;
}

/* -------------------------------------------------------------------- */
/* Shockwave UV warp                                                      */
/* -------------------------------------------------------------------- */
vec2 warp_uv(vec2 uv) {
    vec2 px = 1.0 / u_resolution;
    vec2 d = vec2(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= u_shockwave_count) break;
        vec4 s = u_shockwaves[i];
        vec2 origin = (s.xy * 0.5 + 0.5); /* map [-1,1] to [0,1] */
        float age = s.z;
        float str = s.w;
        vec2 rel = uv - origin;
        float dist = length(rel);
        /* Ring of influence expands outward over the wave's life. */
        float radius = age * 0.45;
        float falloff = exp(-pow((dist - radius) * 18.0, 2.0));
        /* Push outward along rel, attenuated by strength * falloff. */
        if (dist > 1e-5) {
            d += (rel / dist) * falloff * str * 0.020;
        }
    }
    return uv + d;
}

/* -------------------------------------------------------------------- */
/* Main                                                                   */
/* -------------------------------------------------------------------- */
void main() {
    vec2 uv = v_uv;

    /* Apply shockwave warp first; the chromatic offsets are computed
     * in the warped UV space so the fringes follow the warp. */
    uv = warp_uv(uv);

    /* Chromatic aberration direction: radially outward from centre,
     * with a subtle tangential swirl. The strength scales with
     * u_chroma (set by driver) and u_wave (later waves = more). */
    vec2 centred = uv - vec2(0.5);
    vec2 dir = centred;
    vec2 tang = vec2(-centred.y, centred.x);
    dir = normalize(dir + tang * 0.15);
    float chroma_strength = u_chroma * u_resolution.x * 0.5;

    vec3 c = chroma_sample(uv, dir, chroma_strength);

    /* The trail FBO's brightness already encodes intensity (additive
     * accumulation). Map through the palette to recolour. */
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    /* Boost highlights a touch so saturated lines glow rather than
     * wash out at the top of the palette. */
    lum = pow(lum, 0.85);
    vec3 col = palette_map(lum, u_palette_phase, int(u_palette_idx));

    /* Mix palette back with the source — keep the line colour
     * readable but tint everything by the palette. The mix
     * favours the palette at low luminance and the original at
     * high luminance (so hot cores stay vivid). */
    vec3 mixed = mix(col * (lum + 0.05), c, smoothstep(0.0, 0.6, lum));

    /* Tone map (Reinhard) + soft saturation lift. */
    vec3 tm = mixed / (mixed + 0.6);
    /* Boost saturation slightly. */
    float lum2 = dot(tm, vec3(0.299, 0.587, 0.114));
    tm = mix(vec3(lum2), tm, 1.15);

    /* Bloom add-back: if the original is much brighter than the
     * palette-mapped version, layer the bright halo back in. */
    float bright = max(max(c.r, c.g), c.b);
    tm += c * smoothstep(0.6, 1.6, bright) * (u_bloom - 0.5);

    /* Gamma. */
    tm = pow(clamp(tm, 0.0, 1.0), vec3(1.0 / 2.2));

    /* A faint vignette so the centre is brighter than the edges. */
    float vig = 1.0 - dot(centred, centred) * 0.6;
    tm *= clamp(vig, 0.55, 1.0);

    o_col = vec4(tm, 1.0);
}
