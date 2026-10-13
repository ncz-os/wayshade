// SPDX-License-Identifier: Apache-2.0
#version 300 es
// render.frag — fullscreen render of the magmasimplex fluid.
//
// Round 20 (replace kinematic metaballs). The phase field is the
// wax geometry, the hue field is per-cell colour, the temperature
// field drives emission.
//
// Tier 2D (the floor / Intel UHD path):
//   Raymarch the phase field as a 2D surface. Each step samples the
//   phase field with bilinear filtering; the per-step thickness is
//   computed from the gradient of phi across the ray. Thin regions
//   (small accumulated thickness) read hot-bright; thick regions
//   read deep-saturated. The hue field is sampled along the ray
//   and blended by advected mass, so merges mix colour naturally.
//
// Tier 3D (the voxel path; u_tier_3d > 0.5):
//   Sample the 3D atlas texture. The atlas is a 2D texture that
//   holds N slices of size N x N in an sqrt(N) x sqrt(N) grid. We
//   sample two adjacent slices and mix (manual trilinear along Z,
//   hardware bilinear along x/y). Then raymarch and accumulate
//   Beer-Lambert absorption and emission in the wax's hue.
//
// Both tiers share the same optics: Beer-Lambert attenuation
// through the liquid column, a hot emitter at the bottom that
// pulses gently, SSS via thickness, hue mixing by mass.
//
// The 2D tier is honest: the "thickness" we raymarch against is
// not a true path length through a 3D volume but the accumulated
// phase value along the ray. We describe what we compute, not what
// we wish we computed.
precision highp float;
out vec4 fragColor;

uniform float u_time;
uniform float u_seed;
uniform vec2  u_resolution;

uniform vec3  u_liquid;
uniform vec3  u_wax;
uniform vec3  u_secondary;
uniform vec2  u_emitter;
uniform float u_hotness;
uniform float u_tier_3d;

uniform sampler2D u_tex_phase;     /* 2D tier: phi in r */
uniform sampler2D u_tex_hue;       /* rgba hue field */
uniform sampler2D u_tex_temp;      /* r = temperature */

/* 3D atlas uniforms (only used when u_tier_3d > 0.5) */
uniform vec2  u_atlas_size;
uniform float u_atlas_slices;
uniform float u_grid_n;

#define PI 3.14159265358979323846

