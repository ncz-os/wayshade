// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_advect_v.frag — semi-Lagrangian advection of the 2D velocity
// field by itself. Stam step 2.
//
// u_src is the velocity field. We trace each cell BACKWARDS along v
// by u_dt and sample u_src at the previous position. That value
// becomes the new velocity.
//
// Output is RGBA16F: .rg = v.xy, .ba = 0.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_resolution;
uniform float u_dt;

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec2 vel = texture(u_src, uv).rg;
  vec2 prev = uv - vel * u_dt;
  vec4 src = texture(u_src, prev);
  fragColor = vec4(src.rg, 0.0, 1.0);
}