/* mobius.frag — race along a twisted Moebius ribbon against the void.
 *
 * The camera rides just above the strip looking slightly down, with a slow roll
 * about the forward axis. The half twist flips the palette gradient after each
 * full loop, the Moebius signature. The void stays black, but the ribbon casts
 * a soft glow into it from the ray's closest approach, so a frame where the
 * ribbon crosses one corner is no longer mostly empty.
 *
 * Adapted from hyprsaver's shaders/mobius.frag (MIT License, Copyright (c) 2026 Mara
 * Vexa; licence text in docs/fractasia/HYPRSAVER-LICENSE). Changes: the
 * Shadertoy-style uniforms are replaced by the fractint preamble, colours come
 * from fract_palette() through a mirrored wrap, --speed scales time (1 at the
 * default speed, the standalone saver's own pace), and start phases come from
 * u_params so every cycle differs.
 *
 * Layout of u_params (vec4):
 *   x = starting position along the ribbon (radians).
 *   y = starting roll angle (radians).
 *   z, w = unused.
 */

vec3 pal_wrap(float x) { return fract_palette(abs(fract(x) * 2.0 - 1.0)); }

const int   MAX_STEPS      = 48;
const float MAX_DIST       = 20.0;
const float HIT_EPS        = 0.001;
const float MIN_STEP       = 0.001;
const float R              = 1.5;    // major radius of ring
const float W              = 0.3;    // ribbon half-width
const float THICKNESS      = 0.018;  // SDF ribbon thickness (half)
const float SPEED          = 1.2;    // radians / sec camera advance (3× v2)
const float ELEVATION      = 0.3;    // off-surface elevation (in rolled_up direction)
const float DIP_ANGLE      = 0.15;   // radians downward tilt toward strip (~8.6°)
const float ROLL_SPEED     = 0.1;    // rad/sec slow camera roll around forward axis
const float TAU            = 6.283185307;
const float BANDS_PER_LOOP = 28.0;  // close passes of the ribbon showed two or three broad smooth stripes at 16

// Signed distance to the ribbon, in the (dr, dz) half-plane at angle theta.
float mobiusSDF(vec3 p) {
    float rxy   = max(length(p.xy), 0.001);
    float theta = atan(p.y, p.x);
    float ch    = cos(theta * 0.5);
    float sh    = sin(theta * 0.5);
    float dr    = rxy - R;
    // Project (dr, p.z) onto ribbon axes
    float v     =  dr * ch + p.z * sh;   // along ribbon width  in [-W, W]
    float perp  = -dr * sh + p.z * ch;   // through ribbon thickness
    float v_ex  = max(abs(v) - W, 0.0);  // excess past edge (0 inside width)
    return sqrt(perp * perp + v_ex * v_ex) - THICKNESS;
}

void main() {
    float spd = u_speed * (1.0 / 0.3);
    float u_cam = u_time * spd * SPEED + u_params.x;

    float ch_cam = cos(u_cam * 0.5);
    float sh_cam = sin(u_cam * 0.5);
    float cu     = cos(u_cam);
    float su     = sin(u_cam);

    vec3 strip_pos = vec3(R * cu, R * su, 0.0);
    vec3 wdir_cam  = vec3(ch_cam * cu, ch_cam * su,  sh_cam);
    vec3 snorm_cam = vec3(cu * sh_cam, su * sh_cam, -ch_cam);
    vec3 tangent   = vec3(-su, cu, 0.0);

    float roll_rad = u_time * spd * ROLL_SPEED + u_params.y;
    vec3 rolled_up = cos(roll_rad) * snorm_cam + sin(roll_rad) * wdir_cam;

    vec3 ro          = strip_pos + ELEVATION * snorm_cam;
    vec3 cam_forward = normalize(tangent - DIP_ANGLE * snorm_cam);

    vec3 right   = normalize(cross(cam_forward, rolled_up));
    vec3 cam_up  = normalize(cross(right, cam_forward));

    vec2 uv = (gl_FragCoord.xy - 0.5 * u_resolution.xy) / u_resolution.y;
    vec3 rd = normalize(uv.x * right + uv.y * cam_up + cam_forward);

    float t = 0.02;
    vec3  p = ro + t * rd;
    bool  hit = false;
    float glow = 0.0;   /* accumulated closeness to the ribbon along the ray */
    for (int i = 0; i < MAX_STEPS; i++) {
        float d = mobiusSDF(p);
        glow += 0.02 / (0.02 + d * d * 4.0);
        if (abs(d) < HIT_EPS) { hit = true; break; }
        if (t > MAX_DIST) break;
        float step_len = max(abs(d), MIN_STEP);
        t += step_len;
        p += step_len * rd;
    }

    /* Void glow: palette colour of the nearest band, dim and distance-faded. */
    vec3 col = pal_wrap(u_cam / TAU * BANDS_PER_LOOP * 0.5 + 0.25) * min(glow, 1.0) * 0.28;
    if (hit) {
        float rxy_h = max(length(p.xy), 0.001);
        float th_h  = atan(p.y, p.x);
        float v_hit = (rxy_h - R) * cos(th_h * 0.5) + p.z * sin(th_h * 0.5);

        float raw = th_h / TAU * BANDS_PER_LOOP + 0.5;
        col = pal_wrap(raw * 0.5);
        float edge = 1.0 - smoothstep(0.85, 1.0, abs(v_hit) / W);
        col *= 0.75 + 0.25 * edge;
    }
    col = fract_fx_post(col * u_exposure, gl_FragCoord.xy);
    frag_color = vec4(clamp(col, 0.0, 1.0), 1.0);
}
