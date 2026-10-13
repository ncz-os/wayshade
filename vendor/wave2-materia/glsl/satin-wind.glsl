// SPDX-License-Identifier: Apache-2.0
// satin-wind.glsl - hanging satin moved by a slow wind.
//
// What it is: an analytic cloth-like height field with its exact gradient, lit
// with a thread-direction (Kajiya-Kay) highlight. Not a cloth simulation: the
// folds are chosen, not solved, but every quantity is a closed-form function of
// position and (director) time, so the normals are exact and nothing shimmers.
//   - Folds hang vertically. Their spacing is asymmetric: fold phase runs on a
//     warped coordinate s = x + a sin(b x + c) (a b < 1 keeps it monotonic), so
//     folds bunch on one side and open on the other; c drifts over minutes.
//   - Tension waves travel down the cloth: a traveling envelope 1 + T sin(k y -
//     phase) swells and flattens the folds as it passes, and a traveling bend
//     shifts the fold phase, so neighboring heights lag each other.
//   - Fold groups swell and relax over minutes: a broad group envelope whose
//     phase and depth come from the director.
//   - A weak transverse wrinkle field crosses the folds at a shallow angle.
//   - h(x, y) = E(y) G(x) sum A_i sin(k_i s + bend(y) + phase_i) + wrinkles; the
//     gradient is differentiated by hand (product and chain rules), no finite
//     differences.
//   - Shading in linear light: wrapped diffuse between the palette's ground and
//     body, deep folds toward the shadow role, and a restrained anisotropic sheen
//     along the warp threads (two Kajiya-Kay lobes, sharp and broad).
//
// Cost: 3 fold terms + 1 wrinkle term (sin/cos only) + 1 gradient-noise
// evaluation for the tonal drift: L on every tier.
//
// Original work (wave2-materia 2026-09-26, rebuilt 2026-09-29 by LEAD 27), MIT.

//@saver id=satin-wind version=2 family=light+textile director=85
//@palette-native bg=#a28b6e body=#dcc9ab accent=#f8efe0 accent2=#6d5a45
//@option name=folds type=float default=1 min=0.6 max=1.6 rand=0.8:1.25 group=Cloth
//@+ label="Fold count" desc="How many folds cross the frame (relative)."
//@option name=billow type=float default=0.6 min=0.2 max=1 rand=0.45:0.8 group=Cloth
//@+ label="Billow" desc="Depth of the folds."
//@option name=asymmetry type=float default=0.55 min=0 max=1 rand=0.35:0.8 group=Cloth
//@+ label="Asymmetry" desc="How unevenly the folds are spaced: bunched on one side, open on the other."
//@option name=tension type=float default=0.45 min=0 max=1 rand=0.3:0.65 group=Wind
//@+ label="Tension waves" desc="Strength of the waves that travel down the cloth, swelling and flattening the folds as they pass."
//@option name=wind type=float default=1 min=0 max=2 rand=0.75:1.25 group=Wind
//@+ label="Wind" desc="Speed of the traveling waves and the fold sway. 0 = still air; the director still reshapes the fold groups over minutes."
//@option name=wrinkles type=float default=0.3 min=0 max=1 rand=0.15:0.45 group=Cloth
//@+ label="Wrinkles" desc="Strength of the weak transverse wrinkles crossing the folds."
//@option name=sheen type=float default=0.55 min=0 max=1 rand=0.4:0.7 group=Look
//@+ label="Sheen" desc="Strength of the satin highlight along the threads."

