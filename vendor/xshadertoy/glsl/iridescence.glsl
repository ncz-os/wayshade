// SPDX-License-Identifier: Apache-2.0
// Title:  Iridescence
// Desc:   Overlapping translucent soap bubbles drifting across frame, their
//         surfaces showing thin-film interference colour -- the shifting
//         rainbow banding real soap films produce, where hue depends on
//         film thickness and viewing angle.  Bubbles overlap and blend
//         rather than merely occlude.
//
//         Original work.  No reference to any external shader source.
//         The physics is genuinely published and well-known:
//
//           Thin-film interference: constructive/destructive interference
//           between the front- and back-surface reflections of light off
//           a film of thickness d and refractive index n.  For light of
//           wavelength lambda and incidence angle theta (Snell-derived
//           inner angle theta_in), the phase difference is
//             Delta_phi = (4 * pi * n * d * cos(theta_in)) / lambda
//           plus pi for the higher-n reflection.  Resulting intensity
//           I(lambda) = sin^2(Delta_phi / 2).  See e.g. Hecht, "Optics"
//           4th ed., Chapter 9; or any standard thin-film reference.
//
//           CIE RGB approximation: we sample the phase at three visible
//           wavelengths (R=650nm, G=550nm, B=450nm), compute intensity
//           at each, then map to RGB.  This is the cheap approximation
//           used in many production shaders (see e.g. Inigo Quilez's
//           "Thin film interference" article, 2017).
//
//           Fresnel rim: Schlick's approximation
//             F = F0 + (1 - F0) * (1 - cos(theta))^5
//           with F0 ~ 0.04 (air-to-soap).  The rim is brighter than the
//           centre.  Schlick 1994, "An inexpensive BRDF model for
//           physically-based rendering".
//
//         Per-run variation driven entirely by iSeed: bubble count,
//         size distribution, film-thickness range, drift direction and
//         speed.  Two runs at the same frame index differ in the set
//         of bubbles and their colour bands, not merely in animation
//         phase.

#define PI 3.14159265359

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Hash and small helpers.                                            */
/* ----------------------------------------------------------------- */

