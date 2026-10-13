// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_heat.frag — emitter heat source and ambient cooling.
//
// u_src — current temperature field (r)
// Output — temperature after heat has been added at the emitter and
//          cooling has been applied at the top of the frame.
//
// We bias T upward at the emitter (a Gaussian falloff) and apply a
// passive cooling proportional to height. The cooling is what makes
// plumes rise, cool, sink, reheat — the lava-lamp cycle.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_resolution;
uniform float u_emitter_pos_x;
uniform float u_emitter_pos_y;
uniform float u_emitter_r;
uniform float u_emitter_strength;
uniform float u_amb_cool;
uniform float u_hotness;
uniform float u_time;
uniform float u_seed;

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  /* Convert uv (which is 0..1 with 0 at bottom-left in our usage
   * — note: gl_FragCoord.y is 0 at BOTTOM for GL) to the world
   * coordinate system we use elsewhere. We want y up. */
  vec2 pos = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec2 ep  = vec2(u_emitter_pos_x, u_emitter_pos_y);
  float d = length(pos - ep);
  float fall = exp(-(d * d) / (u_emitter_r * u_emitter_r));

  float T = texture(u_src, uv).r;

  /* Source: a steady heat input at the emitter, plus a small
   * sinusoidal wobble so the emitter pulses softly rather than
   * being a perfectly steady source. */
  float pulse = 0.85 + 0.15 * sin(u_time * 0.9 + u_seed * 13.0);
  T += fall * u_emitter_strength * pulse * u_hotness * (1.0 / 60.0);

  /* Ambient cooling: stronger near the top of the frame, weaker
   * near the bottom (the emitter keeps the bottom warm). */
  float topness = clamp((uv.y), 0.0, 1.0);
  float cool = u_amb_cool * topness * (1.0 / 60.0);
  T -= T * cool * 4.0;

  /* Clamp. The phase field needs phi in [-1, 1]; temperature is
   * roughly 0..3 for our hot plumes, but we keep the dynamic range
   * wide by allowing some headroom. */
  T = clamp(T, -0.5, 4.0);
  fragColor = vec4(T, 0.0, 0.0, 1.0);
}