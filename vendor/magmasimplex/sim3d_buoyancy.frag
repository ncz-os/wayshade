// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_buoyancy.frag - 3D buoyancy: T -> v (upward) + small jitter.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_buoy_alpha;
uniform float u_seed;
uniform float u_time;

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


float hash12(vec3 p){
  p = fract(p * vec3(123.34, 456.78, 789.9));
  p += dot(p, p + 78.9 + u_seed * 1e-4);
  return fract(p.x * p.y * p.z);
}

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 v = sampleAtlas(u_src, p).xyz;
  float T = sampleAtlas(u_field, p).r;
  float jx = (hash12(p * 23.7) - 0.5) * 0.15;
  float jz = (hash12(p * 31.1 + 5.0) - 0.5) * 0.15;
  v.x += T * jx * u_buoy_alpha;
  v.y += T * u_buoy_alpha;
  v.z += T * jz * u_buoy_alpha;
  v *= 0.995;
  fragColor = vec4(v, 1.0);
}