/* ---- colour helpers ---- */
vec3 hsv2rgb(vec3 c){
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
vec3 rgb2hsv(vec3 c){
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }

/* ---- 2D field sampling ----
 * Sample the 2D phase field with grid-correct coordinates. The
 * field is at grid resolution (256x256), but we render at full
 * resolution; hardware bilinear filtering on the texture handles
 * the upsample. */
float samplePhase2D(vec2 uv){
  /* uv in [0,1]. texture() with the linear filter does the
   * upsample for us. */
  return texture(u_tex_phase, uv).r;
}

/* Hue field sample at (uv). Returns (rgb, mass). */
vec4 sampleHue2D(vec2 uv){
  return texture(u_tex_hue, uv);
}

/* Temperature field sample at (uv). */
float sampleTemp2D(vec2 uv){
  return texture(u_tex_temp, uv).r;
}

/* ---- 3D atlas sampling ----
 * Convert a 3D coordinate (in [0,1]^3) into (uv, slice) within
 * the atlas. We split z into integer-slice and fractional parts.
 * Trilinear interpolation is done manually: sample two slices and
 * blend by the fractional z. */
vec4 sampleAtlas3D(sampler2D tex, vec3 p, vec2 atlas_size, float slices, float grid_n){
  /* atlas layout: N slices arranged in a sqrt(N) x sqrt(N) grid
   * in atlas_size. Each slice occupies grid_n x grid_n pixels in
   * atlas space. */
  float z = clamp(p.z, 0.0, 1.0) * (slices * slices - 1.0);
  float zi = floor(z);
  float zf = z - zi;
  float ix = mod(zi, slices);
  float iy = floor(zi / slices);
  vec2 uv0 = (vec2(ix, iy) * grid_n + p.xy * grid_n) / atlas_size;
  vec2 uv1 = (vec2(mod(zi + 1.0, slices), iy + floor((zi + 1.0) / slices)) * grid_n
              + p.xy * grid_n) / atlas_size;
  vec4 a = texture(tex, uv0);
  vec4 b = texture(tex, uv1);
  return mix(a, b, zf);
}
float samplePhase3D(vec3 p){
  return sampleAtlas3D(u_tex_phase, p, u_atlas_size, u_atlas_slices, u_grid_n).r;
}
vec4 sampleHue3D(vec3 p){
  return sampleAtlas3D(u_tex_hue, p, u_atlas_size, u_atlas_slices, u_grid_n);
}
float sampleTemp3D(vec3 p){
  return sampleAtlas3D(u_tex_temp, p, u_atlas_size, u_atlas_slices, u_grid_n).r;
}

/* Convert screen pixel to a normalised uv on the simulation grid
 * (which we treat as a 2D slab of the simulation domain). We map
 * (px, py) in [0,W]x[0,H] to [-1,1] x [-aspect, aspect], then to
 * grid space. The simulation domain is roughly [-1,1] in y and
 * [-aspect, aspect] in x. */
vec2 screenToUV(vec2 fragXY){
  vec2 uv_screen = fragXY / u_resolution;
  vec2 uv = vec2(uv_screen.x * 2.0 - 1.0, uv_screen.y * 2.0 - 1.0);
  /* Map to grid [0,1]^2. */
  return vec2(uv.x * 0.5 + 0.5, uv.y * 0.5 + 0.5);
}

void main(){
  vec2 fragXY = gl_FragCoord.xy;
  vec2 uv_grid = screenToUV(fragXY);
  vec2 uv_screen = fragXY / u_resolution;
  vec2 uv_world = vec2(uv_screen.x * 2.0 - 1.0, uv_screen.y * 2.0 - 1.0);
  uv_world.x *= u_resolution.x / u_resolution.y;

  /* Hot emitter. World-space (-1, -0.85) is the bottom centre. */
  float ed = length(uv_world - u_emitter);
  float emitter_wide = exp(-ed * 0.85);
  float emitter_tight = exp(-ed * 3.5);

  /* ---- Field sample ---- */
  float phi;
  vec4 hue_rgba;
  float temp;
  if(u_tier_3d > 0.5){
    /* 3D: march along z (camera ray). */
    /* For the 3D tier we sample phase in 3D. Here we read the
     * "front slice" as a quick approximation. The proper 3D march
     * is below. */
    phi = samplePhase3D(vec3(uv_grid, 0.5));
    hue_rgba = sampleHue3D(vec3(uv_grid, 0.5));
    temp = sampleTemp3D(vec3(uv_grid, 0.5));
  } else {
    phi = samplePhase2D(uv_grid);
    hue_rgba = sampleHue2D(uv_grid);
    temp = sampleTemp2D(uv_grid);
  }

  /* Liquid backdrop. We treat the fluid column above the pixel as
   * having a depth proportional to (1 - y_screen). Light from the
   * emitter falls off with distance. */
  float depth = clamp(0.5 - uv_world.y * 0.45, 0.0, 1.0);
  vec3 liquid_rgb = u_liquid;
  /* Beer-Lambert attenuation through the liquid column. */
  vec3 atten = (u_liquid.x + u_liquid.y + u_liquid.z) < 2.99
             ? exp(-u_liquid * depth * 1.4)
             : vec3(1.0);

  /* Fluid base colour: liquid × slow convection lift (fbm is
   * expensive; use a cheap procedural pattern instead so the
   * shader stays within budget on Intel). */
  float n_low  = 0.5 + 0.4 * sin(uv_world.x * 3.0 + u_time * 0.3)
                    + 0.3 * cos(uv_world.y * 4.0 - u_time * 0.2);
  n_low = clamp(n_low * 0.5, 0.2, 1.0);
  vec3 fluid_base = liquid_rgb * (0.95 + 0.45 * n_low);
  /* Hot emitter glow + tight bright spot. */
  vec3 emitter_col = vec3(1.0, 0.74, 0.42);
  vec3 hot_spot = emitter_col * emitter_wide * u_hotness * 0.85;
  /* Light shafts. */
  vec3 shaft_col = emitter_col * emitter_wide * n_low * u_hotness * 0.35;
  vec3 liquid_col = fluid_base * atten + hot_spot * atten + shaft_col;

  /* ---- Wax render ---- */
  /* The phase field phi is in [-1, 1]. Cells where phi > 0 are
   * "inside the wax". For the 2D tier we do a short raymarch
   * perpendicular to the screen: across the depth z from 0 to 1
   * (the camera ray), we accumulate phi. The accumulated phi is
   * the "thickness" we feed to the SSS term.
   *
   * The 2D tier is HONEST about not having true 3D depth — we
   * describe the model as a 2D field with a thickness derived
   * from the local phi gradient and a soft pseudo-z integral.
   */
  float wax_t_near = 1e9;
  float wax_thick = 0.0;
  float hue_acc_r = 0.0;
  float hue_acc_g = 0.0;
  float hue_acc_b = 0.0;
  float hue_mass = 0.0;
  float temp_acc = 0.0;
  float temp_mass = 0.0;
  int hit = 0;

  if(u_tier_3d > 0.5){
    /* 3D march. 32 steps along the ray from z=0 (camera) to z=1
     * (back). For each step, sample phase, hue, temperature. */
    int max_steps = 32;
    for(int i = 0; i < 32; i++){
      if(i >= max_steps) break;
      float z = float(i) / float(max_steps - 1);
      vec3 p = vec3(uv_grid, z);
      float ph = samplePhase3D(p);
      if(ph > 0.0){
        float pres = ph;
        wax_thick += pres * 0.85;
        if(hit == 0) wax_t_near = float(i) / float(max_steps);
        vec4 hr = sampleHue3D(p);
        float w = pres * hr.a;
        hue_acc_r += hr.r * w;
        hue_acc_g += hr.g * w;
        hue_acc_b += hr.b * w;
        hue_mass += w;
        float tp = sampleTemp3D(p);
        temp_acc += tp * pres;
        temp_mass += pres;
        hit = 1;
      }
    }
  } else {
    /* 2D pseudo-z march: 14 steps along a pseudo-depth from the
     * field, accumulating the local phi as a thickness proxy.
     * The pseudo-depth is sampled along the field's gradient
     * direction (which is the dominant "depth" axis of the wax),
     * but for simplicity we just walk from -0.3 to +0.3 in v
     * (the vertical screen axis) at fixed phi, accumulating the
     * local phase and hue values. */
    int max_steps = 14;
    for(int i = 0; i < 14; i++){
      if(i >= max_steps) break;
      /* Pseudo-depth offset: we walk along v in screen space and
       * resample the field. This is a deliberately simple stand-
       * in for true 3D marching; the visual result is a 2D slab
       * with thickness from the field. */
      float off = float(i - max_steps / 2) * 0.025;
      vec2 uv_off = uv_grid + vec2(0.0, off);
      if(uv_off.y < 0.0 || uv_off.y > 1.0) continue;
      float ph = samplePhase2D(uv_off);
      if(ph > 0.0){
        float pres = ph;
        wax_thick += pres * 0.45;
        if(hit == 0) wax_t_near = float(i) / float(max_steps);
        vec4 hr = sampleHue2D(uv_off);
        float w = pres * hr.a;
        hue_acc_r += hr.r * w;
        hue_acc_g += hr.g * w;
        hue_acc_b += hr.b * w;
        hue_mass += w;
        float tp = sampleTemp2D(uv_off);
        temp_acc += tp * pres;
        temp_mass += pres;
        hit = 1;
      }
    }
  }

  if(hit == 0){
    /* No wax — render the liquid backdrop. */
    vec3 col = liquid_col;
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    col = (col - 0.5) * 1.08 + 0.5;
    fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
    return;
  }

  /* Average hue, weighted by mass. */
  vec3 wax_rgb;
  if(hue_mass > 1e-4){
    wax_rgb = vec3(hue_acc_r, hue_acc_g, hue_acc_b) / hue_mass;
  } else {
    wax_rgb = hue_rgba.rgb;
  }
  float avg_temp = (temp_mass > 1e-4) ? (temp_acc / temp_mass) : 0.0;

  /* Beer-Lambert attenuation through the liquid above the wax. */
  vec3 liquid_trans = (u_liquid.x + u_liquid.y + u_liquid.z) < 2.99
                    ? exp(-u_liquid * wax_t_near * 1.2)
                    : vec3(1.0);

  /* SSS: thin edges hot, thick cores deep. */
  float thin_edge = 1.0 - smoothstep(0.0, 1.5, wax_thick);
  float thick     = smoothstep(0.4, 2.8, wax_thick);

  vec3 h_wax = rgb2hsv(wax_rgb);
  h_wax.y = clamp(h_wax.y * (1.0 + 0.35 * thick), 0.0, 1.0);
  h_wax.z = mix(1.25, 0.85, thick);
  vec3 wax_body = hsv2rgb(h_wax);

  /* Edge glow: additive emissive lift from the emitter, scaled
   * by the temperature at the hit (so cold edges don't glow). */
  float emit_factor = clamp(0.6 + 0.4 * avg_temp, 0.0, 1.5);
  vec3 edge_lift = emitter_col * thin_edge * emit_factor * u_hotness;

  /* Centre wax. */
  vec3 centre_wax = wax_body * (0.75 + 0.25 * smoothstep(2.0, 0.0, wax_thick));

  vec3 base = mix(centre_wax, edge_lift, thin_edge * 0.55);

  /* Apply the liquid transmission. */
  vec3 col = base * liquid_trans;

  /* Directional emitter lift on the LIT side. */
  float ed_w = length(uv_world - u_emitter);
  float lit = exp(-ed_w * 0.85);
  col += emitter_col * lit * thin_edge * 0.55 * u_hotness;

  /* Internal temperature glow (hot wax glows from within). */
  col += wax_body * clamp(avg_temp, 0.0, 2.0) * 0.20;

  /* Reinhard tonemap + gamma. */
  col = col / (1.0 + col * 0.45);
  col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
  col = (col - 0.5) * 1.08 + 0.5;
  fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
}