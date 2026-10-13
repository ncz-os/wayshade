// SPDX-License-Identifier: Apache-2.0
/* lyapunov.frag — Lyapunov exponent over a 1D binary map.
 *
 * Renders the Lyapunov exponent of the 1D map x ← a·x·(1−x), with the
 * parameter pair (a, b) alternating per bit of a sequence. The
 * exponent sign (positive = chaotic, negative = stable) classifies
 * the pixel.
 *
 * Layout of u_params (vec4):
 *   x = a parameter (3.4..4.0).
 *   y = b parameter (3.4..4.0).
 *   z = sequence length (4..16).
 *   w = unused.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


void main() {
    float tm = u_time * u_speed;
    float a = clamp(u_params.x + 0.12 * sin(tm * 0.2), 3.0, 4.0);
    float b = clamp(u_params.y + 0.12 * cos(tm * 0.13), 3.0, 4.0);
    int seq_len = int(u_params.z + 0.5);
    if (seq_len < 4) seq_len = 4;
    if (seq_len > 16) seq_len = 16;

    /* Pixel x = sequence parameter (between 0 and 1); pixel y =
     * initial-condition x0. */
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    float seed_x = u_seed.x;
    /* Build the sequence by reading bits from seed_x and p.x. */
    bool seq[16];
    float mix_in = seed_x;
    for (int i = 0; i < 16; i++) {
        if (i >= seq_len) break;
        mix_in = fract(mix_in * 1.31 + p.x * 7.13);
        seq[i] = mix_in > 0.5;
    }

    /* Walk the map and accumulate log |f'(x)|. */
    float x = 0.5 + 0.49 * p.y; /* keep x in (0, 1) for stability */
    float lyap = 0.0;
    int n_iter = 256;
    if (n_iter > u_iterations) n_iter = u_iterations;
    for (int i = 0; i < 256; i++) {
        if (i >= n_iter) break;
        /* Pick a or b based on the sequence. */
        bool which = seq[i % seq_len];
        float k = which ? a : b;
        float fx = k * x * (1.0 - x);
        float fpx = k * (1.0 - 2.0 * x);
        float abs_fpx = abs(fpx);
        if (abs_fpx > 1e-10) lyap += log(abs_fpx);
        x = fx;
        if (!fract_finite(x) || x < 0.0 || x > 1.0) break;
    }

    /* Normalize: lyap ≈ ±0.693 in the chaotic band. */
    float col = clamp(0.5 + lyap / float(n_iter) * 0.5, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}