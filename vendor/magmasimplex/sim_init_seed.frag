// SPDX-License-Identifier: Apache-2.0
#version 300 es
// sim_init_seed.frag — initial seed of the phase (and hue) fields
// from the per-launch blob layout.
//
// Reads nothing (u_src is a dummy). For each of the 8 blob uniforms,
// we add a Gaussian-falloff phase blob and (for the hue field) a
// colour contribution proportional to each blob's hue id.
//
// Output: when u_write_hue < 0.5: phase field (.r). When
// u_write_hue >= 0.5: hue field (.rgb = mix of per-blob hues
// weighted by their falloff, .a = mass).

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_resolution;
uniform float u_seed;
uniform float u_write_hue;

uniform float u_blob_count;
uniform vec4  u_blob0;  /* (x, y, r, hue_id) — z unused for 2D */
uniform vec4  u_blob1;
uniform vec4  u_blob2;
uniform vec4  u_blob3;
uniform vec4  u_blob4;
uniform vec4  u_blob5;
uniform vec4  u_blob6;
uniform vec4  u_blob7;

/* Cheap hsv->rgb. We use this for the hue field seeding. */
vec3 hsv2rgb(vec3 c){
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float blobF(vec4 b, vec2 p){
  float d = length(p - b.xy);
  /* Gaussian falloff scaled so the blob radius b.z corresponds to
   * roughly 1 standard deviation. */
  float v = exp(-(d * d) / max(b.z * b.z, 1e-4));
  return v;
}

void main(){
  vec2 uv = gl_FragCoord.xy / u_resolution;
  vec2 pos = vec2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);

  vec4 blobs[8];
  blobs[0] = u_blob0; blobs[1] = u_blob1;
  blobs[2] = u_blob2; blobs[3] = u_blob3;
  blobs[4] = u_blob4; blobs[5] = u_blob5;
  blobs[6] = u_blob6; blobs[7] = u_blob7;

  if(u_write_hue < 0.5){
    /* Phase: sum of blob contributions, mapped to phi in [-1, 1]
     * via tanh-like squash so we don't blow past 1. */
    float s = 0.0;
    for(int i = 0; i < 8; i++){
      s += blobF(blobs[i], pos) * 1.8;
    }
    /* Squash so the inside of a blob is solidly at +1 and the
     * outside is near 0. We use tanh for a smooth, monotonic
     * mapping. */
    float phi = tanh(s - 0.6);
    fragColor = vec4(phi, 0.0, 0.0, 1.0);
  } else {
    /* Hue field. The hue at this cell is the per-blob hue ids,
     * weighted by their blob contributions, accumulated. Then
     * mapped through hsv2rgb. Mass is the sum of contributions. */
    float mass = 0.0;
    float weighted_hue = 0.0;
    for(int i = 0; i < 8; i++){
      float w = blobF(blobs[i], pos);
      mass += w;
      weighted_hue += w * blobs[i].w;
    }
    if(mass > 1e-4){
      weighted_hue /= mass;
    } else {
      weighted_hue = 0.0;
    }
    vec3 col = hsv2rgb(vec3(fract(weighted_hue), 0.95, 1.0));
    fragColor = vec4(col, clamp(mass * 1.5, 0.0, 1.0));
  }
}