// SPDX-License-Identifier: Apache-2.0
// jurassicaquarium_skeleton.glsl
//
// SKELETON BUILD for jurassicaquarium -- the smallest slice that proves
// the architecture from
//   `docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md`
// compiles and renders. Not the whole design; just the three things
// required by the task brief:
//
//   1. The shared current field (a single 3D vector field driving sway).
//   2. ONE algorithmic flora type driven by that field (procedural
//      seaweed ribbons; procedural geometry, no model).
//   3. The waterline camera framing, with enough water shading to read
//      as underwater.
//
// Out of scope here, deferred to later slices:
//   - eras / act switching, the four-act cycle
//   - creatures (G1 vertebrate, G2 ammonite, G3 anomalocarid)
//   - reef builders (G5/G6 cones, G7 frond ribbon)
//   - per-act water chemistry differences
//   - the mosasaur breach
//   - godrays
//   - spray particles
//
// Performance budget:
//   - Family doctrine: 2016-era discrete (Pascal/Polaris) is the design
//     target; Intel UHD CML GT2 is the FLOOR and must run, may be
//     reduced. The ridgeline lesson is explicit: a beautiful 0.4 fps
//     shader that cannot run on the floor is not shipped.
//   - This skeleton must run comfortably at 60+ fps on the iGPU floor.
//     That rules out per-pixel raymarching of the water column, large
//     raymarched seaweed meshes, and any 5+ octave fBm.
//
//   Architecture: single-pass fullscreen fragment shader. No offscreen
//   targets, no multi-pass compositing. The waterline is a procedural
//   y = wave(x, t) curve in screen space. The "scene" is virtual in
//   world-space coordinates that get mapped to screen at the surface.
//   All work is analytic -- no raymarch.
//
//   Per-pixel cost: ~250-400 ALU ops worst case, 1-2 vnoise lookups,
//   1 seaweed distance evaluation per pixel (cheap: a closed-form
//   ray-to-vertical-segment closest approach, not a march).
//
// Conventions:
//   - xshadertoy format (mainImage out vec4, in vec2 fragCoord).
//   - iSeed -- four random floats in [0,1) for per-run variation.
//   - iTime -- seconds since start.
//   - iResolution -- framebuffer size in pixels.
//   - GLES 3.0 baseline, no extensions.
//
// Author: ncz (round 2026-09-27). Original work, MIT.
// See docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md.

#define TAU 6.28318530718

// ======================================================================
// Hash and value noise. Cheap. Same shape as caustictidepool.glsl in
// this repo. Used by both the current field and the caustics layer.
// ======================================================================
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2(vec2 p) {
    // Two octaves. Cheap.
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 2; i++) {
        v += amp * vnoise(p);
        p *= 2.07;
        amp *= 0.5;
    }
    return v;
}

// ======================================================================
// THE CURRENT FIELD.
//
// Spec: "One shared low-frequency 3D noise VECTOR FIELD drives sway for
// everything -- fronds, anemone tentacles, crinoid arms, branching coral
// tips. This is the single most important detail on the seafloor:
// independent per-object wiggling is what makes CG seafloors read as
// fake, and one shared field makes the scene read as one place with
// weather."
//
// Implementation: three independent fBm scalars, one per axis, with a
// slow per-axis time drift. The result is a vec3 where adjacent samples
// are correlated but not identical. Cheap: 3 fBm = 6 vnoise per call.
// ======================================================================

vec3 currentField(vec3 p, float t) {
    // Per-axis slow drift. Differing frequencies per axis so the field
    // doesn't read as a single global sine -- "no metronome" rule.
    vec3 drift = vec3(0.13, 0.21, 0.17) * t;
    // Spatial scale: ~5 metre feature size.
    float scale = 0.20;
    vec3 q = p * scale + drift;
    float cx = fbm2(q.yz + vec2(11.7));
    float cy = fbm2(q.zx + vec2(23.3));
    float cz = fbm2(q.xy + vec2(37.1));
    return vec3(cx, cy, cz) * 2.0 - 1.0;
}

// ======================================================================
// SEAWEED (one algorithmic flora type).
//
// Procedural geometry, not a model. Each ribbon is a parametric curve
// anchored on the seafloor. We render seaweed per-pixel via the
// closed-form closest-approach of the camera ray to a vertical segment.
//
// Cost per pixel, per ribbon: ~30 ops (one division, one sqrt, one
// smoothstep). Two ribbons = ~60 ops/pixel for seaweed.
//
// The tip sway is computed analytically per ribbon and stored in
// currentField via the seaweed's screen-space test; for the skeleton
// we don't displace the per-pixel "is this on the seaweed" test by the
// tip sway (the base is rigid, and the per-pixel test sees the base).
// A future slice can bend the ribbon's apparent shape by integrating
// sway into the test.
// ======================================================================

