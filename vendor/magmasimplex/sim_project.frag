// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_project.frag — Jacobi pressure projection.
//
// This pass has TWO modes, switched by u_subtract:
//   u_subtract < 0.5: Jacobi iteration. u_src is pressure (r), u_field
//                     is divergence of velocity. Output: new pressure.
//   u_subtract >= 0.5: subtract grad(p) from v. u_src is v, u_field
//                     is pressure. Output: v - grad(p).
//
// We follow Bridson's "Fluid Simulation" (2007) discrete Poisson.
//
// We use RGBA16F for both v and p. v lives in .rg, p in .r.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_resolution;
uniform float u_subtract;

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec2 t = vec2(1.0) / u_resolution;

  if(u_subtract < 0.5){
    /* Jacobi iteration. Compute laplacian(p) and divergence(v). */
    float pC = texture(u_src, uv).r;
    float pL = texture(u_src, uv - vec2(t.x, 0.0)).r;
    float pR = texture(u_src, uv + vec2(t.x, 0.0)).r;
    float pD = texture(u_src, uv - vec2(0.0, t.y)).r;
    float pU = texture(u_src, uv + vec2(0.0, t.y)).r;

    /* divergence of v sampled at this cell. */
    vec2 vL = texture(u_field, uv - vec2(t.x, 0.0)).rg;
    vec2 vR = texture(u_field, uv + vec2(t.x, 0.0)).rg;
    vec2 vD = texture(u_field, uv - vec2(0.0, t.y)).rg;
    vec2 vU = texture(u_field, uv + vec2(0.0, t.y)).rg;
    float div = 0.5 * ((vR.x - vL.x) + (vU.y - vD.y));

    /* Solve (laplacian(p) = div) for p. We use Jacobi weighting
     * 0.25 for the four neighbours and divide by 4 in the
     * denominator (a common form). */
    float new_p = (pL + pR + pD + pU - div) * 0.25;
    fragColor = vec4(new_p, 0.0, 0.0, 1.0);
  } else {
    /* Subtract gradient of p from v. */
    vec2 v = texture(u_src, uv).rg;
    float pL = texture(u_field, uv - vec2(t.x, 0.0)).r;
    float pR = texture(u_field, uv + vec2(t.x, 0.0)).r;
    float pD = texture(u_field, uv - vec2(0.0, t.y)).r;
    float pU = texture(u_field, uv + vec2(0.0, t.y)).r;
    vec2 grad = 0.5 * vec2(pR - pL, pU - pD);
    vec2 v_new = v - grad;
    fragColor = vec4(v_new, 0.0, 1.0);
  }
}