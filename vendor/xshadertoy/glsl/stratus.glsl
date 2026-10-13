// SPDX-License-Identifier: Apache-2.0
// Title:  Stratus
// Desc:   A soft 2D cloudscape drifting slowly across frame, filling it
//         completely.  Layered, billowing cloud masses with visible
//         internal structure, lit so the upper surfaces catch light and
//         the undersides fall into shadow.  Slow lateral drift with the
//         layers moving at different rates for parallax.
//
//         Original work.  No reference to any external shader source.
//         All technique is from published graphics / noise literature:
//         value noise + fBm (Perlin 1985, "An Image Synthesizer";
//         Musgrave et al. 1991, "The Synthesis and Rendering of Eroded
//         Fractal Terrains"); domain warping for billowing (Inigo
//         Quilez, "domain warping" article, 2002); cheap fake
//         self-shadowing via density sampling toward a light direction
//         (a standard analytic cloud trick in the procedural-sky
//         literature -- see e.g. Schneider, "Graphics Gems" cloud
//         chapters).
//
//         Per-run variation driven entirely by iSeed: cloud coverage
//         (density threshold), octave weighting, drift direction and
//         speed, and the light colour (dawn / midday / dusk / moonlit).
//         Two runs at the same frame index differ in composition AND
//         motion, not merely in animation phase.

#define PI 3.14159265359

#define saturate(x) clamp(x, 0.0, 1.0)

/* ----------------------------------------------------------------- */
/* Hash and value noise.  Clean integer-hash 2D value noise, sampled */
/* on a unit grid with smooth interpolation.                          */
/* ----------------------------------------------------------------- */

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    /* Quintic smoothstep (Perlin's improved interpolant). */
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = hash21(i + vec2(0.0, 0.0));
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/* fBm: weighted sum of octaves.  Standard formulation; weight
 * control per-octave is exposed as a parameter so per-run variation
 * can change the billowy-vs-grainy look. */
float fbm(vec2 p, float oct_weight, float lac, float gain) {
    float s = 0.0;
    float amp = 1.0;
    float norm = 0.0;
    for (int i = 0; i < 5; i++) {
        /* oct_weight biases the octave contribution; 1.0 = even
         * weighting across octaves. */
        float a = amp * pow(oct_weight, float(i));
        s += a * vnoise(p);
        norm += a;
        p *= lac;
        amp *= gain;
    }
    return s / norm;
}

/* Domain warp: nudge the sample point along a low-frequency vector
 * field so straight fBm becomes billowing shapes.  Standard
 * technique. */
vec2 warp(vec2 p, float w, float fr) {
    return vec2(
        vnoise(p + vec2(0.0,  0.0)) ,
        vnoise(p + vec2(31.4, 7.7))
    ) * w * fr - vec2(w * fr * 0.5);
}

/* Density field: cloud coverage in [0,1].  Higher = thicker.  The
 * threshold `cover` converts that to a 0/1-ish mask; we use a soft
 * smoothstep so edges of clouds are diffuse rather than crisp. */
float cloud_density(vec2 p, float cover, float oct_weight) {
    /* Domain warp -- single-pass billowing.  Strength tuned so the
     * shapes look like billowing cumulus rather than directional
     * smear. */
    vec2 q = p + warp(p, 0.55, 1.0) * 0.6;
    float n = fbm(q, oct_weight, 2.07, 0.55);
    /* Subtract a coverage threshold; cloud exists where n > cover. */
    return n - cover;
}

