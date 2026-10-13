// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_atlas.frag — shared atlas indexing helpers for the 3D-tier
// simulation shaders. Each 3D simulation shader begins with the
// snippet below and then implements its own logic.
//
// Atlas layout: a 2D texture of size (N*s, N*s) holds N slices of
// size N x N in an s x s grid (s = ceil(sqrt(N))). u_grid_n = N,
// u_atlas_slices = s, u_atlas_size = (N*s, N*s).
//
// Coordinates are 3D positions in [0,1]^3 (x, y, z). The shader
// fragment's gl_FragCoord.xy is in atlas pixels (0..N*s); we
// convert to (slice_index, local_xy) before operating.
//
// We compute 3D positions and sample two adjacent slices for
// trilinear interpolation, mixing by the fractional z.

precision highp float;

uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;

vec3 fragToLocalXYZ(vec2 fragXY){
  /* Convert atlas frag coord to (x, y, slice_index) within the
   * atlas. x,y in [0,1], slice_index in [0, slices*slices). */
  vec2 px = fragXY;
  float ix = floor(px.x / u_grid_n);
  float iy = floor(px.y / u_grid_n);
  vec2 local = (px - vec2(ix, iy) * u_grid_n) / u_grid_n;
  float zi = iy * u_atlas_slices + ix;
  return vec3(clamp(local, 0.0, 1.0), zi);
}

vec2 XYZtoAtlasUV(vec3 p){
  float zf = clamp(p.z, 0.0, 1.0) * (u_atlas_slices * u_atlas_slices - 1.0);
  float zi = floor(zf);
  float zfi = zf - zi;
  float ix = mod(zi, u_atlas_slices);
  float iy = floor(zi / u_atlas_slices);
  vec2 uv = (vec2(ix, iy) + p.xy) * u_grid_n / u_atlas_size;
  return uv;
}

vec4 sampleAtlas(sampler2D tex, vec3 p){
  /* Trilinear: sample two slices and blend by fractional z. */
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