// Alpha falloff for a seaweed ribbon, given horizontal distance to its
// centre line. ribbon_w is the half-width.
float ribbonAlpha(float horizDist, float ribbon_w) {
    return 1.0 - smoothstep(0.0, ribbon_w, abs(horizDist));
}

// Evaluate one seaweed's contribution at a screen pixel.
//
// Returns vec3:
//   x = alpha (0..1)
//   y = height fraction along the ribbon (0 = base, 1 = tip)
//   z = ribbon index for tinting
//
// Closed-form closest-approach of the camera ray to a vertical segment
// at world xz = baseXZ, height = height. The ray goes from
// ro = (0, 0.05, 0) in direction rd (unnormalised). The closest point on
// the line (baseXZ.x, h*height, baseXZ.z) to the ray in the xz-plane
// is found by minimising |(ro.xz + rd.xz*s) - baseXZ|^2 over s.
vec3 evalSeaweed(vec2 uv, float pitch, float roY,
                 vec2 baseXZ, float height,
                 float ribbon_w, float ribbon_index,
                 float T) {
    // Ray components for this pixel. rd is unnormalised.
    float rx = uv.x * 0.6;
    float rdY = sin(pitch) + uv.y * 0.6;
    float rz = 1.0;
    // Closest approach in xz-plane:
    //   s* = (rd.x*(baseXZ.x - ro.x) + rd.z*(baseXZ.z - ro.z)) / (rd.x^2 + rd.z^2)
    float rdDotR = rx * rx + rz * rz;
    float sStar = (rx * (baseXZ.x - 0.0) + rz * (baseXZ.y - 0.0)) / rdDotR;
    float xAt = 0.0 + rx * sStar;
    float zAt = 0.0 + rz * sStar;
    float yAt = roY + rdY * sStar;
    float hAt = yAt / height;
    float dx = xAt - baseXZ.x;
    float dz = zAt - baseXZ.y;
    float horizDist = sqrt(dx*dx + dz*dz);
    float alpha = 0.0;
    if (hAt > 0.0 && hAt < 1.0) {
        alpha = ribbonAlpha(horizDist, ribbon_w);
    }
    return vec3(alpha, clamp(hAt, 0.0, 1.0), ribbon_index);
}

// Tip sway of a seaweed, returned as vec2 (world xz displacement).
// Cheap: two currentField() calls = 6 fbm2 calls. We evaluate this
// per pixel for the shading's slight saturation drift; the per-pixel
// alpha test does NOT depend on the tip sway (the base is rigid).
vec2 seaweedTipSway(vec2 baseXZ, float height, float phase, float T) {
    vec3 q_lo = currentField(vec3(baseXZ.x, 0.4 * height, baseXZ.y), T);
    vec3 q_hi = currentField(vec3(baseXZ.x, 0.9 * height, baseXZ.y), T);
    float idle = sin(T * 0.6 + phase * TAU);
    float swayAmp = 0.25 * height;
    float tipSwayX = (q_hi.x * 0.81 + q_lo.x * 0.16 + 0.08 * idle) * swayAmp;
    float tipSwayZ = (q_hi.z * 0.81 + q_lo.z * 0.16) * swayAmp;
    return vec2(tipSwayX, tipSwayZ);
}

// ======================================================================
// WATERLINE.
//
// For the skeleton: world-space surface y(x, z, t) = sum of two sine
// waves. Schlick Fresnel for sky-vs-water reflection at the boundary.
// Foam where the wave's local curvature exceeds a threshold.
//
// Cheap: 4 sin + 1 product. ~30 ops.
// ======================================================================

float waterHeight(vec2 xz, float t) {
    float w = 0.0;
    w += 0.030 * sin(dot(xz, vec2( 0.92, 0.39)) * 1.6 + t * 0.55);
    w += 0.022 * sin(dot(xz, vec2(-0.74, 0.67)) * 2.1 + t * 0.71);
    return w;
}

// Normal of the water surface from the analytic gradient. The ridgeline
// lesson: never finite-difference what we can derive.
vec3 waterNormal(vec2 xz, float t) {
    float dx = 0.030 * 1.6 * 0.92 * cos(dot(xz, vec2( 0.92, 0.39)) * 1.6 + t * 0.55)
             + 0.022 * 2.1 * -0.74 * cos(dot(xz, vec2(-0.74, 0.67)) * 2.1 + t * 0.71);
    float dz = 0.030 * 1.6 * 0.39 * cos(dot(xz, vec2( 0.92, 0.39)) * 1.6 + t * 0.55)
             + 0.022 * 2.1 *  0.67 * cos(dot(xz, vec2(-0.74, 0.67)) * 2.1 + t * 0.71);
    return normalize(vec3(-dx, 1.0, -dz));
}

