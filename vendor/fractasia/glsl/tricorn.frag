// SPDX-License-Identifier: Apache-2.0
/* tricorn.frag — z ← conj(z)² + c, c the pixel.
 *
 * The tricorn (Mandelbar) conjugates z before squaring, which breaks the
 * bilateral symmetry of the Mandelbrot set and gives the three-cornered
 * boundary.  The window comes from the driver's view picker (see
 * escape_time.frag); colour and effects are the shared fract_escape_shade.
 * The map is not holomorphic, so the distance estimate uses the z² + c
 * derivative recurrence on the conjugate: an approximation that still
 * draws the outline where the boundary is.
 *
 * Layout of u_params (vec4): centre.x, centre.y, zoom at the cycle start,
 * rotation.  u_view: cycle start time, log2 zoom per second.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

void main() {
    vec2 c = fract_view_point(gl_FragCoord.xy);
    vec2 z = c;
    vec2 dz = vec2(1.0, 0.0);
    bool de = fract_fx(FX_DE);

    FractOrbit o = fract_orbit_begin();
    int max_iter = u_iterations;

    for (int i = 0; i < 256; i++) {
        if (i >= max_iter) break;
        if (de) {
            vec2 zc = vec2(z.x, -z.y);
            dz = 2.0 * vec2(zc.x * dz.x - zc.y * dz.y, zc.x * dz.y + zc.y * dz.x) + vec2(1.0, 0.0);
        }
        /* conj(z)² = (a - ib)² = a² - b² - 2iab. */
        z = vec2(z.x * z.x - z.y * z.y, -2.0 * z.x * z.y) + c;
        float d2 = dot(z, z);
        fract_orbit_step(o, z, d2);
        if (d2 > 4.0) { fract_orbit_escape(o, i, d2, dz, 1.0); break; }
    }
    if (!fract_finite(o.nu)) o.nu = 0.0;

    vec3 rgb = fract_escape_shade(o, u_time * u_speed * 0.02 + u_seed.x);
    rgb = fract_fx_post(rgb * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
