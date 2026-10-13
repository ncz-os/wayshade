// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_phase.frag — Allen-Cahn Cahn-Hilliard-like phase update.
//
// We compute the chemical potential mu = phi^3 - phi - eps^2 * laplacian(phi)
// and update phi <- phi + dt * eps * mu. This is the simplest
// conservative Allen-Cahn update; it gives necking and pinch-off
// for free and is far cheaper than the full Cahn-Hilliard (which
// would need a second order parameter).
//
// u_src — phi (r; in [-1, 1])
// Output — phi updated.
//
// We use a small dt and small mobility (eps) so the reaction is
// slower than advection; otherwise the field looks like it's
// oscillating rather than flowing. u_surface_tension is a per-launch
// randomisation: higher -> stiffer interface, lower -> floppier.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_resolution;
uniform float u_surface_tension;
uniform float u_dt;
uniform float u_seed;

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec2 t = vec2(1.0) / u_resolution;

  float pC = texture(u_src, uv).r;
  float pL = texture(u_src, uv - vec2(t.x, 0.0)).r;
  float pR = texture(u_src, uv + vec2(t.x, 0.0)).r;
  float pD = texture(u_src, uv - vec2(0.0, t.y)).r;
  float pU = texture(u_src, uv + vec2(0.0, t.y)).r;

  /* Discrete Laplacian with central differences. */
  float lap = (pL + pR + pD + pU - 4.0 * pC);

  /* Chemical potential. The eps^2 is folded into u_surface_tension
   * for tunability; eps itself is ~ sqrt(u_surface_tension). */
  float eps = sqrt(max(u_surface_tension, 1e-7));
  float mu = pC * pC * pC - pC - u_surface_tension * lap;

  /* Update. Mobility M is constant (= u_dt). */
  float phi_new = pC + u_dt * 8.0 * mu;

  /* Clamp so the field stays numerically stable. */
  phi_new = clamp(phi_new, -1.5, 1.5);

  fragColor = vec4(phi_new, 0.0, 0.0, 1.0);
}