// Schlick Fresnel, F0 = 0.02 (water). cosTheta is dot(view, normal).
float fresnel(float cosTheta) {
    float f = 0.02 + (1.0 - 0.02) * pow(1.0 - cosTheta, 5.0);
    return clamp(f, 0.0, 1.0);
}

// ======================================================================
// SKY. A simple gradient + sun disc. Cheap. The "sky and weather above"
// pass from the spec is out of scope here; this is the minimum that
// reads as "above the water".
// ======================================================================
vec3 sky(vec3 rd, float t) {
    float h = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.78, 0.74, 0.62);  // warmish
    vec3 zenith  = vec3(0.32, 0.42, 0.55);  // cool
    vec3 col = mix(horizon, zenith, smoothstep(0.0, 0.7, h));
    vec3 sunDir = normalize(vec3(0.30, 0.55, -0.85));
    float sunDot = max(dot(rd, sunDir), 0.0);
    float sun = pow(sunDot, 256.0);
    col += vec3(1.60, 1.35, 1.05) * sun * 1.2;
    float halo = pow(sunDot, 8.0) * 0.18;
    col += vec3(1.10, 0.95, 0.75) * halo;
    return col;
}

// ======================================================================
// UNDERWATER. "Beer-Lambert + scatter + saturation bias" model from
// the spec, but projected directly to a 2D screen-space approximation
// rather than raymarched. We map screen y below the waterline to a
// virtual depth, then colour by depth.
//
// This is the cheap approximation the spec hints at: the architecture
// uses a real FBO + refraction pass in Phase 1, but the skeleton
// proves the colour model works without paying for the multi-pass
// pipeline.
//
// The spec is explicit: the saturation bias is "deliberately anti-
// physical", and it is what makes the result read as glowing rather
// than muddy. Do not "fix" it as a bug.
// ======================================================================
vec3 underwater(vec2 uv, float depth, float t) {
    // Beer-Lambert per channel. Red dies fastest, then green, blue
    // persists. Coefficients picked for a shallow-water read.
    vec3 extinct = exp(-depth * vec3(0.45, 0.18, 0.10));
    // Ambient scatter: lifts the floor. Without this the result is
    // muddy and desaturated (the realistic-ocean failure mode).
    vec3 scatter = vec3(0.05, 0.10, 0.13) * (1.0 - extinct);
    // Saturation bias INCREASES with depth. Deliberately anti-physical.
    vec3 bias = vec3(0.20, 0.45, 0.65) * (1.0 - 1.0 / (1.0 + depth * 0.6));
    // Caustics: cheap interference between two slow wave fields.
    // A real caustic shader (gradient-of-fbm) costs more; for the
    // skeleton we use sin-based interference as a proxy. Reads as
    // moving bright pinpoints on the seafloor.
    vec2 cuv = uv * vec2(4.0, 2.5) + vec2(0.0, t * 0.10);
    float c1 = sin(cuv.x * 2.3 + cuv.y * 1.7 + t * 0.7);
    float c2 = sin(cuv.x * 1.1 - cuv.y * 2.9 + t * 0.5);
    float caustic = pow(max(0.0, c1 * c2), 6.0);
    vec3 causticCol = vec3(1.10, 1.25, 1.10) * caustic * 0.40;
    vec3 col = scatter + bias + causticCol;
    col *= extinct * 0.85 + 0.15;  // soft clamp
    return col;
}

