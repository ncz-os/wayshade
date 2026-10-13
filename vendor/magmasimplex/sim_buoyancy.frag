// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_buoyancy.frag — buoyancy: T (unit 1) drives an upward force
// on v (unit 0). The current v is also read so we ADD to it, not
// replace.
//
// u_src   — current velocity (rg)
// u_field — temperature (r)
// Output  — new velocity with buoyancy added.
//
// The force is proportional to T, directed upward (so .g += kT; .r
// also gets a small lateral noise term driven by the seed so the
// flow isn't perfectly axisymmetric).

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_resolution;
uniform float u_buoy_alpha;
uniform float u_seed;
uniform float u_time;
uniform float u_emitter_pos_x;
uniform float u_emitter_pos_y;
uniform float u_emitter_r;

float hash12(vec2 p){
  p = fract(p * vec2(123.34, 456.78));
  p += dot(p, p + 78.9 + u_seed * 1e-4);
  return fract(p.x * p.y);
}

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec4 v = texture(u_src, uv);
  float T = texture(u_field, uv).r;
  // A small horizontal jitter keyed off the temperature so plumes
  // also have a meander. Not physically correct, but visually
  // pleasant — without it, plumes are dead straight.
  float jx = (hash12(uv * 23.7 + u_seed) - 0.5) * 0.15;
  v.r += T * jx * u_buoy_alpha;
  // Upward force proportional to T. y is screen-up in our coord
  // system (uv.y from bottom is gl_FragCoord.y / H).
  v.g += T * u_buoy_alpha;
  // Damp velocity slightly so the simulation doesn't explode.
  v.rg *= 0.995;
  fragColor = vec4(v.rg, 0.0, 1.0);
}