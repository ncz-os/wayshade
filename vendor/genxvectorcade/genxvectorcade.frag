// SPDX-License-Identifier: Apache-2.0
#version 300 es
// genxvectorcade.frag — fullscreen psychedelic vector-arcade journey.
//
// One single fragment shader, five blended movements, one persistent
// feedback texture. The host (src/gles3_genxvectorcade.c) owns a ping-
// pong pair of half-resolution RGBA8 FBOs; each frame it (a) copies
// the previous FBO onto the new one with a configurable fade and a
// rotation+scale (so wakes spiral), (b) draws the current geometry on
// top via this shader, and (c) blits the result to the default FB.
//
// Movements blend via u_phase[0..4] weights (sum to 1.0). They never
// hard-cut -- the CPU advances the phase vector smoothly so the
// geometry *becomes* the next thing rather than cutting to it.
//
// Original work in a genre. See git history for design notes.
precision highp float;
out vec4 fragColor;

uniform float u_time;
uniform vec2  u_resolution;
uniform sampler2D u_prev;     // previous frame (for additive bloom & chromatic spiral)
uniform float u_fade;          // 0.86..0.96: per-frame persistence of the trail buffer

// Journey phase weights — five movements, sum 1.0. The CPU advances
// these smoothly each frame; we just blend the contributions.
uniform vec4 u_phase_ab;       // tunnel, grid_horizon, starfield_warp, kaleido
uniform float u_phase_e;       // lattice_fractal
// Decoded: phase0=tunnel, phase1=grid_horizon, phase2=starfield_warp,
// phase3=kaleido, phase_e=lattice_fractal.

// Movement parameters (randomised per-launch, modulated by time).
uniform vec2 u_shape_speed;    // x: cross-section shape index 0..5 (tri/sq/pent/hex/circle/star)
                               // y: travel speed multiplier 0.6..1.6
uniform vec2 u_seg_rot;        // x: segment count 6..18, y: rotation rate 0.2..1.4
uniform vec2 u_pal_pair;       // x: palette index 0..5, y: palette cycling rate 0.08..0.24
uniform float u_pal_phase;     // 0..1, drift starting offset
uniform float u_pal_contrast;  // 0.9..1.4, contrast toe
uniform vec2 u_sym_burst;      // x: symmetry order (continuous, 2..24), y: burst frequency
uniform float u_trail_persist; // 0.86..0.97
uniform float u_warp_amount;   // 0.0..1.6, domain warping strength
uniform float u_ca_amount;     // 0.0..0.018, chromatic aberration
uniform float u_pulse;         // beat-like internal pulse 0..1 (CPU computes)
uniform float u_flash;         // brief full-frame flash on movement boundaries, 0..1
uniform float u_seed;

// Phase 2 additions: the evolving phosphor ecosystem.
uniform sampler2D u_state;     // low-res life state (R density, G chem, B genome, A age)
uniform float u_tick;          // integer simulation tick — never the float time
uniform vec4  u_glow_pools;    // x,y,r,type for one cabinet pool (more via [1..3] would need arrays)
uniform float u_reveal;        // 0..1, depth reveal moment intensity (one per cycle)

