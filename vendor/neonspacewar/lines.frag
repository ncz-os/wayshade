// SPDX-License-Identifier: Apache-2.0
#version 300 es
/* lines.frag — fragment shader for the thick-stroke line renderer.
 *
 * Layout per-vertex (LineVert):
 *   x, y            world-space endpoint
 *   ox, oy          world-space other endpoint of the segment
 *   r, g, b         line colour
 *   a               alpha multiplier
 *   w               half-thickness in pixels
 *   s               side: -1 or +1 (perpendicular offset from line center)
 *
 * The vertex shader expands each segment into two triangles and
 * extrudes the four vertices by +-a_width*u_px_to_clip along the
 * segment's perpendicular, so the line ends up a thick stroked quad
 * with sub-pixel control.
 *
 * This fragment shader converts v_side (a -1..+1 "signed distance
 * to line center" expressed as the extrusion offset) into a sharp
 * bright core and a soft halo via exp(-d*d*k), without a blur pass.
 *
 * Default (when this file isn't found on disk and the inline shader
 * in src/gles3_neonspacewar.c is used) does the same.
 */
precision mediump float;

in vec3 v_color;
in float v_alpha;
in float v_side;
out vec4 o_col;

void main() {
    /* d in [0..1] across the half-quad. */
    float d = abs(v_side);
    /* Sharp core for d<0.55, falls off smoothly to 0 by d=1.0.
     * The 1.6x boost gives the core a hot inner line. */
    float core = 1.0 - smoothstep(0.55, 1.0, d);
    /* Soft exponential halo without a blur pass. The 0.55 factor
     * weights the halo contribution relative to the core. */
    float halo = exp(-d * d * 3.5);
    vec3 c = v_color * v_alpha * (core * 1.6 + halo * 0.55);
    o_col = vec4(c, core * v_alpha + halo * 0.25);
}
