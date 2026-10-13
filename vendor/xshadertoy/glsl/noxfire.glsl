// Title:  Nox Fire
// Author: Matt Vianueva <diatribes@gmail.com>
// URL:    https://www.shadertoy.com/view/wfG3Dz
// Date:   24-May-2025
// Desc:   Nox Fire

// Relicensed as MIT License, by permission, 8-Mar-2026.

/*

    -4 from FabriceNeyret2
    
    Thanks :D !

*/

/* ncz port fix (0.8.1): the tunnel radius `4. + sin(t*.7)*4.` oscillates
 * through zero, so the tunnel periodically pinches shut and the frame goes
 * black.  The unbounded march then lets `d` overflow to +Inf, after which
 * both the `o*o/d` normalization and the large-argument `sin()` in the
 * noise loop collapse the whole frame to NaN/black -- and because the
 * precision of `sin()` on large arguments differs by vendor, the black
 * frame lands at different times on Intel/Mesa (frame 4) and Apple/Mesa
 * (t=25/60).  Fix: offset the radius so it never reaches zero, bound the
 * march (and clamp `d`), reduce every noise angle modulo TAU so `sin()`
 * always sees a small argument, and retune the depth normalization and
 * exposure to the warm fire palette.  Mean luminance now stays in a
 * fire-lit band (>= 0.08 at t = 4 s and t = 30 s) instead of dropping to
 * 0.0005. */
void mainImage(out vec4 o, vec2 u) {
    o = vec4(0,0,0,0);
    float i=0., d=0., s=0., n=0., t=iTime;
    vec3 p = iResolution;
    u = (u-p.xy/2.)/p.y;
    for(o*=i; i++<1e2; ) {
        // march, p = ro + rd * d, p.z += t*4;
        p = vec3(u * d, d + t*4.);
        // turbulence
        p += cos(p.z+t+p.yzx*.5)*.6;
        // modulate tunnel radius; offset so it never pinches shut
        s = 5.+sin(t*.7)*2.-length(p.xy);
        // rotate
        p.xy *= mat2(cos(t+vec4(0,33,11,0)));
        // noise loop, argument reduced to [0, TAU)
        for (n = 1.6; n < 32.; n += n )
            // subtract noise from tunnel dist
            s -= abs(dot(sin(mod(p.z + t + p*n, 6.2831853)), vec3(1.12))) / n;
        // accumulate distance, capped so p stays bounded and d stays finite
        d += s = .01 + abs(s)*.1;
        if (d > 60.) { d = 60.; break; }
        // grayscale color
        o += 1. / s;
    }
    // warm fire tone map; divide by depth for the tunnel falloff
    o = tanh(vec4(6.0,1.6,0.5,1.0) * o * o / (d + 25.0) / 4e6);
}


/* Original

void mainImage(out vec4 o, vec2 u) {
    float i, d, s, n, f, t=iTime;
    vec3 p = iResolution;
    u = (u-p.xy/2.)/p.y;
    for(o*=i; i++<1e2; ) {
        // march, p = ro + rd * d, p.z += t*4;
        p = vec3(u * d, d + t*4.);
        // turbulence
        p += cos(p.z+t+p.yzx*.5)*.6;
        // modulate tunnel radius
        s = 4.+sin(t*.7)*4.-length(p.xy);
        // rotate
        p.xy *= mat2(cos(t+vec4(0,33,11,0)));
        // noise loop
        for (n = .1; n < 2.;
            // subtract noise from tunnel dist
            s -= abs(dot(sin(p.z+t+p * n * 16.), vec3( .07))) / n,
            // grow noise
            n += n);
        // accumulate distance
        d += s = .01 + abs(s)*.1;
        // grayscale color
        o += 1. / s;
    }
    // o*o to increase saturation,
    // divide by d for depth
    // colorize
    o = tanh(vec4(5,2,1,1) * o * o / d / 2e6);
}

*/
