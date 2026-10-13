// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_project.frag - 3D pressure projection with subtraction mode.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_subtract;

vec3 fragToLocalXYZ(vec2 fragXY){
  vec2 px = fragXY;
  float ix = floor(px.x / u_grid_n);
  float iy = floor(px.y / u_grid_n);
  vec2 local = (px - vec2(ix, iy) * u_grid_n) / u_grid_n;
  float zi = iy * u_atlas_slices + ix;
  return vec3(clamp(local, 0.0, 1.0), zi);
}
vec4 sampleAtlas(sampler2D tex, vec3 p){
  float zf = clamp(p.z, 0.0, 1.0) * (u_atlas_slices * u_atlas_slices - 1.0);
  float zi = floor(zf);
  float zfi = zf - zi;
  float ix0 = mod(zi, u_atlas_slices);
  float iy0 = floor(zi / u_atlas_slices);
  vec2 uv0 = (vec2(ix0, iy0) + p.xy) * u_grid_n / u_atlas_size;
  vec4 a = texture(tex, uv0);
  float zin = zi + 1.0;
  if(zfi > 1e-4 && zin < u_atlas_slices * u_atlas_slices){
    float ix1 = mod(zin, u_atlas_slices);
    float iy1 = floor(zin / u_atlas_slices);
    vec2 uv1 = (vec2(ix1, iy1) + p.xy) * u_grid_n / u_atlas_size;
    vec4 b = texture(tex, uv1);
    return mix(a, b, zfi);
  }
  return a;
}


void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 t = vec3(1.0 / u_grid_n);

  if(u_subtract < 0.5){
    /* Jacobi iteration. */
    float pC = sampleAtlas(u_src, p).r;
    float pL = sampleAtlas(u_src, p - vec3(t.x, 0.0, 0.0)).r;
    float pR = sampleAtlas(u_src, p + vec3(t.x, 0.0, 0.0)).r;
    float pD = sampleAtlas(u_src, p - vec3(0.0, t.y, 0.0)).r;
    float pU = sampleAtlas(u_src, p + vec3(0.0, t.y, 0.0)).r;
    float pF = sampleAtlas(u_src, p - vec3(0.0, 0.0, t.z)).r;
    float pB = sampleAtlas(u_src, p + vec3(0.0, 0.0, t.z)).r;
    vec3 vL = sampleAtlas(u_field, p - vec3(t.x, 0.0, 0.0)).xyz;
    vec3 vR = sampleAtlas(u_field, p + vec3(t.x, 0.0, 0.0)).xyz;
    vec3 vD = sampleAtlas(u_field, p - vec3(0.0, t.y, 0.0)).xyz;
    vec3 vU = sampleAtlas(u_field, p + vec3(0.0, t.y, 0.0)).xyz;
    vec3 vF = sampleAtlas(u_field, p - vec3(0.0, 0.0, t.z)).xyz;
    vec3 vB = sampleAtlas(u_field, p + vec3(0.0, 0.0, t.z)).xyz;
    float div = 0.5 * ((vR.x - vL.x) + (vU.y - vD.y) + (vB.z - vF.z));
    float new_p = (pL + pR + pD + pU + pF + pB - div) / 6.0;
    fragColor = vec4(new_p, 0.0, 0.0, 1.0);
  } else {
    /* Subtract gradient. */
    vec3 v = sampleAtlas(u_src, p).xyz;
    float pL = sampleAtlas(u_field, p - vec3(t.x, 0.0, 0.0)).r;
    float pR = sampleAtlas(u_field, p + vec3(t.x, 0.0, 0.0)).r;
    float pD = sampleAtlas(u_field, p - vec3(0.0, t.y, 0.0)).r;
    float pU = sampleAtlas(u_field, p + vec3(0.0, t.y, 0.0)).r;
    float pF = sampleAtlas(u_field, p - vec3(0.0, 0.0, t.z)).r;
    float pB = sampleAtlas(u_field, p + vec3(0.0, 0.0, t.z)).r;
    vec3 grad = 0.5 * vec3(pR - pL, pU - pD, pB - pF);
    fragColor = vec4(v - grad, 1.0);
  }
}
