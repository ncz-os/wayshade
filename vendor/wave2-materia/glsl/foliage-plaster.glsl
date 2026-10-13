// SPDX-License-Identifier: Apache-2.0
// foliage-plaster.glsl
// wave2 — quiet materiality: foliage shadows on plaster.
//
// Algorithm:
//   1. The receiving surface is warm uneven plaster — pale cream
//      with a soft low-frequency variation so it doesn't read as a
//      flat wall. No animation of the plaster itself.
//   2. Foliage shadows cast onto the plaster, at three scales:
//      - LARGE: a few big blurred shadow patches that drift slowly
//        across the wall (tens of seconds).
//      - MEDIUM: many leaf-cluster shapes (~30) at regular positions,
//        each wobbling on its own noise-driven trajectory (a few
//        seconds).
//      - SMALL: tiny opening spots of bright light that flicker on
//        and off (occasional 1-2 s flashes).
//   3. Soft penumbra everywhere. No hard outlines. Shadows blend with
//      each other and the plaster.
//   4. Noise-driven wobble (NOT uniform sinusoidal bobbing — that's
//      explicitly rejected by the brief).
//
// Tonemap (ACES) + gamma.

#define PI 3.14159265359

// ---- Hash / value noise ----
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm4(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 4; i++) {
        s += a * vnoise(p);
        p = rot * p * 2.07 + vec2(13.1, 7.7);
        a *= 0.5;
    }
    return s;
}

