// SPDX-License-Identifier: Apache-2.0
/* julia.frag — Julia set, z ← z² + c for a c on the Mandelbrot boundary.
 *
 * The driver's view picker draws c within a hair of the Mandelbrot set's
 * boundary (where Julia sets are connected, or barely not, and carry the
 * richest structure), then frames a boundary cell of the resulting set and
 * zooms in through the cycle.  c sways on a tiny circle so the set breathes
 * without losing its shape.  Colour and effects are the shared
 * fract_escape_shade (see escape_time.frag).
 *
 * Layout of u_params (vec4): centre.x, centre.y, zoom at the cycle start,
 * rotation.  u_view: cycle start time, log2 zoom per second, c.re, c.im.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

void main() {
    vec2 z = fract_view_point(gl_FragCoord.xy);
    float sway = u_time * u_speed * 0.25 + u_seed.y * 6.2832;
    vec2 c = u_view.zw + 0.0025 * vec2(cos(sway), sin(sway));
    vec2 dz = vec2(1.0, 0.0);
    bool de = fract_fx(FX_DE);

    FractOrbit o = fract_orbit_begin();
    int max_iter = u_iterations;

    for (int i = 0; i < 256; i++) {
        if (i >= max_iter) break;
        if (de) dz = 2.0 * vec2(z.x * dz.x - z.y * dz.y, z.x * dz.y + z.y * dz.x);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float d2 = dot(z, z);
        fract_orbit_step(o, z, d2);
        if (d2 > 4.0) { fract_orbit_escape(o, i, d2, dz, 1.0); break; }
    }
    if (!fract_finite(o.nu)) o.nu = 0.0;

    vec3 rgb = fract_escape_shade(o, u_time * u_speed * 0.02 + u_seed.x);
    rgb = fract_fx_post(rgb * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