// ---- HSL helpers (palette interpolation in hue-space) ----
const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;
vec3 hsl2rgb(vec3 c){
    vec3 rgb = clamp(abs(mod(c.x*6.0 + vec3(0.0,4.0,2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return c.z + c.y * (rgb - 0.5) * (1.0 - abs(2.0*c.z - 1.0));
}
float hueLerp(float a,float b,float k){ float d=b-a; d-=floor(d+0.5); return a+d*k; }
struct Stop{ float h,s,l; };
Stop stopLerp(Stop a,Stop b,float k){
    return Stop(hueLerp(a.h,b.h,k), a.s+(b.s-a.s)*k, a.l+(b.l-a.l)*k);
}
// Six palette pairs (complementary neon pairs as in the brief).
// 0 magenta/cyan  1 orange/blue  2 lime/violet  3 hot/cool  4 red/teal  5 gold/magenta
void paletteStops(float idx, out Stop s0, out Stop s1, out Stop s2, out Stop s3){
    int i = int(idx);
    if(i == 0){
        s0=Stop(0.92,0.95,0.10); s1=Stop(0.85,0.90,0.42);
        s2=Stop(0.50,0.95,0.66); s3=Stop(0.52,0.85,0.96);
    } else if(i == 1){
        s0=Stop(0.06,0.95,0.10); s1=Stop(0.08,0.95,0.42);
        s2=Stop(0.60,0.90,0.62); s3=Stop(0.66,0.75,0.96);
    } else if(i == 2){
        s0=Stop(0.30,0.95,0.10); s1=Stop(0.26,0.90,0.42);
        s2=Stop(0.78,0.85,0.62); s3=Stop(0.82,0.75,0.96);
    } else if(i == 3){
        s0=Stop(0.97,0.95,0.10); s1=Stop(0.99,0.90,0.42);
        s2=Stop(0.55,0.90,0.62); s3=Stop(0.55,0.65,0.96);
    } else if(i == 4){
        s0=Stop(0.99,0.95,0.10); s1=Stop(0.01,0.85,0.42);
        s2=Stop(0.49,0.90,0.62); s3=Stop(0.50,0.80,0.96);
    } else {
        s0=Stop(0.13,0.95,0.10); s1=Stop(0.10,0.90,0.42);
        s2=Stop(0.92,0.90,0.62); s3=Stop(0.85,0.70,0.96);
    }
}
vec3 palette(float t){
    float active_h = u_pal_phase + u_time * u_pal_pair.y;
    Stop s0,s1,s2,s3;
    paletteStops(u_pal_pair.x, s0, s1, s2, s3);
    s0.h=fract(s0.h+active_h); s1.h=fract(s1.h+active_h);
    s2.h=fract(s2.h+active_h); s3.h=fract(s3.h+active_h);
    Stop r;
    if(t<0.33)      r=stopLerp(s0,s1,t/0.33);
    else if(t<0.66) r=stopLerp(s1,s2,(t-0.33)/0.33);
    else if(t<0.90) r=stopLerp(s2,s3,(t-0.66)/0.24);
    else            r=s3;
    return hsl2rgb(vec3(r.h, r.s, r.l));
}

// ---- hash + value noise (cheap; we don't need real fbm here) ----
float hash21(vec2 p){
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345 + u_seed*0.0001);
    return fract(p.x * p.y);
}
float hash11(float n){ return fract(sin(n)*43758.5453 + u_seed*0.1); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(hash21(i),                hash21(i+vec2(1,0)), f.x),
               mix(hash21(i+vec2(0,1)),      hash21(i+vec2(1,1)), f.x), f.y);
}
float fbm2(vec2 p){
    float v = 0.0, a = 0.5;
    for(int i = 0; i < 4; i++){
        v += a * vnoise(p);
        p = p * 2.03 + vec2(17.13, -11.7);
        a *= 0.5;
    }
    return v;
}

// ---- 2D rotation + kaleidoscope fold ----
vec2 rot2(vec2 p, float a){
    float c = cos(a), s = sin(a);
    return vec2(c*p.x - s*p.y, s*p.x + c*p.y);
}
vec2 kaleido(vec2 p, float order){
    // Mirror into wedge of angle PI/order, then reflect into [0, PI/order]
    float wedge = PI / order;
    float a = atan(p.y, p.x);
    float r = length(p);
    a = mod(a, 2.0*wedge);
    a = abs(a - wedge);
    return vec2(cos(a), sin(a)) * r;
}

// ---- domain warp: bend the coord field with FBM ----
vec2 warp(vec2 p, float amt){
    vec2 q = vec2(fbm2(p*1.7 + vec2(0.0, u_time*0.3)),
                  fbm2(p*1.7 + vec2(11.0, u_time*0.27)));
    return p + amt * (q - 0.5);
}

// ---- movement: vector tunnel -------------------------------------
// Generates a glowing polygon cross-section receding into z, with
// per-segment line intensity. z = log depth (large = far).
float tunnel(vec2 uv, out vec3 baseCol){
    baseCol = vec3(0.0);
    float intensity = 0.0;
    // aspect-corrected uv centered on vanishing point. Use a wider
    // FOV so the tunnel fills the screen rather than leaving big
    // margins of black.
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * vec2(aspect * 1.6, 1.0 * 1.6);
    // tunnel depth progresses with time
    float speed = u_shape_speed.y;
    float z = 1.5 + 4.0 * fract(u_time * 0.07 * speed);
    float r = length(p);
    float a = atan(p.y, p.x);
    // domain warp bends the "straight" tunnel so motion breathes
    vec2 wp = warp(p * 0.7, u_warp_amount * 0.35);
    a += 0.6 * sin(z*1.3 + u_time*0.5) + 0.25 * (wp.x - 0.5);
    // cross-section shape: signed distance to a regular n-gon
    float sides = 3.0;
    if(u_shape_speed.x < 0.5)       sides = 3.0;
    else if(u_shape_speed.x < 1.5)  sides = 4.0;
    else if(u_shape_speed.x < 2.5)  sides = 5.0;
    else if(u_shape_speed.x < 3.5)  sides = 6.0;
    else if(u_shape_speed.x < 4.5)  sides = 32.0;  /* circle */
    else                            sides = 5.0;   /* star: 5-gon with inner radius factor */
    float starInner = (u_shape_speed.x >= 5.5) ? 0.55 : 1.0;
    float ang = a + u_time * u_seg_rot.y * 0.15;
    float ca = cos(ang/sides), sa = sin(ang/sides);
    vec2 q = vec2(ca*p.x + sa*p.y, -sa*p.x + ca*p.y);
    float polyR = abs(q.y);
    float segCount = floor(u_seg_rot.x);
    float ringT = log(1.0 + z * 2.0) * 1.5;
    float ringF = fract(ringT);
    float ringDist = abs(ringF - 0.5);
    float ringLine = exp(-pow(ringDist * 32.0, 2.0));
    // longitudinal lines along the polygon — wider so they fill the
    // screen at the near end
    float longLine = exp(-pow(polyR * 7.0, 2.0));
    // radial streaks along tunnel axis
    float radial = exp(-pow(r * 18.0, 2.0)) * 0.5;
    // depth fade: weaker so the edges stay visible
    float depthFade = exp(-r * 0.9);
    float core = exp(-pow(r * 60.0, 2.0)) * 1.4;
    intensity = (longLine * 0.9 + ringLine * 0.7 + radial * 0.4 + core) * depthFade;
    // colour: hue varies with depth and segment
    float t = clamp(0.10 + 0.55 * ringT - 0.20 * ringF, 0.0, 1.0);
    baseCol = palette(t);
    // overexposure: hot core near vanishing point
    float near = exp(-pow(r * 18.0, 2.0));
    baseCol += vec3(1.0, 0.95, 0.85) * near * 1.4;
    return intensity;
}

// ---- movement: grid horizon --------------------------------------
// Pitch the camera down. Unroll the tunnel into an infinite ground
// plane streaking toward a horizon. Synthwave sun behind it.
float gridHorizon(vec2 uv, out vec3 baseCol){
    baseCol = vec3(0.0);
    float intensity = 0.0;
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    // camera pitch - the horizon line is at uv.y ~= +0.1 (slightly above mid)
    float horizon = 0.10;
    if(p.y < horizon){
        // ground plane: y -> depth; x -> lateral
        float depth = 1.0 / max(horizon - p.y, 0.02);
        float lateral = p.x * depth;
        // time-streaked grid
        float speed = u_shape_speed.y * 1.4;
        float gd = depth + u_time * speed * 2.0;
        // warping for an "alive" feel
        vec2 wp = warp(vec2(lateral*0.7, gd*0.5), u_warp_amount * 0.30);
        lateral += (wp.x - 0.5) * 0.4;
        gd += (wp.y - 0.5) * 1.0;
        // grid lines
        float gridU = abs(fract(lateral) - 0.5);
        float gridV = abs(fract(gd) - 0.5);
        float lineU = smoothstep(0.05, 0.0, gridU);
        float lineV = smoothstep(0.05, 0.0, gridV);
        float gridIntensity = (lineU + lineV) * 1.2;
        // fade with distance
        float fade = exp(-(depth - 1.0) * 0.08);
        intensity = gridIntensity * fade * 0.9;
        // colour: magenta-to-cyan along depth
        float t = clamp(0.20 + 0.40 * fract(gd*0.15), 0.0, 1.0);
        baseCol = palette(t) * 1.5;
        // hot core on bright lines
        baseCol += vec3(1.0, 0.9, 1.1) * (lineU*lineV) * 2.0 * fade;
    } else {
        // sky: synthwave sun disc
        vec2 sp = p - vec2(0.0, horizon + 0.18);
        float r = length(sp);
        float disc = smoothstep(0.18, 0.16, r);
        float bloom = smoothstep(0.45, 0.0, r);
        // horizontal bands cutting the sun
        float bands = step(0.5, sin(sp.y * 38.0 + u_time*1.2)) ;
        disc *= mix(0.4, 1.0, bands);
        intensity = disc * 1.2 + bloom * 0.45;
        // sky gradient: deep violet -> magenta -> orange near horizon
        float t = clamp(0.55 + 0.20 * (1.0 - p.y), 0.0, 1.0);
        vec3 sky = palette(t);
        baseCol = sky * (disc * 1.4 + bloom * 0.5) + sky * 0.05 * (1.0 - disc);
    }
    return intensity;
}

// ---- movement: starfield warp ------------------------------------
float starfieldWarp(vec2 uv, out vec3 baseCol){
    baseCol = vec3(0.0);
    float intensity = 0.0;
    vec2 p = (uv - 0.5) * 2.0;   /* wider FOV */
    // radial streak field: lines stretch toward vanishing point
    float r = length(p);
    float a = atan(p.y, p.x);
    // kaleidoscope a touch for "folding hyperspace"
    vec2 kp = kaleido(p * 1.4, max(u_sym_burst.x * 0.5, 4.0));
    float speed = u_shape_speed.y * 0.8;
    float z = 1.0 + 6.0 * fract(u_time * 0.06 * speed);
    // ring the stars along expanding shells
    float ringR = 0.05 + 0.6 * z * 0.16;
    float dRing = abs(r - ringR);
    float ringLine = exp(-pow(dRing * 60.0, 2.0)) * (1.0 / (z*0.7));
    // streaks along radial direction: each "star" is a line from r=ringR outward
    float streakLen = 0.06 + 0.10 * (1.0 / z);
    float streak = exp(-pow((r - ringR + streakLen*0.5) / max(streakLen, 1e-4), 2.0));
    streak *= smoothstep(ringR, ringR + streakLen, r);
    float aJitter = 0.18 * sin(a * 31.0 + u_time * 0.4 + u_seed);
    intensity = (ringLine * 1.0 + streak * 0.9) * (1.0 + 0.4 * sin(a*5.0 + aJitter));
    // dust nebula
    vec2 q = warp(kp * 1.3, u_warp_amount * 0.55);
    float dust = fbm2(q * 2.2 + vec2(u_time*0.10, -u_time*0.07));
    intensity += pow(dust, 2.5) * 0.8;
    // colour: hot star core, complementary fringe
    float t = clamp(0.30 + 0.45 * z * 0.10, 0.0, 1.0);
    baseCol = palette(t) * 1.3 + vec3(1.0) * exp(-pow(r * 3.0, 2.0)) * 0.5;
    return intensity;
}

// ---- movement: kaleidoscope bloom -------------------------------
float kaleidoBloom(vec2 uv, out vec3 baseCol){
    baseCol = vec3(0.0);
    float intensity = 0.0;
    vec2 p = (uv - 0.5);
    // wider FOV so the arms fill the screen
    p *= 2.4;
    // kaleidoscope order climbs with the pulse
    float order = max(u_sym_burst.x * (0.7 + 0.6 * u_pulse), 3.0);
    vec2 kp = kaleido(p * (1.4 + 0.5 * sin(u_time*0.4)), order);
    // warped, recursive
    kp = warp(kp * 1.4, u_warp_amount * 0.8);
    kp = kaleido(kp, order * 0.5);
    // distance to a moving "rune" shape: glowing polygon outline
    float sides = 5.0;
    if(u_shape_speed.x < 1.5) sides = 4.0;
    else if(u_shape_speed.x < 3.5) sides = 6.0;
    else if(u_shape_speed.x < 4.5) sides = 5.0;
    else sides = 8.0;
    float ang = atan(kp.y, kp.x);
    float rr = length(kp);
    float ca = cos(ang/sides), sa = sin(ang/sides);
    vec2 q = vec2(ca*kp.x + sa*kp.y, -sa*kp.x + ca*kp.y);
    float polyR = abs(q.y);
    // pulsing radius
    float pulseR = 0.55 + 0.20 * sin(u_time*1.2) * (0.6 + 0.4 * u_pulse);
    float d = abs(rr - pulseR);
    float ringLine = exp(-pow(d * 40.0, 2.0)) * 1.3;
    float polyLine = exp(-pow(polyR * 8.0, 2.0)) * 0.7;
    intensity = ringLine + polyLine;
    // chromatic rings: stack 3 phases
    vec3 col1 = palette(0.10 + 0.35 * sin(u_time*0.5));
    vec3 col2 = palette(0.55 + 0.30 * cos(u_time*0.4));
    vec3 col3 = palette(0.85 + 0.20 * sin(u_time*0.3 + 1.7));
    baseCol = col1*ringLine + col2*polyLine + col3*0.20*(ringLine+polyLine);
    // hot core
    baseCol += vec3(1.0) * exp(-pow(rr * 6.0, 2.0)) * 0.9;
    return intensity;
}

// ---- movement: lattice / fractal canyon -------------------------
// Recursive rotation+scale structure the camera threads through.
float latticeCanyon(vec2 uv, out vec3 baseCol){
    baseCol = vec3(0.0);
    float intensity = 0.0;
    vec2 p = (uv - 0.5) * 2.4;   /* wider FOV */
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    p.x *= aspect;
    // domain warp
    vec2 wp = warp(p * 1.0, u_warp_amount * 0.5);
    // tunnel-ish z for "threading through"
    float speed = u_shape_speed.y;
    float z = 1.5 + 3.5 * fract(u_time * 0.10 * speed);
    // accumulate 4 octaves of rotated, scaled, warped copies
    float scale = 1.0;
    vec2 q = wp * (1.0 + z*0.3);
    float rot = u_time * u_seg_rot.y * 0.08 + u_seed;
    vec3 accumCol = vec3(0.0);
    float falloff = 1.0;
    for(int i = 0; i < 4; i++){
        q = rot2(q, rot);
        q += vec2(0.42, 0.31);
        scale *= 0.72;
        falloff *= 0.75;
        vec2 fq = fract(q * 1.4) - 0.5;
        float d = min(abs(fq.x), abs(fq.y));
        float line = exp(-pow(d * 32.0 / scale, 2.0)) * scale * falloff;
        float t = float(i)/4.0;
        vec3 c = palette(0.20 + 0.55*t + 0.10*sin(u_time*0.3 + float(i)));
        accumCol += c * line;
        intensity += line * 0.6;
    }
    baseCol = accumCol;
    // central vanishing-point glow
    float r = length(p);
    baseCol += vec3(1.0, 0.9, 1.0) * exp(-pow(r*4.5, 2.0)) * 0.5;
    return intensity;
}

// ---- chromatic aberration on the previous frame -----------------
vec3 chromSample(sampler2D tex, vec2 uv, float amt){
    vec2 d = uv - 0.5;
    float r = texture(tex, uv + d * amt * 1.0).r;
    float g = texture(tex, uv).g;
    float b = texture(tex, uv - d * amt * 1.0).b;
    return vec3(r, g, b);
}

// ---- main --------------------------------------------------------
void main(){
    vec2 uv = gl_FragCoord.xy / u_resolution.xy;
    // velocity-ish modulator: travelling into the journey
    float journeyT = u_time * 0.05;

    // decode phases
    float pT = u_phase_ab.x;        // tunnel
    float pG = u_phase_ab.y;        // grid horizon
    float pS = u_phase_ab.z;        // starfield warp
    float pK = u_phase_ab.w;        // kaleidoscope
    float pL = u_phase_e;           // lattice

    // sum of weights for normalization
    float wsum = pT + pG + pS + pK + pL + 1e-5;
    pT /= wsum; pG /= wsum; pS /= wsum; pK /= wsum; pL /= wsum;

    // base UV (centre-stable; chromatic aberration happens after the mix)
    vec2 uvWarp = uv;
    // sample each movement and accumulate
    vec3 col = vec3(0.0);
    float intensity = 0.0;

    vec3 cTmp; float iTmp;
    iTmp = tunnel(uvWarp, cTmp);          col += cTmp * iTmp * pT;        intensity += iTmp * pT;
    iTmp = gridHorizon(uvWarp, cTmp);     col += cTmp * iTmp * pG;        intensity += iTmp * pG;
    iTmp = starfieldWarp(uvWarp, cTmp);   col += cTmp * iTmp * pS;        intensity += iTmp * pS;
    iTmp = kaleidoBloom(uvWarp, cTmp);    col += cTmp * iTmp * pK;        intensity += iTmp * pK;
    iTmp = latticeCanyon(uvWarp, cTmp);   col += cTmp * iTmp * pL;        intensity += iTmp * pL;

    // tone: darken the average so the hot cores pop and the
    // background stays dark. Trail accumulates via screen blend, so
    // we MUST keep `col` well below 1.0 in the typical case or
    // repeated additions will drive the buffer to white within a few
    // dozen frames.
    col *= 0.7;
    col = pow(col, vec3(1.0 / u_pal_contrast));
    // Soft saturation curve — hot cores clip to white, dark
    // backgrounds stay dark.
    col = col / (1.0 + col * 0.5);

    // feedback mix: the previous frame, chromatically split, blended
    // back at the trail persistence level. The host has already faded
    // the previous frame slightly (u_fade); here we add the new
    // geometry and let the trail decay naturally.
    vec3 prev = chromSample(u_prev, uv, u_ca_amount);
    // Per-channel phosphor decay: blue lingers longer than red and
    // green (real-phosphor behaviour; blue phosphor decay ~30ms vs
    // red ~3ms). The host's fade-copy applies the same weighting,
    // so the *raw* previous-frame buffer uses these multipliers
    // when computing the trail envelope. We do the additional decay
    // here so the optics pass is per-channel in the shader too.
    vec3 phosphorDecay = vec3(0.94, 0.965, 0.985);
    // trail persistence: the faded previous frame is mixed back. Use
    // screen-blend (1 - (1-a)*(1-b)) for additive feel without the
    // pure-additive runaway to white; clamp to keep hot cores intact.
    // The max-based envelope (Astra) prevents unbounded accumulation
    // when persistence is set high.
    vec3 faded = prev * phosphorDecay * u_trail_persist;

    // Everything below that brightens the frame (cabinet glow, habitat
    // tint and density, the reveal, the pulse) is applied to this frame's
    // new light only, never to the trail it is mixed with. The trail is fed
    // back every frame, so a gain applied after the mix multiplied the
    // whole buffer each frame: with a high persistence and a strong pulse
    // the loop gain exceeded 1 and the frame ran up to white within seconds
    // (seen with seed 2724644096, trail 0.92). Now the feedback gain is
    // phosphorDecay * u_trail_persist, always below 1.
    vec3 fresh = col;

    // Cabinet glow pool: a large soft additive that drifts in
    // parallax. Tinted by type (0=warm cyan, 1=magenta, 2=amber).
    float t = u_glow_pools.w;
    vec3 glow_col;
    if(t < 0.5)      glow_col = vec3(0.10, 0.85, 0.95);   // cyan
    else if(t < 1.5) glow_col = vec3(0.95, 0.30, 0.85);   // magenta
    else             glow_col = vec3(0.95, 0.65, 0.20);   // amber
    vec2 d = uv - u_glow_pools.xy;
    float glow = exp(-pow(length(d) / max(u_glow_pools.z, 0.05), 2.0));
    // Add a slow pulse so the pool breathes rather than sits static.
    glow *= 0.55 + 0.20 * sin(u_time * 0.35);
    fresh += glow_col * glow * 0.18;

    // Habitat tint from the state texture: the genome B channel biases
    // the palette so different species drift the colour in different
    // regions. We sample at low resolution — the field is intentionally
    // coarse; the visual layer interpolates softly.
    vec4 st = texture(u_state, uv);
    // Genome drives a slow hue offset that shifts over the whole frame.
    float genomeBias = (st.b - 0.5) * 0.20;
    fresh *= 1.0 + genomeBias * vec3(-0.10, 0.05, 0.12);
    // Density (R channel) brightens where life is active.
    fresh += st.r * vec3(0.20, 0.16, 0.30);

    // Depth reveal moment: a single 4s window per journey when the
    // camera tilts and the field gains a parallax Z. Implemented as
    // a smooth radial zoom + slight hue boost.
    if(u_reveal > 0.001){
        vec2 c = uv - 0.5;
        float zoom = 1.0 + 0.12 * u_reveal;
        vec2 uvR = c * zoom + 0.5;
        vec3 reveal = chromSample(u_prev, uvR, u_ca_amount * 1.6);
        fresh += reveal * 0.35 * u_reveal * u_reveal;
    }

    // pulse modulation: the new light swells with the pulse
    fresh *= 1.0 + 0.18 * u_pulse;

    vec3 mixed = max(fresh, faded);
    // Screen-blend on top so the new light is what we just computed this frame.
    mixed = 1.0 - (1.0 - min(mixed, vec3(1.0))) * (1.0 - min(fresh, vec3(1.0)));

    // brief inversion flash at movement boundaries
    if(u_flash > 0.0){
        vec3 inv = vec3(1.0) - mixed;
        mixed = mix(mixed, inv, u_flash * 0.55);
    }

    fragColor = vec4(mixed, 1.0);
}
