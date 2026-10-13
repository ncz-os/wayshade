// SPDX-License-Identifier: Apache-2.0
/* escape_time.frag — the Mandelbrot set, z ← z² + c with c the pixel.
 *
 * The window comes from the driver's view picker (fract_view_point in the
 * preamble): a centre on the set boundary, a zoom that keeps rising through
 * the cycle, and a slow rotation.  Colour is the shared fract_escape_shade:
 * palette bands in log iteration space that survive any zoom depth, a dark
 * far field, an interior lifted only by its orbit-trap glow, and the cycle's
 * effect recipe (orbit traps, stripes, distance-estimate outline and glow,
 * emboss; see fractasia.h).  dz is tracked only when the recipe asks for the
 * distance estimate.
 *
 * Layout of u_params (vec4): centre.x, centre.y, zoom at the cycle start,
 * rotation.  u_view: cycle start time, log2 zoom per second (c unused).
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

const int F1_MAX_ITER = 256;

void main() {
    vec2 c = fract_view_point(gl_FragCoord.xy);
    vec2 z = c;
    vec2 dz = vec2(1.0, 0.0);
    bool de = fract_fx(FX_DE);

    FractOrbit o = fract_orbit_begin();
    int max_iter = u_iterations;

    /* The main cardioid and the period-2 bulb are interior by construction:
     * skip the loop there (the CPU probe makes the same test). */
    {
        float xq = c.x - 0.25, q = xq * xq + c.y * c.y;
        float xb = c.x + 1.0;
        if (q * (q + xq) <= 0.25 * c.y * c.y || xb * xb + c.y * c.y <= 0.0625) {
            max_iter = 0;
            o.trap = dot(c, c) * 0.5;
            o.line = abs(c.y) * 0.5;
            o.cross = min(abs(c.x), abs(c.y)) * 0.5;
        }
    }

    for (int i = 0; i < F1_MAX_ITER; i++) {
        if (i >= max_iter) break;
        if (de) dz = 2.0 * vec2(z.x * dz.x - z.y * dz.y, z.x * dz.y + z.y * dz.x) + vec2(1.0, 0.0);
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
