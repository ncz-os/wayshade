// SPDX-License-Identifier: Apache-2.0
// melee.glsl -- every pass of neonspacewar's Melee mode.
//
// The C side (src/nsw_melee_render.c) compiles this file once per pass with
// a PASS_* define after the #version line.
//
// Everything that has an edge is a line segment, drawn twice: once sharp at
// the full output resolution (an analytic capsule with a one-pixel
// antialiased edge), and once as a soft glow into a smaller buffer that is
// added underneath. The glow never touches the edges, so hulls stay crisp
// on any GPU; weak GPUs only get a smaller, cheaper glow buffer.
precision highp float;
precision highp int;

// ---------------------------------------------------------------- instanced segment
#ifdef PASS_VS_LINE
layout(location = 1) in vec4 a_seg;     // x0, y0, x1, y1 in output pixels
layout(location = 2) in vec4 a_col;     // linear rgb, intensity
layout(location = 3) in vec2 a_wid;     // core width (px), glow radius (px)
uniform vec2 u_target;                  // size of the buffer being drawn, pixels
uniform float u_scale;                  // output pixels -> target pixels
uniform float u_glow;                   // 1 for the glow pass
out vec2 v_local;
flat out float v_len;
flat out float v_half;
flat out float v_rad;
flat out vec4 v_col;
void main() {
    vec2 a = a_seg.xy * u_scale, b = a_seg.zw * u_scale;
    vec2 d = b - a;
    float l = length(d);
    vec2 f = l > 1e-4 ? d / l : vec2(1.0, 0.0);
    vec2 s = vec2(-f.y, f.x);
    float half_w = 0.5 * a_wid.x * u_scale;
    float rad = u_glow > 0.5 ? a_wid.y * u_scale * 2.6 + 1.0 : half_w + 1.5;
    int vid = gl_VertexID & 3;
    float along = (vid & 1) == 0 ? -rad : l + rad;
    float across = (vid >> 1) == 0 ? -rad : rad;
    vec2 p = a + f * along + s * across;
    gl_Position = vec4(p / u_target * 2.0 - 1.0, 0.0, 1.0);
    v_local = vec2(along, across);
    v_len = l;
    v_half = half_w;
    v_rad = max(a_wid.y * u_scale, 0.5);
    v_col = a_col;
}
#endif

#ifdef PASS_SHARP
in vec2 v_local;
flat in float v_len;
flat in float v_half;
flat in float v_rad;
flat in vec4 v_col;
out vec4 o_col;
void main() {
    float t = clamp(v_local.x, 0.0, v_len);
    float d = length(vec2(v_local.x - t, v_local.y));
    // A hot, slightly whitened core with a one-pixel antialiased edge.
    float core = 1.0 - smoothstep(v_half - 0.75, v_half + 0.75, d);
    vec3 c = mix(v_col.rgb, vec3(1.0), 0.35 * core * core);
    o_col = vec4(c * core * v_col.a, 0.0);
}
#endif

#ifdef PASS_GLOW
in vec2 v_local;
flat in float v_len;
flat in float v_half;
flat in float v_rad;
flat in vec4 v_col;
out vec4 o_col;
void main() {
    float t = clamp(v_local.x, 0.0, v_len);
    float d = length(vec2(v_local.x - t, v_local.y));
    float g = exp(-(d * d) / (v_rad * v_rad));
    o_col = vec4(v_col.rgb * g * v_col.a * 0.55, 0.0);
}
#endif

// ---------------------------------------------------------------- full screen
#ifdef PASS_VS_FULL
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
#endif

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

// Deep space behind the arena: three layers of stars that move against the
// camera at different rates, and a faint nebula tinted by the matchup.
#ifdef PASS_BG
uniform vec2 u_res;
uniform vec3 u_cam;          // camera centre (world), zoom (pixels per world unit)
uniform float u_rot;         // camera roll
uniform vec3 u_tint_a;
uniform vec3 u_tint_b;
uniform float u_quality;     // 0 weak, 1 full
uniform float u_seed;
out vec4 o_col;
vec3 stars(vec2 p, float scale, float density, float seed) {
    vec2 g = p * scale;
    vec2 cell = floor(g), f = fract(g) - 0.5;
    float h = hash12(cell + seed);
    if (h > density) return vec3(0.0);
    vec2 o = vec2(hash12(cell + seed + 7.1), hash12(cell + seed + 3.3)) - 0.5;
    float d = length(f - o * 0.7);
    float b = pow(hash12(cell + seed + 1.9), 3.0);
    vec3 c = mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.85, 0.7), hash12(cell + seed + 5.5));
    float px = scale / max(u_cam.z, 1e-3) * u_res.y / 1080.0;  // star edge ~1.5 px at 1080p, same apparent size at 4K
    return c * b * (1.0 - smoothstep(0.02, 0.02 + 1.5 * px, d)) * 1.2;
}
void main() {
    vec2 q = (gl_FragCoord.xy - 0.5 * u_res) / max(u_cam.z, 1e-3);
    float cr = cos(u_rot), sr = sin(u_rot);
    q = vec2(cr * q.x - sr * q.y, sr * q.x + cr * q.y);
    vec3 col = vec3(0.0);
    // Parallax: far layers move less with the camera.
    col += stars(q * 0.25 + u_cam.xy * 0.05, 26.0, 0.10, u_seed) * 0.5;
    col += stars(q * 0.5 + u_cam.xy * 0.15, 14.0, 0.07, u_seed + 11.0) * 0.8;
    if (u_quality > 0.5) col += stars(q + u_cam.xy * 0.4, 8.0, 0.05, u_seed + 23.0);
    vec2 np = q * 0.35 + u_cam.xy * 0.1;
    float n = vnoise(np * 2.0) * 0.6 + vnoise(np * 4.3 + 7.0) * 0.4;
    if (u_quality > 0.5) n = n * 0.75 + vnoise(np * 9.0 + 3.0) * 0.25;
    col += mix(u_tint_a, u_tint_b, vnoise(np * 1.3 + 17.0)) * smoothstep(0.45, 0.9, n) * 0.10;
    o_col = vec4(col, 1.0);
}
#endif

// Add the glow buffer under the sharp lines.
#ifdef PASS_COMPOSITE
uniform sampler2D u_glowtex;
uniform vec2 u_res;
uniform float u_gain;        // glow strength option (0-2)
out vec4 o_col;
void main() {
    vec3 g = texture(u_glowtex, gl_FragCoord.xy / u_res).rgb;
    o_col = vec4(g * u_gain, 0.0);
}
#endif
