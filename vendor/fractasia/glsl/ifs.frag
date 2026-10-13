// SPDX-License-Identifier: Apache-2.0
/* ifs.frag — Iterated Function System (Barnsley-style) with four
 *            classic fractal-flame-style affine maps.
 *
 * For each pixel, accumulate the density contributed by walking N
 * iterations of the IFS. Each iteration picks one of the four maps
 * based on a deterministic per-pixel probability.
 *
 * Layout of u_params (vec4):
 *   x = sub-type (0..3 selects the canonical IFS family).
 *   y = roughness (0..1).
 *   z = density scale.
 *   w = unused.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

void f5_apply(int sub, vec2 p_in, out vec2 p_out) {
    float a = 0.0, b = 0.0, d = 0.0, e = 0.0;
    vec2 off = vec2(0.0);
    if (sub == 0) {
        /* Sierpinski triangle. */
        a = 0.5; b = 0.0; d = 0.5; e = 0.0;
        off = vec2(0.0, 0.5);
    } else if (sub == 1) {
        /* Barnsley fern (lightly rotated). */
        a = 0.85; b = 0.04; d = -0.04; e = 0.85;
        off = vec2(0.0, 0.5);
    } else if (sub == 2) {
        /* Maple leaf. */
        a = 0.4; b = -0.4; d = 0.4; e = 0.4;
        off = vec2(0.3, 0.3);
    } else {
        /* Crystal. */
        a = 0.6; b = 0.0; d = 0.0; e = -0.6;
        off = vec2(0.5, 0.5);
    }
    p_out = vec2(a * p_in.x + b * p_in.y,
                 d * p_in.x + e * p_in.y) + off;
}

void main() {
    int sub = int(u_params.x + 0.5) % 4;
    float rough = clamp(u_params.y, 0.1, 1.0);

    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

    /* Deterministic per-pixel hash from pixel coord + u_seed. */
    float h = fract(sin(dot(p + u_seed.xy * 100.0, vec2(12.9898, 78.233))) * 43758.5453);

    /* Walk N IFS steps and accumulate the running x-position. The
     * final colour is the running position magnitude. */
    float acc = 0.0;
    /* The hash above stays on the fixed pixel position (a rotating hash would
     * shimmer); the orbit's starting point rotates and breathes instead. */
    float tm = u_time * u_speed;
    float ra = tm * 0.1;
    vec2 q = mat2(cos(ra), sin(ra), -sin(ra), cos(ra)) * p * (1.0 + 0.15 * sin(tm * 0.18));
    for (int i = 0; i < 32; i++) {
        h = fract(sin(h * 12.9898 + 78.233) * 43758.5453);
        int sub_step = sub + int(h * 4.0);
        vec2 q2;
        f5_apply(sub_step % 4, q, q2);
        q = q2;
        acc += abs(q.x) + abs(q.y);
    }

    float col = acc * 0.05 + 0.1 * rough;
    col = clamp(col, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}