float hash11(float n) {
    return fract(sin(n * 12.9898) * 43758.5453);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

/* ----------------------------------------------------------------- */
/* Thin-film interference colour.                                      */
/*                                                                   */
/* Inputs:                                                             */
/*   d     -- film thickness in nanometres (typical soap: 50-1000 nm). */
/*   cos_t -- cos(theta_in), the cosine of the angle inside the film. */
/*           Ranges 0..1; for normal incidence cos_t=1.               */
/* Output: RGB triple, each channel in [0,1].                          */
/*                                                                   */
/* We sample the phase at three visible wavelengths, take sin^2 to get  */
/* intensity at each, and emit as RGB.  This is the cheap-but-correct */
/* spectral approximation.                                            */
/* ----------------------------------------------------------------- */

vec3 thin_film_rgb(float d_nm, float cos_t) {
    /* Refractive index of soap film ~ 1.33 (water-based). */
    const float n = 1.33;
    /* Visible wavelengths in nm. */
    const float LR = 650.0;
    const float LG = 550.0;
    const float LB = 450.0;

    /* Phase difference at each wavelength. */
    float phi_r = (4.0 * PI * n * d_nm * cos_t) / LR + PI;
    float phi_g = (4.0 * PI * n * d_nm * cos_t) / LG + PI;
    float phi_b = (4.0 * PI * n * d_nm * cos_t) / LB + PI;

    /* sin^2(phi/2) gives intensity in [0,1]. */
    float r = 0.5 - 0.5 * cos(phi_r);
    float g = 0.5 - 0.5 * cos(phi_g);
    float b = 0.5 - 0.5 * cos(phi_b);

    return vec3(r, g, b);
}

/* Fresnel-Schlick reflectance at normal-incidence mix.  cos_theta is
 * the cosine of the angle between view and surface normal (here, the
 * angle between the bubble's surface normal at this pixel and the
 * direction to the camera).  F0 is the reflectance at normal incidence
 * (air-to-soap ~ 0.025). */
float fresnel_schlick(float cos_theta, float f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

/* ----------------------------------------------------------------- */
/* A single bubble's contribution.  Centre at (cx, cy), radius r,     */
/* thickness profile d(r/d_to_centre), returns an RGBA premultiplied  */
/* contribution that the caller blends into the frame.               */
/* ----------------------------------------------------------------- */

vec4 bubble(vec2 px, vec2 c, float r, float base_thick,
            vec2 drift, float t) {
    vec2 d2 = px - c;
    float dist = length(d2);
    if (dist > r) return vec4(0.0);

    /* Normalised distance from the centre, 0 at centre, 1 at edge. */
    float u = dist / r;

    /* Real soap-film thickness profile (gravity-drained): the film
     * is THICKEST at the top and THINNEST at the bottom of the
     * bubble (drainage), and we use this to drive the colour bands.
     * For a head-on 2D view, gravity has no natural direction, so
     * we model the film as a smooth radial profile: thickest at
     * the centre, thinning toward the rim.  A small wobble adds
     * natural swirl that drifts over time. */
    /* Angular wobble around the bubble (kept subtle). */
    float ang = atan(d2.y, d2.x);
    float wobble = 0.06 * sin(ang * 3.0 + t * 0.3);
    /* Thickness profile (nm).  Base value with a soft radial falloff
     * (parabolic) so the film is thickest at the centre, thinner at
     * the rim.  The resulting phase shifts along u produce visible
     * concentric colour bands. */
    float thickness_profile = base_thick *
        (0.55 + 0.85 * (1.0 - u) * (1.0 - u) + wobble);

    /* The view angle inside the film, approximated by a function of
     * u.  At the rim the path through the film is longest so cos_t
     * is small.  At the centre we look normal to the film. */
    float cos_t = saturate(0.97 - u * 0.85);

    /* Colour: thin-film RGB.  For very thin films (under ~80 nm)
     * intensity is near zero across all wavelengths -- we boost the
     * contrast so the colour reads. */
    vec3 film_col = thin_film_rgb(thickness_profile, cos_t);
    float sat_boost = smoothstep(150.0, 400.0, thickness_profile) * 0.4 + 0.7;
    film_col *= sat_boost;

    /* A black-border effect near the very rim: the film is so thin
     * there that it's essentially transparent (no significant
     * interference).  We darken the colour in a thin annulus. */
    float thin_factor = smoothstep(60.0, 140.0, thickness_profile);
    film_col *= thin_factor;

    /* Fresnel rim highlight: only at the very edge (the film surface
     * at grazing angles reflects strongly).  Schlick's approximation
     * for the bubble's outer surface. */
    float cos_theta = saturate(1.0 - u * 0.95);
    float f = fresnel_schlick(cos_theta, 0.03);
    /* Use the thin-film colour tinted by the Fresnel reflectance so
     * the rim picks up the colour of the interference, not pure white. */
    vec3 rim_col = film_col * 1.5 + vec3(0.6, 0.7, 0.85) * 0.3;
    vec3 col = mix(film_col, rim_col, f * pow(u, 2.0) * 1.3);

    /* A small specular highlight near the top of each bubble to read
     * as 3D. */
    vec2 highlight_pos = c + vec2(0.0, -r * 0.45);
    float hl = exp(-length(px - highlight_pos) * 6.0 / r);
    col += vec3(0.7, 0.78, 0.85) * hl * 0.5;

    /* Alpha: ramp down only at the very rim so overlapping bubbles
     * blend rather than occlude. */
    float alpha = 1.0 - smoothstep(0.93, 1.00, u);

    return vec4(col * alpha, alpha);
}

/* ----------------------------------------------------------------- */
/* mainImage.                                                          */
/* ----------------------------------------------------------------- */

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* --- per-run variation --- */
    /* Bubble count: 6 to 14 visible bubbles. */
    int nbubbles = int(mix(6.0, 14.0, fract(iSeed.x * 1.7)) + 0.5);
    /* Size range: smallest vs largest bubble radius in screen units. */
    float size_min = mix(0.07, 0.13, fract(iSeed.y * 2.1));
    float size_max = mix(0.18, 0.32, fract(iSeed.y * 2.1 + 0.5));
    /* Base film thickness in nm.  Real soap films range 30-1000 nm;
     * we go 60-450 so we see vivid colour bands. */
    float thickness_lo = mix(60.0, 200.0, fract(iSeed.z * 1.3));
    float thickness_hi = mix(280.0, 450.0, fract(iSeed.z * 1.3 + 0.7));
    /* Drift speed and direction. */
    float drift_speed = mix(0.04, 0.10, fract(iSeed.w * 1.7));
    float drift_angle = mix(-0.6, 0.6, fract(iSeed.w * 2.1));
    /* Background tint: pale, slightly saturated, so the iridescent
     * bubble colours stand out. */
    vec3 bg_top = mix(vec3(0.20, 0.18, 0.28),
                      vec3(0.10, 0.16, 0.20),
                      fract(iSeed.x + iSeed.w));
    vec3 bg_bot = mix(vec3(0.05, 0.06, 0.10),
                      vec3(0.20, 0.12, 0.18),
                      fract(iSeed.y + 0.3));

    /* --- screen coords --- */
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;

    /* --- background gradient --- */
    float v = saturate(uv.y * 0.5 + 0.5);
    vec3 bg = mix(bg_bot, bg_top, v);

    /* --- bubbles (alpha composite over background) --- */
    vec3 col = bg;

    /* We render up to 16 bubbles in fixed order.  The number of
     * active bubbles is nbubbles; positions are derived from a hash of
     * the bubble index, offset by drift. */
    float t = iTime * drift_speed;
    vec2 drift = vec2(cos(drift_angle), sin(drift_angle)) * t;

    /* Wrap-around: each bubble orbits in a slow loop so the field is
     * infinite-ish but cheap to compute.  We use a periodic "lane"
     * coordinate per bubble. */
    for (int i = 0; i < 16; i++) {
        if (i >= nbubbles) break;
        float fi = float(i);
        /* Per-bubble size. */
        float h = hash11(fi * 7.1 + 13.0);
        float r = mix(size_min, size_max, h);
        /* Initial position.  Use a hash on the bubble index. */
        vec2 home = vec2(
            hash11(fi * 3.7 + 1.3) * 2.0 - 1.0,
            hash11(fi * 5.3 + 9.1) * 1.5 - 0.5
        );
        /* Per-bubble drift: a slightly different angle and speed
         * (parallax). */
        float b_angle = drift_angle + mix(-0.3, 0.3, hash11(fi * 11.7));
        float b_speed = drift_speed * mix(0.6, 1.3, hash11(fi * 17.3));
        vec2 b_drift = vec2(cos(b_angle), sin(b_angle)) * iTime * b_speed;
        /* Final position: home + drift, with a small bobbing motion. */
        vec2 c = home * vec2(0.95, 0.7) + b_drift +
                 vec2(sin(t * 1.3 + fi), cos(t * 1.1 + fi * 1.7)) * 0.02;
        /* Per-bubble base thickness. */
        float b_thick = mix(thickness_lo, thickness_hi,
                            hash11(fi * 21.7 + 5.0));
        /* Per-bubble drift vector for highlight (reusing b_drift). */
        vec4 bub = bubble(uv, c, r, b_thick, b_drift, iTime);
        /* Alpha-blend over current colour. */
        float a = bub.w;
        col = mix(col, bub.rgb, a);
    }

    /* --- gentle tonemap & gamma --- */
    col = max(col, 0.0);
    col = col / (1.0 + col * 0.6);
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}
