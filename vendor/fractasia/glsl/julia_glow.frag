/* julia_glow.frag — Julia set with a cardioid-tracing c, boundary striping and an
 * orbit-trap shimmer on a dark ground.
 *
 * Adapted from hyprsaver's shaders/julia.frag (MIT License, Copyright (c) 2026 Mara
 * Vexa; licence text in docs/fractasia/HYPRSAVER-LICENSE). Changes: the
 * Shadertoy-style uniforms are replaced by the fractint preamble, colours come
 * from fract_palette() through a mirrored wrap, --speed scales time (1 at the
 * default speed, the standalone saver's own pace), and start phases come from
 * u_params so every cycle differs.  The lookups are offset into the brighter
 * middle of the palette (as ship_glow.frag does): a generated palette is dark
 * at its low end by design, and sampling it there left the whole picture dim.
 *
 * Layout of u_params (vec4):
 *   x = starting phase of c along the path (radians).
 *   y = phase of the slow pan and zoom oscillation (radians).
 *   z, w = unused.
 */

vec3 pal_wrap(float x) { return fract_palette(abs(fract(x) * 2.0 - 1.0)); }

vec2 julia_c(float t) {
    float angle = t * 0.15 + u_params.x;
    float r = 0.7885;
    return vec2(r * cos(angle), r * sin(angle));
}

/* Smooth escape count, or 0.0 for interior; trap = min |Im z| along the orbit. */
float julia_iter(vec2 z, vec2 c, int max_iter, out float trap) {
    trap = 1.0e9;
    for (int i = 0; i < 256; i++) {
        if (i >= max_iter) break;
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float d2 = dot(z, z);
        trap = min(trap, abs(z.y));
        if (d2 > 4.0) {
            float nu = log2(log2(sqrt(d2)));
            return float(i) + 1.0 - nu;
        }
    }
    return 0.0;
}

void main() {
    float spd = u_speed * (1.0 / 0.3);
    float t = u_time * spd * 0.5;
    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

    float ph = u_params.y;
    float zoom = 1.3 + 0.3 * sin(t * 0.07 + ph);
    vec2 offset = vec2(sin(t * 0.031 + ph) * 0.05, cos(t * 0.023 + ph) * 0.05);
    vec2 z = uv * 2.1 / zoom + offset;
    vec2 c = julia_c(t);

    /* The look was tuned at 200 iterations; quality only raises it. */
    int max_iter = max(u_iterations, 160);
    float trap;
    float n = julia_iter(z, c, max_iter, trap);

    vec3 col;
    if (n == 0.0) {
        float glow = exp(-trap * 4.0) * 0.55;
        col = pal_wrap(0.30 + t * 0.03) * glow;
    } else {
        float t_col = n / 200.0;
        vec3 col_boundary = pal_wrap(0.30 + t_col * 3.0 + t * 0.05);
        float trap_glow = exp(-trap * 6.0);
        vec3 col_trap = pal_wrap(0.55 + t * 0.07) * trap_glow;
        col = mix(col_boundary, col_trap, 0.35);
        /* Far pixels used to fall to a tenth of the boundary brightness and
         * most of the frame sat near black; the knee is three times earlier. */
        col *= pow(clamp(t_col * 3.0, 0.0, 1.0), 0.5) * 1.5;
        col *= smoothstep(0.0, 0.02, t_col);
    }
    if (!fract_finite(col.r + col.g + col.b)) col = vec3(0.0);
    col = fract_fx_post(col * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(col, 0.0, 1.0), 1.0);
}
