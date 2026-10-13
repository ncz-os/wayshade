/* orbit_trap.frag — cubic Julia (z^3 + c) coloured by the distance of the orbit to
 * three rotating point traps 120 degrees apart. Escaping and interior pixels use
 * the same trap signal, so there is no flat interior, and the picture fades to
 * black away from the traps.
 *
 * Adapted from hyprsaver's shaders/fractaltrap.frag (MIT License, Copyright (c) 2026 Mara
 * Vexa; licence text in docs/fractasia/HYPRSAVER-LICENSE). Changes: the
 * Shadertoy-style uniforms are replaced by the fractint preamble, colours come
 * from fract_palette() through a mirrored wrap, --speed scales time (1 at the
 * default speed, the standalone saver's own pace), and start phases come from
 * u_params so every cycle differs.
 *
 * Layout of u_params (vec4):
 *   x = starting phase of c on its circle (radians).
 *   y = radius of both the c circle and the trap circle (0.52 to 0.62, which
 *       keeps every c inside the cubic multibrot's connected region).
 *   z = starting phase of the traps (radians).
 *   w = unused.
 */

vec3 pal_wrap(float x) { return fract_palette(abs(fract(x) * 2.0 - 1.0)); }

void main() {
    float spd = u_speed * (1.0 / 0.3);
    float t = u_time * spd;
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    vec2 z = p * 1.8;

    float radius = clamp(u_params.y, 0.4, 0.65);
    float angle = t * 0.06 + u_params.x;
    vec2 c = radius * vec2(cos(angle), sin(angle));

    float trap_angle = t * 0.09 + u_params.z;
    vec2 trap_p1 = radius * vec2(cos(trap_angle),          sin(trap_angle));
    vec2 trap_p2 = radius * vec2(cos(trap_angle + 2.0944), sin(trap_angle + 2.0944));
    vec2 trap_p3 = radius * vec2(cos(trap_angle + 4.1888), sin(trap_angle + 4.1888));

    const int MAX_ITER = 80;
    float min_trap_dist_sq = 1.0e10;
    bool escaped = false;
    float escape_iter = float(MAX_ITER);

    for (int i = 0; i < MAX_ITER; i++) {
        float xsq = z.x * z.x;
        float ysq = z.y * z.y;
        z = vec2(z.x * (xsq - 3.0 * ysq), z.y * (3.0 * xsq - ysq)) + c;

        vec2 d1 = z - trap_p1;
        vec2 d2 = z - trap_p2;
        vec2 d3 = z - trap_p3;
        float dsq = min(min(dot(d1, d1), dot(d2, d2)), dot(d3, d3));
        min_trap_dist_sq = min(min_trap_dist_sq, dsq);

        if (dot(z, z) > 4.0) {
            escape_iter = float(i) + 1.0;
            escaped = true;
            break;
        }
    }

    float min_trap_dist = sqrt(min_trap_dist_sq);
    float trap_glow = exp(-min_trap_dist * 3.0);

    float t_trap = clamp(min_trap_dist, 0.0, 1.0);
    vec3 col_primary = pal_wrap(t_trap * 2.0 + t * 0.10);
    float t_iter = escape_iter / float(MAX_ITER);
    vec3 col_secondary = pal_wrap(t_iter * 3.0 + t * 0.14 + 0.3);

    vec3 col = mix(col_primary, col_secondary, 0.4);
    if (!escaped) col *= 0.6;
    col *= trap_glow;
    col *= smoothstep(0.0, 0.05, trap_glow);
    if (!fract_finite(col.r + col.g + col.b)) col = vec3(0.0);
    col = fract_fx_post(col * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(col, 0.0, 1.0), 1.0);
}
