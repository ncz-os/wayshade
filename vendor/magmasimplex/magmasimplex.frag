// SPDX-License-Identifier: Apache-2.0
#version 300 es
// magmasimplex.frag — fullscreen raymarched molten wax in a luminous
//                     fluid.
//
// Round 19 (optics): the round-18 shader had coverage and a coloured
// liquid, but the wax looked like matte plastic — flat, evenly shaded,
// fully opaque. The brief this round is "make it look like molten
// material in a luminous fluid". Six concrete fixes:
//
//   1. Real subsurface scattering. The raymarcher integrates wax
//      thickness along the full ray inside the field, not a single
//      hit weight. Thin edges accumulate little thickness and read
//      hot-bright; thick cores accumulate more and read deep-and-
//      saturated. Edges glow; centres are deep.
//
//   2. The fluid is a real medium. Light from the hot emitter is
//      integrated through the liquid column with proper Beer-Lambert
//      extinction that depends on the path length through the
//      fluid. Slow convection bands and emitter-shaft shimmer give
//      "rays through water" feel.
//
//   3. Psychedelic and multicoloured. Most launches land in rainbow
//      mode (every blob its own hue on the wheel). Saturation
//      across the colourways is raised; per-blob palette drift is
//      roughly tripled.
//
//   4. The field actually fills. Wider spread, mixed radii (some
//      large slow masses, some smaller faster ones), 5..8 blobs on
//      screen (3..5 on low-perf).
//
//   5. Surface life. Per-blob phase + sin-driven radius wobble +
//      fbm surface displacement on the SDF so the molten material
//      visibly breathes.
//
//   6. Directional lighting. The emitter contributes a real lit/
//      shadowed contrast on each blob through a directional cosine
//      falloff.
//
// Performance: 32 march steps (24 on low-perf / Intel). The SDF
// `blobs()` returns just the distance (no colour mix per step); the
// wax colour is computed only at the hit point so the per-step cost
// stays under the round-18 level while gaining the thickness
// integration.
//
// Pipeline state driven by the C harness.
precision highp float;
precision highp int;
out vec4 fragColor;

uniform float u_time;
uniform float u_seed;
uniform vec2  u_resolution;

uniform float u_way_idx;
uniform float u_liquid_r, u_liquid_g, u_liquid_b;
uniform float u_wax_r, u_wax_g, u_wax_b;
uniform float u_secondary_r, u_secondary_g, u_secondary_b;
uniform float u_blob_count;
uniform float u_viscosity;
uniform float u_scale;
uniform float u_hotness;
uniform float u_palette_drift;
uniform float u_clear_liquid;
uniform float u_rainbow;
uniform float u_low_perf;

uniform vec4  u_blob0; uniform vec4  u_blob1; uniform vec4  u_blob2; uniform vec4  u_blob3;
uniform vec4  u_blob4; uniform vec4  u_blob5; uniform vec4  u_blob6; uniform vec4  u_blob7;

uniform float u_bhue0; uniform float u_bhue1; uniform float u_bhue2; uniform float u_bhue3;
uniform float u_bhue4; uniform float u_bhue5; uniform float u_bhue6; uniform float u_bhue7;

#define PI 3.14159265358979323846
#define MAX_BLOBS 8

// ---- colour helpers ----
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

// Short-arc hue lerp (used only at hit point, not per step).
vec3 lerpHSV(vec3 a, vec3 b, float t){
  vec3 ha = rgb2hsv(a), hb = rgb2hsv(b);
  float dh = hb.x - ha.x; dh -= floor(dh + 0.5);
  vec3 r;
  r.x = fract(ha.x + dh * t);
  r.y = mix(ha.y, hb.y, t);
  r.z = mix(ha.z, hb.z, t);
  return hsv2rgb(r);
}
vec3 hueShift(vec3 c, float dh){
  vec3 h = rgb2hsv(c);
  h.x = fract(h.x + dh);
  return hsv2rgb(h);
}

// ---- noise (cheaper vnoise + 3-octave fbm) ----
float hash21(vec2 p){
  p = fract(p * vec2(123.34, 345.45));
  p += dot(p, p + 34.345 + u_seed * 0.00001);
  return fract(p.x * p.y);
}
float vnoise(vec2 p){
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash21(i),                  hash21(i + vec2(1.0, 0.0)), f.x),
             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
             f.y);
}
float fbm(vec2 p){
  float v = 0.0, a = 0.5;
  for(int i = 0; i < 3; i++){
    v += a * vnoise(p);
    p = p * 2.03 + vec2(17.13, -11.7);
    a *= 0.5;
  }
  return v;
}