// Soft "leaf" shape: elongated ellipse with smooth radial falloff.
float soft_leaf(vec2 p, vec2 pos, float angle, float len, float wid) {
    vec2 d = p - pos;
    float cs = cos(-angle), sn = sin(-angle);
    vec2 q = vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
    float r = length(vec2(q.x / len, q.y / wid));
    return 1.0 - smoothstep(0.4, 1.6, r);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 res = iResolution.xy;
    vec2 uv = (fragCoord - 0.5 * res) / min(res.x, res.y);
    float t = iTime;

    // ---- Plaster base ----
    // Warm cream plaster, slightly uneven. Linear brightness around
    // 0.50-0.65 so there's room for shadows to darken without going
    // to ink.
    float plaster_var = fbm4(uv * 1.3 + vec2(0.0, t * 0.005));
    vec3 plaster = mix(vec3(0.58, 0.50, 0.40),
                       vec3(0.78, 0.68, 0.52),
                       plaster_var);
    plaster *= 0.92 + 0.10 * fbm4(uv * 8.0);

    // ---- LARGE scale: a few big blurred shadow patches ----
    // These drift slowly (tens of seconds) and provide the broad
    // dappled effect.
    float shadow = 0.0;
    for (int i = 0; i < 4; i++) {
        float fi = float(i);
        // Big patch anchor.
        vec2 base = vec2(
            (hash21(vec2(fi, 1.0)) * 2.0 - 1.0) * 1.0,
            (hash21(vec2(fi, 2.0)) * 2.0 - 1.0) * 0.7
        );
        // Slow drift — the patch moves on its own noise trajectory.
        vec2 drift = vec2(
            fbm4(vec2(fi, t * 0.04)) - 0.5,
            fbm4(vec2(fi + 23.0, t * 0.05)) - 0.5
        ) * 0.7;
        vec2 pos = base + drift;
        float angle = (hash21(vec2(fi, 3.0)) - 0.5) * PI;
        float len = 0.7 + hash21(vec2(fi, 4.0)) * 0.4;
        float wid = 0.35 + hash21(vec2(fi, 5.0)) * 0.2;
        shadow += soft_leaf(uv, pos, angle, len, wid) * 0.45;
    }

    // ---- MEDIUM scale: leaf clusters ----
    // 8x6 = 48 anchor points, each with 1 leaf + a satellite. The
    // grid covers the visible frame plus a margin so clusters drift
    // in/out from off-screen edges.
    const int NX = 8;
    const int NY = 6;
    for (int iy = 0; iy < NY; iy++) {
        for (int ix = 0; ix < NX; ix++) {
            float fi = float(iy * NX + ix);
            vec2 anchor = vec2((float(ix) - 3.0) * 0.55,
                               (float(iy) - 2.0) * 0.45);

            float h1 = hash21(vec2(fi, 1.0));
            float h2 = hash21(vec2(fi, 2.0));
            float h3 = hash21(vec2(fi, 3.0));
            float h4 = hash21(vec2(fi, 4.0));

            // Per-anchor wobble (own noise trajectory).
            vec2 wobble = vec2(
                fbm4(vec2(fi, t * 0.13)) - 0.5,
                fbm4(vec2(fi + 23.0, t * 0.11)) - 0.5
            ) * 0.40;
            vec2 pos = anchor + wobble;

            float baseAngle = h1 * PI * 2.0;
            // The leaf is longer than it is wide — gives it the
            // elongated character of a real leaf rather than a blob.
            // Per-leaf weight tuned so leaves read as soft shapes,
            // not ink stains. The medium-scale cluster is the main
            // contributor; the satellite is a soft accent.
            float len1 = 0.26 + h2 * 0.20;
            float wid1 = 0.05 + h3 * 0.04;
            shadow += soft_leaf(uv, pos, baseAngle, len1, wid1) * 0.32;

            // Satellite: smaller, different angle.
            vec2 satOff = vec2(h2 - 0.5, h4 - 0.5) * 0.28;
            float satAngle = baseAngle + (h4 - 0.5) * 2.5 + t * (0.04 + h1 * 0.05);
            float len2 = 0.16 + h1 * 0.12;
            float wid2 = 0.04 + h4 * 0.03;
            shadow += soft_leaf(uv, pos + satOff, satAngle, len2, wid2) * 0.22;
        }
    }

    // Clamp.
    shadow = clamp(shadow, 0.0, 1.4);

    // ---- SMALL scale: light openings ----
    // Tiny bright spots where light breaks through the canopy. Few
    // in number, occasionally visible, drifting fast.
    float opening = 0.0;
    for (int k = 0; k < 10; k++) {
        float fk = float(k);
        vec2 opos = vec2(
            (hash21(vec2(fk, 6.0)) * 2.4 - 1.2),
            (hash21(vec2(fk, 7.0)) * 1.6 - 0.8)
        );
        opos += vec2(fbm4(vec2(fk, t * 0.6)) - 0.5,
                     fbm4(vec2(fk + 11.0, t * 0.6)) - 0.5) * 0.08;
        float dist = length((uv - opos) * vec2(1.0, 1.2));
        // Flicker on/off per spot at 3-7 second periods.
        float flick = step(0.5, fract(t * (0.14 + hash21(vec2(fk, 8.0)) * 0.25) + fk * 0.31));
        opening += flick * (1.0 - smoothstep(0.0, 0.035, dist)) * 0.55;
    }

    // ---- Compose ----
    vec3 col = plaster;
    // Plaster darkened by shadow. The shadow tint is a slightly
    // darker/cooler version of the plaster (B>G>R), so shadows on
    // warm plaster look natural — NOT cool grey.
    vec3 shadowTint = vec3(0.50, 0.55, 0.65);
    col = mix(col, col * shadowTint, clamp(shadow, 0.0, 1.0));
    // Light openings: bright warm cream patches.
    col += opening * vec3(0.45, 0.40, 0.32);

    // ---- Vignette ----
    float vig = 1.0 - 0.15 * dot(uv, uv);
    col *= vig;

    // ---- Tonemap (ACES Narkowicz) + gamma ----
    vec3 a = col * (2.51 * col + 0.03);
    vec3 b = col * (2.43 * col + 0.59) + 0.14;
    vec3 mapped = clamp(a / b, 0.0, 1.0);
    fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}