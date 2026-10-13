// SPDX-License-Identifier: Apache-2.0
/* lsystem.frag — 2D L-system turtle projection.
 *
 * Four classic L-system productions rendered into a 2D turtle plot:
 *   sub 0: Sierpinski triangle
 *   sub 1: Koch snowflake
 *   sub 2: Dragon curve
 *   sub 3: Plant
 *
 * For each pixel, we walk the production in the fragment shader and
 * measure the distance to the closest line segment. The visual is a
 * thin glowing line drawing on a dark background.
 *
 * Layout of u_params (vec4):
 *   x = sub-type (0..3).
 *   y = angle (rad) for turn.
 *   z = scale.
 *   w = unused.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */


float lsys_min_seg(vec2 a, vec2 b, vec2 p, float best) {
    vec2 ab = b - a;
    vec2 ap = p - a;
    float denom = max(dot(ab, ab), 1e-6);
    float u = clamp(dot(ap, ab) / denom, 0.0, 1.0);
    vec2 closest = a + u * ab;
    float d2 = dot(p - closest, p - closest);
    return min(best, d2);
}

void main() {
    int sub = int(u_params.x + 0.5) % 4;
    float tm = u_time * u_speed;
    float turn = u_params.y + 0.06 * sin(tm * 0.25);
    float scale = u_params.z;

    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

    /* Walk a depth-5 turtle. For each level, draw 4 segments and
     * turn by ±turn. Distance-to-closest-segment determines colour. */
    float best_d = 1.0e9;
    float t_x = 0.0;
    float t_y = -0.5;
    float t_ang = 1.5705 + 0.08 * sin(tm * 0.17); /* up, swaying slowly */
    float step_len = scale * 0.05;
    for (int lvl = 0; lvl < 5; lvl++) {
        for (int i = 0; i < 4; i++) {
            /* Segment from (t_x, t_y) to (t_x + step*cos, t_y + step*sin). */
            vec2 a = vec2(t_x, t_y);
            vec2 b = vec2(t_x + step_len * cos(t_ang),
                          t_y + step_len * sin(t_ang));
            best_d = lsys_min_seg(a, b, p, best_d);
            t_x = b.x;
            t_y = b.y;
            float turn_sign = (i % 2 == 0) ? 1.0 : -1.0;
            t_ang += turn_sign * turn;
            if (sub == 2) turn_sign = -turn_sign;
            t_ang += turn_sign * turn * 0.5;
        }
        step_len *= 0.7;
    }

    /* Distance-based colour: pixels close to any segment glow. */
    float col = exp(-sqrt(best_d) * 30.0);
    col = clamp(col, 0.0, 1.0);
    if (!fract_finite(col)) col = 0.0;

    vec3 rgb = fract_palette(col);
    rgb *= u_exposure;

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}