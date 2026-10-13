// SPDX-License-Identifier: Apache-2.0
/* newton.frag — Newton-Raphson root-finding fractal.
 *
 * For z ← z − f(z)/f'(z) with f(z) = z^k − 1, the basin of attraction
 * partitions the complex plane into k regions (the roots are the k-th
 * roots of unity). Each pixel walks the iteration and is coloured by
 * which root its orbit converged to.
 *
 * Layout of u_params (vec4):
 *   x = k (degree, 3..7).
 *   y = jitter (small random perturbation to avoid stuck orbits).
 *   z = step clamp.
 *   w = unused.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


void main() {
    int k = int(u_params.x + 0.5);
    if (k < 3) k = 3;
    if (k > 7) k = 7;
    float jitter = u_params.y;
    float step_clamp = u_params.z;

    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    /* Slow rotation and a gentle zoom breath, so the basins drift instead of
     * sitting still for the whole cycle. */
    float tm = u_time * u_speed;
    float ra = tm * 0.12;
    p = mat2(cos(ra), sin(ra), -sin(ra), cos(ra)) * p;
    vec2 z = p * 2.0 * (1.0 + 0.18 * sin(tm * 0.2));
    /* Deterministic jitter to break stuck orbits at the boundary. */
    z += jitter * vec2(sin(p.x * 23.7), cos(p.y * 17.3));

    float trap = 1.0e9;
    int root_idx = -1;
    float dist_to_root = 1.0e9;
    int max_iter = u_iterations;

    for (int i = 0; i < 64; i++) {
        if (i >= max_iter) break;

        /* Evaluate f(z) = z^k − 1 and f'(z) = k · z^(k−1). Both via
         * iterative multiplication. */
        vec2 fz = vec2(1.0, 0.0);
        vec2 fp = vec2(1.0, 0.0);
        for (int j = 0; j < 8; j++) {
            if (j == k) { fz = vec2(fz.x - 1.0, fz.y); }
            if (j == k - 1) { fp = vec2(float(k) * fp.x, float(k) * fp.y); }
            if (j >= k) break;
            fz = vec2(fz.x * z.x - fz.y * z.y,
                      fz.x * z.y + fz.y * z.x);
            if (j < k - 1) {
                fp = vec2(fp.x * z.x - fp.y * z.y,
                          fp.x * z.y + fp.y * z.x);
            }
        }
        /* Handle the final step (j == k). */
        fz = vec2(fz.x * z.x - fz.y * z.y,
                  fz.x * z.y + fz.y * z.x);
        fz = vec2(fz.x - 1.0, fz.y);

        /* z ← z − f(z) / f'(z). Complex division: (a + ib) / (c + id)
         *   = ((ac + bd) + i(bc − ad)) / (c² + d²). */
        float denom = fp.x * fp.x + fp.y * fp.y;
        if (denom < 1e-10) denom = 1e-10;
        vec2 step = vec2(
            (fz.x * fp.x + fz.y * fp.y) / denom,
            (fz.y * fp.x - fz.x * fp.y) / denom
        );
        /* Step clamp to avoid runaway divergence. */
        float sl = length(step);
        if (sl > step_clamp) step *= step_clamp / max(sl, 1e-6);
        z -= step;

        float d2 = dot(z, z);
        trap = min(trap, d2);
        /* Convergence: which root of unity are we closest to? */
        float best_d = 1.0e9;
        for (int r = 0; r < 8; r++) {
            if (r >= k) break;
            float ang = 6.2831853 * float(r) / float(k);
            vec2 root_z = vec2(cos(ang), sin(ang));
            float dd = dot(z - root_z, z - root_z);
            if (dd < best_d) { best_d = dd; root_idx = r; }
        }
        dist_to_root = best_d;
        if (best_d < 1e-5) break;
    }

    /* Map root_idx to a colour in [0,1] using the root index + the
     * distance (lighter colour near the convergence point). */
    float col;
    if (root_idx >= 0) {
        col = (float(root_idx) + 0.5) / float(k);
        col = mix(col, 1.0 - dist_to_root * 2.0, 0.3);
    } else {
        col = 0.5 + 0.2 * exp(-sqrt(trap));
    }
    col = clamp(col, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}