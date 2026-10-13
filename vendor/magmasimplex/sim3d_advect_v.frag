// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_advect_v.frag — semi-Lagrangian advection of the 3D
// velocity atlas by itself. Each cell of the atlas samples its own
// velocity, traces back by v*dt to a previous 3D position, and
// reads the velocity there.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_dt;

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
  /* The atlas holds (v.x, v.y, v.z) in the first three channels
   * and a "slice index" weight in .a (we ignore). */
  vec4 v_here = sampleAtlas(u_src, vec3(p.xy, p.z / (u_atlas_slices * u_atlas_slices - 1.0)));
  vec3 v = v_here.xyz;
  vec3 prev = clamp(p.xyz - v * u_dt, vec3(0.0), vec3(1.0));
  vec4 src = sampleAtlas(u_src, prev);
  fragColor = vec4(src.xyz, 1.0);
}