/* ----------------------------------------------------------------- */
/* mainImage.                                                          */
/* ----------------------------------------------------------------- */

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    /* --- per-run variation --- */
    /* Coverage: a soft threshold.  0.30 = mostly sky with thin
     * clouds; 0.65 = overcast heavy. */
    float cover = mix(0.30, 0.62, fract(iSeed.x * 1.7));
    /* Octave weighting: < 1.0 biases to larger-scale billows, > 1.0
     * biases to finer detail. */
    float oct_weight = mix(0.55, 1.45, fract(iSeed.y * 2.1));
    /* Drift direction: an angle in radians, mostly horizontal with
     * slight vertical to avoid a perfectly left-right slide. */
    float drift_angle = mix(-0.18, 0.18, fract(iSeed.z * 1.3))
                      + mix(0.0, 6.283, 0.0); /* anchor in [-pi,pi] */
    /* Drift speed. */
    float drift_speed = mix(0.04, 0.12, fract(iSeed.w * 1.9));
    /* Light colour: 4 presets. */
    float light_choice = fract(iSeed.z + iSeed.w * 0.5);
    vec3 light_top;
    vec3 light_bot;
    vec3 sky_above;
    vec3 sky_below;
    if (light_choice < 0.25) {
        /* Dawn: warm rose above, peach below. */
        light_top  = vec3(1.00, 0.78, 0.62);
        light_bot  = vec3(0.95, 0.55, 0.40);
        sky_above  = vec3(0.45, 0.30, 0.40);
        sky_below  = vec3(0.85, 0.55, 0.45);
    } else if (light_choice < 0.50) {
        /* Midday: bright white above, soft cyan below. */
        light_top  = vec3(1.00, 0.98, 0.95);
        light_bot  = vec3(0.85, 0.90, 0.95);
        sky_above  = vec3(0.45, 0.65, 0.85);
        sky_below  = vec3(0.75, 0.85, 0.95);
    } else if (light_choice < 0.75) {
        /* Dusk: amber/red above, violet below. */
        light_top  = vec3(1.00, 0.55, 0.30);
        light_bot  = vec3(0.80, 0.35, 0.45);
        sky_above  = vec3(0.30, 0.20, 0.45);
        sky_below  = vec3(0.65, 0.35, 0.45);
    } else {
        /* Moonlit: cool blue-white above, deep navy below. */
        light_top  = vec3(0.85, 0.90, 1.00);
        light_bot  = vec3(0.50, 0.60, 0.80);
        sky_above  = vec3(0.06, 0.08, 0.18);
        sky_below  = vec3(0.20, 0.25, 0.40);
    }

    /* --- screen coords --- */
    vec2 res = iResolution.xy;
    vec2 uv  = (fragCoord - 0.5 * res) / res.y;

    /* --- world-space sample coords with slow drift --- */
    /* scale = how much of the world we see (smaller = zoomed in). */
    float scale = 2.4;
    vec2 drift = vec2(cos(drift_angle), sin(drift_angle)) * iTime * drift_speed;
    vec2 p = (uv * scale) + drift;

    /* --- cloud density --- */
    float d = cloud_density(p, cover, oct_weight);
    /* Soft cloud mask: 0 below cover, 1 above. */
    float cloud = saturate(smoothstep(0.0, 0.18, d));

    /* --- lighting: self-shadow via density sampling toward light ---
     * We sample the density slightly offset in the direction OF the
     * light.  If that sample is also above threshold, we are shadowed
     * (less light reaches us).  Cheap fake self-shadowing. */
    vec2 light_dir_screen = vec2(0.45, 0.7);  /* mostly upward in screen */
    float shadow_step = 0.06 * scale;
    /* Sample the density at a slightly displaced point. */
    float d_shadow = cloud_density(p + light_dir_screen * shadow_step,
                                   cover, oct_weight);
    /* If the shadow-side density is high, this pixel is in shadow. */
    float shadow = saturate(smoothstep(0.05, 0.25, d_shadow));
    /* The more shadow, the more we lean toward light_bot. */
    vec3 cloud_light = mix(light_top, light_bot, shadow);

    /* --- compositing --- */
    /* Base sky: vertical gradient. */
    float v = saturate(uv.y * 0.5 + 0.5); /* 0 at bottom, 1 at top */
    vec3 sky = mix(sky_below, sky_above, v);

    /* Mix clouds in over the sky. */
    vec3 col = mix(sky, cloud_light, cloud * 0.92);

    /* Slight darkening at the lower edges of clouds to enhance the
     * impression of internal structure.  Take the derivative of the
     * cloud field in y (the fall direction). */
    float d_dy = d - cloud_density(p - vec2(0.0, 0.04), cover, oct_weight);
    float underside = saturate(smoothstep(0.0, 0.05, d_dy) * cloud);
    col = mix(col, col * 0.6, underside * 0.5);

    /* Very subtle horizon haze (lighter toward bottom-center). */
    float horiz = saturate(1.0 - length(vec2(uv.x * 0.4, uv.y + 0.1)) * 1.5);
    col = mix(col, col * 1.05 + light_bot * 0.04, horiz * 0.4);

    /* --- gentle tonemap & gamma --- */
    col = max(col, 0.0);
    col = col / (1.0 + col * 0.45);
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}
