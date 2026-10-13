// SPDX-License-Identifier: Apache-2.0
/* plasma.frag — midpoint-displacement plasma cloud.
 *
 * A pure 1D/2D fragment-shader version of the classic plasma effect:
 * we walk the pixel coordinate through a noise-driven iterative
 * function that produces a fractal plasma cloud. Deterministic per
 * family cycle.  The sine sum is domain-warped by its own first octaves
 * and pushed through a contrast curve, so the cloud has folds and
 * filaments instead of settling into a flat pastel gradient.
 *
 * Layout of u_params (vec4):
 *   x = roughness (0..1).
 *   y, z, w = phase offsets.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


float f5_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 p = fract_fx_fold((gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y);
    /* Roughness floor raised from 0.4: below it the first octave owns the
     * picture and the cloud is one soft gradient. */
    float rough = clamp(u_params.x, 0.55, 0.9);
    float ph0 = u_params.y;
    float ph1 = u_params.z;
    float ph2 = u_params.w;

    /* Midpoint displacement surrogate: sum of sin waves at
     * increasing frequencies. */
    float v = 0.0;
    float amp = 1.0;
    float freq = 1.0;
    vec2 q = p;
    /* Slow rotation of the coordinate system. */
    float ang = u_time * u_speed * 0.07 + ph0;
    float cr = cos(ang), si = sin(ang);
    q = vec2(cr * q.x - si * q.y, si * q.x + cr * q.y);
    float drift = u_time * u_speed * 0.15;
    for (int i = 0; i < 8; i++) {
        float o = amp * (sin(freq * (q.x + ph1) + drift) +
                         sin(freq * (q.y + ph2) - drift * 0.7) +
                         sin(freq * (q.x + q.y + ph0))) * 0.33;
        v += o;
        /* Domain warp: the coarse octaves bend the fine ones. */
        if (i < 3) q += vec2(o * 0.35, -o * 0.25);
        amp *= rough;
        freq *= 2.0;
    }
    v = v * 0.5 + 0.5;
    /* Contrast curve around the mean so the palette's dark and bright
     * bands both appear; the wrap keeps it inside the LUT. */
    v = 0.5 + (v - 0.5) * 1.8;
    v = abs(fract(v * 0.5 + 0.5) * 2.0 - 1.0);
    v = clamp(v, 0.0, 1.0);
    if (!fract_finite(v)) v = 0.0;

    vec3 rgb = fract_palette(v);
    rgb = fract_fx_post(rgb * u_exposure, gl_FragCoord.xy);

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}