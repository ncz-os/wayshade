// SPDX-License-Identifier: Apache-2.0
/* quaternion.frag — 4D quaternion escape-time fractal.
 *
 * Renders a 3D slice through the 4D quaternion Julia set. For each
 * pixel, we walk the 4D escape-time iteration and pick the colour
 * from the slice coordinate the pixel sits on. The slice plane is
 * chosen deterministically per family cycle.
 *
 * The 4D quaternion q = (x, y, z, w). Quaternion multiplication:
 *   q1 · q2 = (x1·x2 - y1·y2 - z1·z2 - w1·w2,
 *               x1·y2 + y1·x2 + z1·w2 - w1·z2,
 *               x1·z2 - y1·w2 + z1·x2 + w1·y2,
 *               x1·w2 + y1·z2 - z1·y2 + w1·x2).
 *
 * Iteration: q = q² + c, bailout |q|² > 4.
 *
 * Layout of u_params (vec4):
 *   x, y, z, w = quaternion c parameter (unit quaternion).
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


vec4 f3_qmul(vec4 a, vec4 b) {
    return vec4(
        a.x * b.x - a.y * b.y - a.z * b.z - a.w * b.w,
        a.x * b.y + a.y * b.x + a.z * b.w - a.w * b.z,
        a.x * b.z - a.y * b.w + a.z * b.x + a.w * b.y,
        a.x * b.w + a.y * b.z - a.z * b.y + a.w * b.x
    );
}

void main() {
    /* 3D slice position from pixel coordinate. The w-component is
     * a fixed slice offset from u_seed.w — that way we walk the 4D
     * space without animating it (4D orbits can't be observed in
     * real time). */
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    /* The 4D slice walks slowly through w and c turns in its xy plane, so the
     * shape evolves over a cycle. */
    float tm = u_time * u_speed;
    float slice_w = sin(tm * 0.15 + u_seed.w * 6.2831853) * 0.9;
    vec4 z = vec4(p.x, p.y, 0.0, slice_w);

    vec4 c = u_params;
    float cs = cos(tm * 0.08), sn = sin(tm * 0.08);
    c.xy = vec2(cs * c.x - sn * c.y, sn * c.x + cs * c.y);

    float trap = 1.0e9;
    bool escaped = false;
    float escape_iter = float(u_iterations);
    int max_iter = u_iterations;

    for (int i = 0; i < 64; i++) {
        if (i >= max_iter) break;
        z = f3_qmul(z, z) + c;
        float d2 = dot(z, z);
        trap = min(trap, d2);
        if (d2 > 4.0) {
            escape_iter = float(i) + 1.0;
            escaped = true;
            break;
        }
    }

    float col;
    if (escaped) {
        col = escape_iter / float(max(max_iter, 1));
    } else {
        col = 0.05 + 0.15 * exp(-sqrt(trap) * 1.5);
    }
    col = clamp(col, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    /* Subtle depth cue: blend z.z into the colour so different slices
     * read as different hues. */
    rgb = mix(rgb, rgb.bgr, abs(z.z) * 0.2);

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}