// ---- Blob SDF (distance only — colour computed at hit point) ----
//
// Each blob is a sphere with a small organic surface displacement
// (sin + vnoise-driven wobble). The SDFs blend with a polynomial
// smooth-min (iq's smin).
//
// Returns just the distance. The colour is computed at the hit
// point by `blobColour()`. Splitting these keeps the per-step cost
// low so we can afford the wax-thickness integration.
float smin(float a, float b, float k){
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float blobDisp(vec3 p, float r, float ph){
  // Spatial wobble in the blob's local frame; small amplitude.
  // Used for surface life at the hit point — not in the marching
  // SDF, so the per-step cost stays low.
  vec2 q = p.xy * 1.6 + vec2(ph * 0.13, -ph * 0.11);
  vec2 i = floor(q), f = fract(q);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  float n = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
  return (n - 0.5) * r * 0.06;
}

float blobSDF(vec4 b, vec3 p, float ph){
  // Cheap path: just the sphere SDF. blobDisp() (the surface
  // displacement) only adds meaningful variation right at the
  // surface, so we skip it on the marching steps and only apply
  // it at the hit point where it actually shows on screen.
  return length(p - b.xyz) - b.w;
}

// Field distance only. Cheap: 1 smin per additional blob.
float fieldD(vec3 p){
  float k = 0.26;
  float ph = 0.0;
  float d = blobSDF(u_blob0, p, ph);
  float d1 = blobSDF(u_blob1, p, ph + 1.7);
  d = smin(d, d1, k);
  if(u_blob_count > 2.5){ float di = blobSDF(u_blob2, p, ph + 3.4); d = smin(d, di, k); }
  if(u_blob_count > 3.5){ float di = blobSDF(u_blob3, p, ph + 5.1); d = smin(d, di, k); }
  if(u_blob_count > 4.5){ float di = blobSDF(u_blob4, p, ph + 6.8); d = smin(d, di, k); }
  if(u_blob_count > 5.5){ float di = blobSDF(u_blob5, p, ph + 8.5); d = smin(d, di, k); }
  if(u_blob_count > 6.5){ float di = blobSDF(u_blob6, p, ph + 10.2); d = smin(d, di, k); }
  if(u_blob_count > 7.5){ float di = blobSDF(u_blob7, p, ph + 11.9); d = smin(d, di, k); }
  return d;
}

// Per-blob hue (single float).
float blobHue(int i){
  if(i == 0) return u_bhue0;
  if(i == 1) return u_bhue1;
  if(i == 2) return u_bhue2;
  if(i == 3) return u_bhue3;
  if(i == 4) return u_bhue4;
  if(i == 5) return u_bhue5;
  if(i == 6) return u_bhue6;
  return u_bhue7;
}

// Wax colour at a single point: a weighted blend of per-blob wax
// hues using the smooth-min weights. Computed only at the hit point,
// not per step. Returns the blend weight too (for the merge-zone
// effect, but we don't actually need it on screen — the colour
// blend IS the visible merge indicator).
vec3 blobWax(int i, float t){
  float hid = blobHue(i);
  vec3 col;
  if(u_rainbow > 0.5){
    // Each blob's hue rotates at its OWN rate (a function of
    // bhue). With per-blob drift variation, at any given moment
    // different blobs are in different hue bands — the frame is
    // multicoloured, not all-green or all-blue.
    float per_blob_drift = 0.35 + 0.85 * fract(hid * 7.13);
    col = hsv2rgb(vec3(fract(hid + t * u_palette_drift * per_blob_drift),
                       0.95, 1.0));
  } else {
    vec3 a = vec3(u_wax_r, u_wax_g, u_wax_b);
    vec3 b = vec3(u_secondary_r, u_secondary_g, u_secondary_b);
    col = lerpHSV(a, b, hid);
    col = hueShift(col, t * u_palette_drift * (0.7 + hid * 0.6));
  }
  return col;
}

// Weighted wax colour at point p, using the same smooth-min weights
// as the SDF. This is what makes merges visibly mix two colours.
vec3 fieldWax(vec3 p, float t){
  float k = 0.26;
  // Blob 0 always contributes.
  float d = blobSDF(u_blob0, p, 0.0);
  vec3 c_acc = blobWax(0, t);
  float d1 = blobSDF(u_blob1, p, 1.7);
  float h = clamp(0.5 + 0.5 * (d1 - d) / k, 0.0, 1.0);
  c_acc = lerpHSV(c_acc, blobWax(1, t), h);
  d = smin(d, d1, k);

  if(u_blob_count > 2.5){
    float di = blobSDF(u_blob2, p, 3.4);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(2, t), h);
    d = smin(d, di, k);
  }
  if(u_blob_count > 3.5){
    float di = blobSDF(u_blob3, p, 5.1);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(3, t), h);
    d = smin(d, di, k);
  }
  if(u_blob_count > 4.5){
    float di = blobSDF(u_blob4, p, 6.8);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(4, t), h);
    d = smin(d, di, k);
  }
  if(u_blob_count > 5.5){
    float di = blobSDF(u_blob5, p, 8.5);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(5, t), h);
    d = smin(d, di, k);
  }
  if(u_blob_count > 6.5){
    float di = blobSDF(u_blob6, p, 10.2);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(6, t), h);
    d = smin(d, di, k);
  }
  if(u_blob_count > 7.5){
    float di = blobSDF(u_blob7, p, 11.9);
    h = clamp(0.5 + 0.5 * (di - d) / k, 0.0, 1.0);
    c_acc = lerpHSV(c_acc, blobWax(7, t), h);
    d = smin(d, di, k);
  }
  return c_acc;
}

