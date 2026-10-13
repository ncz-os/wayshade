// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_advect_h.frag — semi-Lagrangian advection of the hue field by
// the 2D velocity field. The hue field is RGBA8: rgb = per-cell hue
// (mixed by advection), a = mass (1 if wax is here, 0 otherwise).
//
// u_src   — velocity (rg)
// u_field — hue (rgba)
// Output  — hue.
//
// The mass channel is advected the same way; it decays slightly at
// every step to simulate the wax slowly dissolving at the
// interface. Hue is preserved.

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
  vec4 src = texture(u_field, prev);
  // Mass decay is very gentle; otherwise the field would fade out.
  fragColor = vec4(src.rgb, src.a);
}