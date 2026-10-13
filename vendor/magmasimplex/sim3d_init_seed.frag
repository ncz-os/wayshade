// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim3d_init_seed.frag - 3D initial seed: analytic blobs -> phase
// or hue atlas.

precision highp float;
out vec4 fragColor;

uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_seed;
uniform float u_write_hue;

uniform float u_blob_count;
uniform vec4  u_blob0;  /* (x, y, z, r) */
uniform vec4  u_blob1;
uniform vec4  u_blob2;
uniform vec4  u_blob3;
uniform vec4  u_blob4;
uniform vec4  u_blob5;
uniform vec4  u_blob6;
uniform vec4  u_blob7;

vec3 fragToLocalXYZ(vec2 fragXY){
  vec2 px = fragXY;
  float ix = floor(px.x / u_grid_n);
  float iy = floor(px.y / u_grid_n);
  vec2 local = (px - vec2(ix, iy) * u_grid_n) / u_grid_n;
  float zi = iy * u_atlas_slices + ix;
  return vec3(clamp(local, 0.0, 1.0), zi);
}

vec3 hsv2rgb(vec3 c){
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float blobF(vec4 b, vec3 p){
  float d = length(p - b.xyz);
  return exp(-(d * d) / max(b.w * b.w, 1e-4));
}

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 wp = p * 2.0 - 1.0; /* world-space [-1,1]^3 */

  vec4 blobs[8];
  blobs[0] = u_blob0; blobs[1] = u_blob1;
  blobs[2] = u_blob2; blobs[3] = u_blob3;
  blobs[4] = u_blob4; blobs[5] = u_blob5;
  blobs[6] = u_blob6; blobs[7] = u_blob7;

  if(u_write_hue < 0.5){
    float s = 0.0;
    for(int i = 0; i < 8; i++){
      s += blobF(blobs[i], wp) * 1.8;
    }
    float phi = tanh(s - 0.6);
    fragColor = vec4(phi, 0.0, 0.0, 1.0);
  } else {
    float mass = 0.0;
    float weighted_hue = 0.0;
    for(int i = 0; i < 8; i++){
      float w = blobF(blobs[i], wp);
      mass += w;
      weighted_hue += w * blobs[i].w;
    }
    if(mass > 1e-4) weighted_hue /= mass;
    else weighted_hue = 0.0;
    vec3 col = hsv2rgb(vec3(fract(weighted_hue), 0.95, 1.0));
    fragColor = vec4(col, clamp(mass * 1.5, 0.0, 1.0));
  }
}
