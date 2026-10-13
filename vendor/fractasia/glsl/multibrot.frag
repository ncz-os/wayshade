// SPDX-License-Identifier: Apache-2.0
/* multibrot.frag — Julia set of z ← z^n + c, n ∈ 3..8, for a c on the
 * boundary of the multibrot set.
 *
 * The parameter-space multibrot set is a near-disc for n above 4, so a view
 * of it is half hole whichever rim is framed; its Julia sets for c on that
 * rim are n-fold symmetric dendrites and spirals that fill the frame.  The
 * driver's view picker draws c within a hair of the boundary (the same
 * neighbour test julia.frag's c gets, under z^n), frames a boundary cell of
 * the resulting set, and zooms in through the cycle.  Earlier versions used a
 * random c, which for most draws was dust or an empty screen.  Colour and
 * effects are the shared fract_escape_shade; the distance estimate tracks
 * dz' = n z^(n-1) dz.
 *
 * Layout of u_params (vec4): centre.x, centre.y, zoom at the cycle start,
 * rotation.  u_view: cycle start time, log2 zoom per second, c.re, c.im.
 * u_type: the exponent n.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

/* z^n by repeated squaring: at most three squarings and three multiplies
 * for n <= 8, instead of n - 1 multiplies.  The CPU probe does the same. */
vec2 f1_pow(vec2 z, int n) {
    vec2 r = vec2(1.0, 0.0);
    vec2 b = z;
    for (int e = n; e > 0; e >>= 1) {
        if ((e & 1) == 1) r = vec2(r.x * b.x - r.y * b.y, r.x * b.y + r.y * b.x);
        b = vec2(b.x * b.x - b.y * b.y, 2.0 * b.x * b.y);
    }
    return r;
}

void main() {
    int n = clamp(u_type, 3, 8);
    float inv_log_n = 1.0 / log2(float(n));

    vec2 z = fract_view_point(gl_FragCoord.xy);
    float sway = u_time * u_speed * 0.25 + u_seed.y * 6.2832;
    vec2 c = u_view.zw + 0.0015 * vec2(cos(sway), sin(sway));
    vec2 dz = vec2(1.0, 0.0);
    bool de = fract_fx(FX_DE);

    FractOrbit o = fract_orbit_begin();
    int max_iter = u_iterations;

    for (int i = 0; i < 256; i++) {
        if (i >= max_iter) break;
        if (de) {
            vec2 p = f1_pow(z, n - 1) * float(n);
            dz = vec2(p.x * dz.x - p.y * dz.y, p.x * dz.y + p.y * dz.x);
        }
        z = f1_pow(z, n) + c;
        float d2 = dot(z, z);
        fract_orbit_step(o, z, d2);
        if (d2 > 4.0) { fract_orbit_escape(o, i, d2, dz, inv_log_n); break; }
    }
    if (!fract_finite(o.nu)) o.nu = 0.0;

    vec3 rgb = fract_escape_shade(o, u_time * u_speed * 0.02 + u_seed.x);
    rgb = fract_fx_post(rgb * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
