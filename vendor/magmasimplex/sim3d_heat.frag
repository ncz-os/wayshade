// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_heat.frag - 3D heat source + ambient cooling.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_emitter_pos_x;
uniform float u_emitter_pos_y;
uniform float u_emitter_r;
uniform float u_emitter_strength;
uniform float u_amb_cool;
uniform float u_hotness;
uniform float u_time;
uniform float u_seed;

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
  vec2 uv = p.xy;
  vec2 pos = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
  vec2 ep  = vec2(u_emitter_pos_x, u_emitter_pos_y);
  float d = length(pos - ep);
  float fall = exp(-(d * d) / (u_emitter_r * u_emitter_r));

  float T = sampleAtlas(u_src, p).r;

  float pulse = 0.85 + 0.15 * sin(u_time * 0.9 + u_seed * 13.0);
  T += fall * u_emitter_strength * pulse * u_hotness * (1.0 / 60.0);

  float topness = clamp(uv.y, 0.0, 1.0);
  float cool = u_amb_cool * topness * (1.0 / 60.0);
  T -= T * cool * 4.0;

  T = clamp(T, -0.5, 4.0);
  fragColor = vec4(T, 0.0, 0.0, 1.0);
}