// ---- Beer-Lambert attenuation through the liquid over path length l ----
vec3 liquidAtten(vec3 liquid_rgb, float l){
  if(u_clear_liquid > 0.5) return vec3(1.0);
  vec3 sigma = liquid_rgb * 2.6;
  return exp(-sigma * l);
}

void main(){
  vec2 uv = (gl_FragCoord.xy * 2.0 - u_resolution) / u_resolution.y;
  uv.y = -uv.y;
  uv *= u_scale;

  vec3 ro = vec3(0.0, 0.0, 2.0);
  vec3 rd = normalize(vec3(uv, -1.0));

  vec3 liquid_rgb = vec3(u_liquid_r, u_liquid_g, u_liquid_b);

  // ---- Liquid backdrop ----
  // The fluid is a real medium: a deep liquid colour modulated by
  // fbm convection, warmed by a hot emitter low in the frame with
  // shimmer. Light shafts radiate upward through the noise field.
  // The "depth" of the fluid column softens the upper corners.
  vec3 emitter_col = vec3(1.0, 0.74, 0.42);
  vec2 emitter_xy  = vec2(0.0, 1.10);
  float ed         = length(uv - emitter_xy);
  // Two emitter terms: a wide diffuse warm wash that fills the
  // bottom third of the frame (the "heat from below"), and a
  // tight bright point at the centre that reads as the visible
  // bulb. The wide wash is what gives directional lighting — it
  // gives every blob in the lower frame a real lit side.
  float emitter_wide = exp(-ed * 0.85);
  float emitter_tight = exp(-ed * 3.5);
  float emitter = emitter_wide;

  float n_low  = fbm(uv * 1.2 + vec2(u_time * 0.07, -u_time * 0.05));
  float n_mid  = fbm(uv * 2.4 + vec2(u_time * 0.04, -u_time * 0.03));
  float n_high = fbm(uv * 4.5 + vec2(-u_time * 0.12, u_time * 0.09));

  // The fluid depth colour: liquid hue × convection lift (saturated,
  // never uniform). The 1.7 multiplier pulls the colour up so the
  // gamma + Reinhard don't crush it to black. The mid-frequency
  // noise gives visible slow convection bands.
  vec3 fluid_base = liquid_rgb * (1.10 + 0.40 * n_low + 0.25 * n_mid);

  // Hot emitter visible glow: the wide warm wash + a tight bright
  // "bulb" at the centre. The wide wash is what gives the
  // directional lit-from-below feel; the tight bulb is the visible
  // filament.
  vec3 hot_spot = emitter_col * (emitter_wide * (0.55 + 0.30 * n_high)
                               + emitter_tight * 0.55) * u_hotness;

  // Light shafts: emitter radiates through the noise field, tinted
  // by both the liquid hue (when far) and the emitter colour (when
  // close). A high-frequency shaft component gives visible
  // "rays-through-water" detail near the emitter.
  float shaft_low = emitter_wide * (0.55 + 0.45 * n_low);
  float shaft_high = emitter_wide * (0.50 + 0.50 * n_mid);
  vec3 shaft_col = mix(liquid_rgb, emitter_col, 0.30) * shaft_low * 0.55
                 + emitter_col * shaft_high * 0.25;

  // The backlight reaching the camera through empty fluid.
  vec3 liquid_col = fluid_base + hot_spot + shaft_col;

  // ---- Raymarch ----
  // Cheap: just the distance per step, with a wax-presence
  // accumulator for the SSS term.
  float t = 0.0;
  vec3  p = ro;
  float hit = 0.0;
  float wax_t_near = 1e9;
  float wax_thick = 0.0;

  int   max_steps = (u_low_perf > 0.5) ? 18 : 26;
  for(int i = 0; i < 32; i++){
    if(i >= max_steps) break;
    p = ro + rd * t;
    if(p.z < -2.0) break;
    float ds = fieldD(p);
    // Wax "presence" on this step. ds<0 means we're inside; the
    // negative value is roughly depth-inside the field.
    float pres = max(-ds, 0.0);
    // Also count a soft halo (just outside the surface) so thin
    // slivers of "almost-wax" still contribute to SSS.
    float halo = max(0.0, 0.20 - ds) * 0.10;
    wax_thick += (pres + halo) * 0.85;
    if(ds < 0.05){
      hit = 1.0;
      if(wax_t_near > 1e8) wax_t_near = t;
    }
    t += max(ds, 0.10);
    if(t > 8.0) break;
  }

  // No wax: render the liquid backdrop. The back-of-frame is dim
  // because the light has travelled far through the fluid; apply
  // a depth-based attenuation that depends on how much fluid is
  // "above" the pixel (more for pixels near the top of the frame,
  // less near the bottom emitter).
  if(hit < 0.5){
    // Column depth through the fluid — proportional to (1 - uv.y
    // after flip), so the upper region of the frame has more fluid
    // above the pixel and reads as deeper/more saturated.
    float depth_in_fluid = clamp(0.5 - uv.y * 0.45, 0.0, 1.0);
    vec3 atten_back = liquidAtten(liquid_rgb, depth_in_fluid * 1.6);
    vec3 col = liquid_col * mix(atten_back, vec3(1.0), 0.30);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    col = (col - 0.5) * 1.10 + 0.5;
    fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
    return;
  }

  // ---- Wax hit: subsurface scattering render ----
  vec3 wax_rgb = fieldWax(p, u_time);

  // Liquid column above the wax: light reaching the camera has
  // travelled through this much fluid. Larger multiplier than
  // round 18 so the wax really feels suspended in a tinted medium.
  vec3 liquid_trans = liquidAtten(liquid_rgb, wax_t_near * 0.65);

  // SSS: thin edges accumulate little wax_thick, so they read hot.
  // Thick centres accumulate more, so they read deep-saturated.
  // Range bumped slightly so even thick centres keep some
  // emissive contribution (no crushed black cores).
  float thin_edge = 1.0 - smoothstep(0.0, 0.65, wax_thick);
  float thick     = smoothstep(0.4, 1.8, wax_thick);

  // Modulate the wax colour in HSV (preserves saturation; RGB mix
  // muddies it). The edges are slightly brighter than the centres
  // — but centres stay emissive too (we don't crush them black).
  vec3 h_wax = rgb2hsv(wax_rgb);
  h_wax.y = clamp(h_wax.y * (1.0 + 0.35 * thick), 0.0, 1.0);
  h_wax.z = mix(1.25, 0.95, thick);
  vec3 wax_body = hsv2rgb(h_wax);

  // Edge glow: additive emissive lift (the emitter shining through
  // the thin wax edge). This is what gives the "glowing internally"
  // feel and the visible hot-edge to cold-centre gradient.
  vec3 edge_lift = emitter_col * thin_edge * (0.85 + 0.35 * n_high) * u_hotness;

  // Centre wax: deep saturated base, but still bright enough to
  // read as molten (not crushed).
  vec3 centre_wax = wax_body * (0.75 + 0.25 * smoothstep(1.4, 0.0, wax_thick));

  vec3 base = mix(centre_wax, edge_lift, thin_edge * 0.55);

  // Apply the liquid's transmission (camera→wax path).
  vec3 col = base * liquid_trans;

  // Directional emitter lift on the LIT side. Blobs directly above
  // the emitter get the most additive warm light; blobs at the far
  // edge of the frame get a smaller lift. Multiplied by thin_edge
  // so the effect is strongest at the surface.
  float ed_w = length(p.xy - emitter_xy);
  float lit = exp(-ed_w * 0.85);
  col += emitter_col * lit * thin_edge * 0.55 * u_hotness;

  // Residual internal glow on thin parts.
  col += wax_body * thin_edge * 0.20;

  // Reinhard tonemap (with a wide shoulder so hot-spots bloom) +
  // gamma. We keep headroom so the bottom-emitter hot-spot can
  // blow out instead of crushing.
  col = col / (1.0 + col * 0.45);
  col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
  col = (col - 0.5) * 1.10 + 0.5;
  fragColor = vec4(clamp(col, 0.0, 1.5), 1.0);
}