#include "xst_common.glsl"

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = xst_uv(fragCoord);
    float x = uv.x, y = uv.y;
    float W = OPT_WIND;

    // --- asymmetric fold coordinate s(x) and ds/dx
    float fk = 1.9 * OPT_FOLDS;
    float ab = 0.75 * OPT_ASYMMETRY;                  // a*b < 1: monotonic
    float b = 1.3 * fk, a = ab / b;
    float c = XST_TAU * iSeed.x + 0.025 * DIR_PH4;
    float s = x + a * sin(b * x + c);
    float s_x = 1.0 + a * b * cos(b * x + c);

    // --- tension wave envelope E(y) traveling down (y decreasing is down)
    float ky = 2.2;
    float phE = 0.30 * DIR_PH0 * W;
    float Tt = 0.5 * OPT_TENSION;
    float E = 1.0 + Tt * sin(ky * y + phE);
    float E_y = Tt * ky * cos(ky * y + phE);

    // --- traveling bend of the fold phase
    float kb = 1.6;
    float phB = 0.22 * DIR_PH1 * W + 2.0;
    float Bm = 0.55 * OPT_TENSION + 0.15;
    float bend = Bm * sin(kb * y + phB);
    float bend_y = Bm * kb * cos(kb * y + phB);

    // --- fold groups swell and relax over minutes (director)
    float gk = 0.55 * fk;
    float gph = XST_TAU * iSeed.y + 0.02 * DIR_PH2;
    float g = 0.25 + 0.35 * DIR_P0;
    float G = 1.0 + g * sin(gk * s + gph);
    float G_x = g * gk * cos(gk * s + gph) * s_x;

    // --- fold sum and its partial derivatives
    const vec3 kr = vec3(1.0, 1.73, 2.61);
    const vec3 ar = vec3(0.50, 0.24, 0.11);
    float S = 0.0, Sc_k = 0.0, Sc = 0.0;
    for (int i = 0; i < 3; i++) {
        float k = 3.4 * fk * kr[i];
        float ph = 0.12 * float(i + 1) * DIR_PH3 * W + XST_TAU * fract(iSeed.z * (1.7 + float(i)));
        float th = k * s + bend * (1.0 + 0.4 * float(i)) + ph;
        float sn = sin(th), cs = cos(th);
        S += ar[i] * sn;
        Sc_k += ar[i] * cs * k;                    // d/ds
        Sc += ar[i] * cs * (1.0 + 0.4 * float(i)); // d/dbend
    }
    float amp = OPT_BILLOW * (0.85 + 0.3 * DIR_P1);
    float h = amp * E * G * S;
    float hx = amp * E * (G_x * S + G * Sc_k * s_x);
    float hy = amp * (E_y * G * S + E * G * Sc * bend_y);

    // --- weak transverse wrinkles at a shallow angle
    float ka = 11.0, al = 0.35 + 0.2 * (DIR_P2 - 0.5);
    float wph = ka * (y * cos(al) + x * sin(al)) + 0.4 * DIR_PH5 * W;
    float wa = 0.035 * OPT_WRINKLES * (0.6 + 0.4 * sin(0.8 * s + 1.3));
    h += wa * sin(wph);
    hx += wa * ka * sin(al) * cos(wph);
    hy += wa * ka * cos(al) * cos(wph);

    // --- shading
    vec3 N = normalize(vec3(-hx * 0.20, -hy * 0.20, 1.0));
    vec3 L = normalize(vec3(-0.75 + 0.3 * (DIR_P3 - 0.5), 0.35, 0.55));
    vec3 V = vec3(0.0, 0.0, 1.0);
    float wrap = 0.15;
    float diff = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float tone = xst_noise(uv * 0.5 + 3.0);        // broad, still tonal drift of the dye
    vec3 base = mix(PAL_ACCENT2, PAL_BODY, clamp(0.15 + 0.95 * diff + 0.06 * tone, 0.0, 1.0));
    base = mix(base, PAL_BG, 0.25);
    float valley = smoothstep(0.1, -0.9, h / max(amp, 1e-3));
    base = mix(base, PAL_ACCENT2, valley * 0.25);

    // Kajiya-Kay along the weft threads (across the folds), two lobes: the
    // tangent tilts with the fold slope, so the sheen rides the crests
    vec3 T0 = vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(T0 - N * dot(N, T0));
    vec3 H = normalize(L + V);
    float th2 = dot(T, H);
    float sinTH = sqrt(max(1.0 - th2 * th2, 0.0));
    float spec = 0.30 * pow(sinTH, 60.0) + 0.16 * pow(sinTH, 10.0);
    spec *= smoothstep(0.0, 0.25, dot(N, L));
    vec3 col = base + OPT_SHEEN * spec * mix(PAL_ACCENT, vec3(1.0), 0.3);

    col *= 1.0 - 0.14 * dot(uv * vec2(0.55, 0.9), uv * vec2(0.55, 0.9));
    fragColor = xst_output(col, fragCoord);
}
