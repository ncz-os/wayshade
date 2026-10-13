// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_advect_p.frag — semi-Lagrangian advection of the phase field
// by the 2D velocity field.
//
// u_src   — velocity (rg)
// u_field — phase (r; phi in [-1, 1] with 0 = interface)
// Output  — phase.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_resolution;
uniform float u_dt;

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec2 vel = texture(u_src, uv).rg;
  vec2 prev = uv - vel * u_dt;
  float p = texture(u_field, prev).r;
  fragColor = vec4(p, 0.0, 0.0, 1.0);
}