/* ship_glow.frag — a Burning Ship Julia variant on a dark ground.
 *
 * The same iteration as a Julia set, but |Re z| and |Im z| are folded before each
 * squaring, which breaks the rotational symmetry and gives the angular, mirror-
 * symmetric "ship" silhouettes. c circles a small orbit through a structurally rich
 * region of the Burning Ship parameter space. Interior pixels are pure black.
 *
 * Adapted from hyprsaver's shaders/shipburn.frag (MIT License, Copyright (c) 2026 Mara
 * Vexa; licence text in docs/fractasia/HYPRSAVER-LICENSE). Changes: the Shadertoy-style
 * uniforms are replaced by the fractasia preamble, colours come from fract_palette()
 * through a mirrored wrap, --speed scales time (1 at the default speed, the standalone
 * saver's own pace), and the start phase and orbit radius come from u_params so every
 * cycle differs.
 *
 * Layout of u_params (vec4):
 *   x = starting phase of c on its orbit (radians).
 *   y = radius of that orbit (0.015 to 0.024).
 *   z, w = unused.
 */

vec3 pal_wrap(float x) { return fract_palette(abs(fract(x) * 2.0 - 1.0)); }

void main() {
    float spd = u_speed * (1.0 / 0.3);
    float t = u_time * spd;
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    /* Zoomed in from the standalone saver's 2.0: the figure is a thin horizontal band
     * and filled a small part of the screen. */
    vec2 z = p * 0.95;

    float radius = clamp(u_params.y, 0.01, 0.03);
    float angle = t * 0.225 + u_params.x;
    vec2 c = vec2(-1.762 + radius * cos(angle), -0.028 + radius * sin(angle));

    const int MAX_ITER = 150;
    float escape_iter = float(MAX_ITER);
    bool escaped = false;

    for (int i = 0; i < MAX_ITER; i++) {
        z = abs(z);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float d2 = dot(z, z);
        if (d2 > 4.0) {
            float nu = log2(log2(sqrt(d2)));
            escape_iter = float(i) + 1.0 - nu;
            escaped = true;
            break;
        }
    }

    if (!escaped) {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float t_col = escape_iter / float(MAX_ITER);
    /* Knee at 0.45 rather than 0.6: the rigging away from the hull reads
     * instead of fading to the black ground. */
    float t_thick = smoothstep(0.0, 0.45, t_col);

    /* Escaped pixels have small t, which lands on the dark low end of a generated
     * palette (kept dark on purpose for the ground), and col is dimmed again below.
     * Offset the lookups into the brighter middle of the palette. */
    vec3 col_boundary = pal_wrap(0.30 + t_thick * 3.0 + t * 0.075);
    vec3 col_shimmer  = pal_wrap(0.55 + t_thick * 2.0 + t * 0.105);
    vec3 col = mix(col_boundary, col_shimmer, 0.35);

    col *= pow(clamp(t_thick, 0.0, 1.0), 0.5) * 2.2;
    col *= smoothstep(0.0, 0.02, t_col);

    if (!fract_finite(col.r + col.g + col.b)) col = vec3(0.0);
    col = fract_fx_post(col * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(col, 0.0, 1.0), 1.0);
}
