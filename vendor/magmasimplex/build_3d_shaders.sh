#!/bin/sh
# build_3d_shaders.sh - generate the 3D-tier shader variants from the
# 2D versions. Each 3D shader is the 2D shader with:
#   - atlas helpers inlined at the top
#   - sampleXxx3D() wrapper functions defined
#   - main() rewritten to operate in 3D (sample by 3D coord, not 2D)

set -e
SIM=vendor/magmasimplex

HELPERS='
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
'

# ---- advect_t (3D) ----
cat > "$SIM/sim3d_advect_t.frag" <<EOF
#version 300 es
// sim3d_advect_t.frag - 3D semi-Lagrangian advection of temperature.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;       // velocity atlas
uniform sampler2D u_field;     // temperature atlas (.r)
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_dt;
$HELPERS

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 v = sampleAtlas(u_src, p).xyz;
  vec3 prev = clamp(p.xyz - v * u_dt, vec3(0.0), vec3(1.0));
  float t = sampleAtlas(u_field, prev).r;
  fragColor = vec4(t, 0.0, 0.0, 1.0);
}
EOF

# ---- advect_p (3D) ----
cat > "$SIM/sim3d_advect_p.frag" <<EOF
#version 300 es
// sim3d_advect_p.frag - 3D semi-Lagrangian advection of phase.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_dt;
$HELPERS

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 v = sampleAtlas(u_src, p).xyz;
  vec3 prev = clamp(p.xyz - v * u_dt, vec3(0.0), vec3(1.0));
  float ph = sampleAtlas(u_field, prev).r;
  fragColor = vec4(ph, 0.0, 0.0, 1.0);
}
EOF

# ---- advect_h (3D) ----
cat > "$SIM/sim3d_advect_h.frag" <<EOF
#version 300 es
// sim3d_advect_h.frag - 3D semi-Lagrangian advection of hue.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform sampler2D u_field;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_dt;
$HELPERS

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 v = sampleAtlas(u_src, p).xyz;
  vec3 prev = clamp(p.xyz - v * u_dt, vec3(0.0), vec3(1.0));
  vec4 src = sampleAtlas(u_field, prev);
  fragColor = vec4(src.rgb, src.a);
}
EOF

# ---- buoyancy (3D) ----
cat > "$SIM/sim3d_buoyancy.frag" <<EOF
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
$HELPERS

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
EOF

# ---- project (3D) ----
cat > "$SIM/sim3d_project.frag" <<EOF
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
$HELPERS

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
EOF

# ---- heat (3D) ----
cat > "$SIM/sim3d_heat.frag" <<EOF
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
$HELPERS

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
EOF

# ---- phase (3D) ----
cat > "$SIM/sim3d_phase.frag" <<EOF
#version 300 es
// sim3d_phase.frag - 3D Allen-Cahn phase update.

precision highp float;
out vec4 fragColor;

uniform sampler2D u_src;
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;
uniform float u_surface_tension;
uniform float u_dt;
uniform float u_seed;
$HELPERS

void main(){
  vec3 p = fragToLocalXYZ(gl_FragCoord.xy);
  vec3 t = vec3(1.0 / u_grid_n);

  float pC = sampleAtlas(u_src, p).r;
  float pL = sampleAtlas(u_src, p - vec3(t.x, 0.0, 0.0)).r;
  float pR = sampleAtlas(u_src, p + vec3(t.x, 0.0, 0.0)).r;
  float pD = sampleAtlas(u_src, p - vec3(0.0, t.y, 0.0)).r;
  float pU = sampleAtlas(u_src, p + vec3(0.0, t.y, 0.0)).r;
  float pF = sampleAtlas(u_src, p - vec3(0.0, 0.0, t.z)).r;
  float pB = sampleAtlas(u_src, p + vec3(0.0, 0.0, t.z)).r;

  float lap = (pL + pR + pD + pU + pF + pB - 6.0 * pC);

  float mu = pC * pC * pC - pC - u_surface_tension * lap;
  float phi_new = pC + u_dt * 8.0 * mu;
  phi_new = clamp(phi_new, -1.5, 1.5);
  fragColor = vec4(phi_new, 0.0, 0.0, 1.0);
}
EOF

# ---- init_seed (3D) ----
cat > "$SIM/sim3d_init_seed.frag" <<EOF
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
EOF

echo "Generated 3D-tier shaders:"
ls -la "$SIM"/sim3d_*.frag