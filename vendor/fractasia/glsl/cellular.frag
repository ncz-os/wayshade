// SPDX-License-Identifier: Apache-2.0
/* cellular.frag — 1D Wolfram elementary cellular automaton, scrolling.
 *
 * The automaton is stepped on the CPU (fract_ca_step) one row at a time into a
 * 251 x 128 ring texture, u_ca (R8, row r lives at r mod 128). This shader only looks cells up, so the cost per pixel is one
 * fetch, and the scroll never runs out of rows.
 *
 * Layout of u_params (vec4):
 *   x = rule number.
 *   y = unused (the initial row is chosen on the CPU from u_seed).
 *   z = scroll speed (rows per second).
 *   w = u_time at the start of this cycle, so each cycle begins at row 0.
 *
 * Original to ncz-screensavers (Apache-2.0). Standard textbook
 * algorithm; no Fractint source code copied.
 */

uniform sampler2D u_ca;

void main() {
    vec2 p = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;

    /* Column along x: FRACT_CA_W (251) cells across the width. */
    int col = int(mod(p.x * 0.5 + 0.5, 1.0) * 251.0);
    col = clamp(col, 0, 250);

    /* Continuous row coordinate: cell edges slide smoothly with the scroll.
     * A cell is 2/251 of the screen height wide, so (0.5 - p.y) * 125.5 rows
     * are visible and cells are square; the ring keeps 128. */
    float scroll = max(u_time - u_params.w, 0.0) * u_params.z;
    float y_pos = (0.5 - p.y) * 125.5;
    int row = int(floor(y_pos + scroll));
    row = max(row, 0) & 127;

    float cell = texelFetch(u_ca, ivec2(col, row), 0).r;
    /* The ring stores 0 or 1 in an R8 texture, which reads back as 0 or 1/255. */
    vec3 rgb = fract_palette(cell > 0.002 ? 1.0 : 0.0);
    rgb = fract_fx_post(rgb * u_exposure, gl_FragCoord.xy);

    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