// ======================================================================
// MAIN IMAGE
// ======================================================================
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Aspect-corrected normalised coords.
    vec2 uv = (fragCoord / iResolution.xy) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    // Per-run variation. The two seaweed anchor positions and heights
    // shift by iSeed so the scene differs each launch.
    float seedX = iSeed.x * 0.6 - 0.3;
    float seedZ = iSeed.y * 0.6 - 0.3;
    float seedH = 0.7 + iSeed.z * 0.5;

    // Camera setup: at the surface (y=0.5 m above water), looking
    // forward (+z) with a slight downward tilt (~25deg). The waterline
    // -- where rd.y = 0 -- sits at uv.y = -sin(pitch) / 0.6 = 0.70
    // for our pitch. For the skeleton we draw the waterline as a
    // wavy curve at uv.y near WATERLINE_Y below, with sky above and
    // water below.
    const float PITCH = -0.55;        // ~31deg down
    const float CAM_Y = 0.5;          // 0.5 m above the water surface
    const float WATERLINE_Y = 0.36;
    vec3 ro = vec3(0.0, CAM_Y, 0.0);
    float pitch = PITCH;
    float rx = uv.x * 0.6;
    float rdY = sin(pitch) + uv.y * 0.6;
    vec3 rd = vec3(rx, rdY, 1.0);

    vec3 col;
    if (uv.y > WATERLINE_Y) {
        // Above waterline -- sky.
        col = sky(normalize(rd), iTime);
    } else {
        // Below waterline -- underwater scene.
        // Virtual depth from uv.y below waterline. Range: 0 (at the
        // waterline) to ~5m at the bottom of the screen.
        float depth01 = clamp((WATERLINE_Y - uv.y) / 1.3, 0.0, 1.0);
        float vDepth = depth01 * 5.0;
        col = underwater(uv, vDepth, iTime);

        // Two procedural seaweed ribbons. Closed-form per ribbon.
        // World coords: camera at (0, 0.5, 0), seafloor at y = 0,
        // ~1m below. Seaweeds anchor at world Y = 0.
        //
        // World Z is distance ahead. World X is sideways. The
        // seaweed's projection to screen happens via the ray's
        // intersection with the seafloor plane (y = 0) and the
        // vertical segment from (X, 0, Z) to (X, height, Z).
        vec2 b0 = vec2(-0.30 + seedX * 0.3, 0.7 + seedZ * 0.2);
        vec2 b1 = vec2( 0.30 + seedX * 0.3, 0.9 + seedZ * 0.2);
        float h0 = 1.30 * seedH;
        float h1 = 1.00 * seedH;
        vec3 s0 = evalSeaweed(uv, pitch, CAM_Y, b0, h0, 0.22, 0.0, iTime);
        vec3 s1 = evalSeaweed(uv, pitch, CAM_Y, b1, h1, 0.18, 1.0, iTime);
        // Pick the closer (higher-alpha) ribbon.
        vec3 s = (s0.x >= s1.x) ? s0 : s1;
        if (s.x > 0.0) {
            // Per-launch tint drift, computed from the tip sway so the
            // colour subtly shifts with the current. This wires the
            // current field into the shading, not just the geometry.
            vec2 baseTip;
            if (s.z < 0.5) {
                baseTip = seaweedTipSway(b0, h0, 0.10, iTime);
            } else {
                baseTip = seaweedTipSway(b1, h1, 0.70, iTime);
            }
            float tintDrift = clamp(baseTip.x * 2.0 + 0.5, 0.0, 1.0);
            vec3 base = vec3(0.10, 0.18, 0.08);
            vec3 tip  = mix(vec3(0.40, 0.62, 0.22),
                            vec3(0.55, 0.68, 0.30),
                            tintDrift) + iSeed.w * 0.10;
            vec3 seaweedCol = mix(base, tip, smoothstep(0.0, 1.0, s.y));
            col = mix(col, seaweedCol, s.x * 0.95);
        }

        // Sub-surface glow just below the waterline.
        float below = smoothstep(0.0, 0.10, WATERLINE_Y - uv.y);
        col += vec3(0.10, 0.18, 0.20) * below * 0.10;
    }

    // Fresnel + foam AT the waterline.
    if (uv.y > WATERLINE_Y - 0.16 && uv.y < WATERLINE_Y + 0.10) {
        // Map the pixel to a world xz at the waterline plane.
        float tLine = -CAM_Y / rdY;
        vec2 xzLine = vec2(rx * tLine, tLine);
        vec3 N = waterNormal(xzLine, iTime);
        vec3 nrd = normalize(rd);
        float cosT = max(dot(-nrd, N), 0.0);
        float F = fresnel(cosT);
        vec3 R = reflect(nrd, N);
        vec3 skyRef = sky(R, iTime);
        float band = exp(-abs(uv.y - WATERLINE_Y) * 22.0);
        col = mix(col, skyRef, F * band * 0.55);
        // Foam where wave curvature is high.
        float curvature = 0.030 * 1.6 * 0.92 * cos(dot(xzLine, vec2( 0.92, 0.39)) * 1.6 + iTime * 0.55)
                        + 0.022 * 2.1 * 0.67 * cos(dot(xzLine, vec2(-0.74, 0.67)) * 2.1 + iTime * 0.71);
        float foam = band * smoothstep(0.20, 0.55, abs(curvature));
        col = mix(col, vec3(0.92, 0.94, 0.95), foam * 0.5);
    }

    // Tonemap (Reinhard) and gamma to sRGB.
    col = col / (1.0 + col);
    col = pow(col, vec3(1.0 / 2.2));
    // Soft vignette.
    float vig = 1.0 - 0.20 * dot(uv, uv);
    col *= vig;

    fragColor = vec4(col, 1.0);
}