/* SPDX-License-Identifier: Apache-2.0 */
/* test_leviathan_randomize.c - many random (seed, option) draws of leviathan
 * render non-flat and stay inside the gate's safety caps.
 *
 * usage: test_leviathan_randomize SHADER.glsl [DRAWS] [QUALITY 0|1|2]
 *
 * Renders the shader headless (surfaceless EGL) with LV_OPTIONS defined and
 * random values for every option uniform, at arc times in each act. Checks:
 *   - every frame outside the quiet room has luma spread (not a flat frame);
 *   - inside the gate, the colour fields and the coda (arc 276-352 s) no
 *     channel exceeds 0.85 (+ 2 LSB of dither) and the mean luma is <= 0.50.
 * Exit 0 pass, 1 fail, 77 skip (no EGL/GLES device, or a software renderer:
 * a CPU rendering proves nothing about the GPU path and is never accepted).
 */
#define _POSIX_C_SOURCE 200809L
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 192
#define H 108

static uint32_t rng = 0x12345678u;
static float frand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 16777216.f;
}

static char *slurp(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[n] = 0;
    return b;
}

static GLuint sh(GLenum t, const char *s) {
    GLuint o = glCreateShader(t);
    glShaderSource(o, 1, &s, NULL);
    glCompileShader(o);
    GLint ok = 0;
    glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(o, sizeof log, NULL, log);
        fprintf(stderr, "compile failed:\n%s\n", log);
        return 0;
    }
    return o;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s SHADER.glsl [DRAWS]\n", argv[0]); return 2; }
    int draws = argc > 2 ? atoi(argv[2]) : 24;
    setenv("EGL_PLATFORM", "surfaceless", 1);
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { printf("SKIP: no EGL display\n"); return 77; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint nc = 0;
    if (!eglChooseConfig(d, ca, &cfg, 1, &nc) || nc < 1) { printf("SKIP: no GLES3 config\n"); return 77; }
    EGLint pa[] = {EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pa);
    EGLint xa[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
    if (s == EGL_NO_SURFACE || c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c)) {
        printf("SKIP: no GLES3 context\n");
        return 77;
    }
    const char *ren = (const char *)glGetString(GL_RENDERER);
    if (!ren || strstr(ren, "llvmpipe") || strstr(ren, "softpipe") || strstr(ren, "SwiftShader")) {
        printf("SKIP: software renderer (%s)\n", ren ? ren : "?");
        return 77;
    }
    printf("renderer: %s\n", ren);

    char *body = slurp(argv[1]);
    if (!body) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const char *head =
        "#define LV_OPTIONS 1\n"
        "precision highp float;\nprecision highp int;\nout vec4 frag_color;\n"
        "uniform vec3 iResolution;\nuniform float iTime;\nuniform float iTimeDelta;\n"
        "uniform float iFrameRate;\nuniform int iFrame;\nuniform vec4 iDate;\nuniform vec4 iMouse;\n"
        "uniform vec4 iSeed;\n";
    const char *tail = "\nvoid main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); frag_color = c; }\n";
    int qual = argc > 3 ? atoi(argv[3]) : 0;
    char ver[64];
    snprintf(ver, sizeof ver, "#version 300 es\n#define NCZ_QUALITY %d\n", qual < 0 || qual > 2 ? 0 : qual);
    size_t n = strlen(ver) + strlen(head) + strlen(body) + strlen(tail) + 1;
    char *src = malloc(n);
    if (!src) { fprintf(stderr, "out of memory\n"); return 1; }
    snprintf(src, n, "%s%s%s%s", ver, head, body, tail);
    GLuint vs = sh(GL_VERTEX_SHADER, "#version 300 es\nlayout(location=0) in vec2 p;\nvoid main(){gl_Position=vec4(p,0.0,1.0);}\n");
    GLuint fs = sh(GL_FRAGMENT_SHADER, src);
    if (!vs || !fs) return 1;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "link failed\n"); return 1; }
    glUseProgram(prog);
    static const float q[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glViewport(0, 0, W, H);
    glUniform3f(glGetUniformLocation(prog, "iResolution"), W, H, 1);
    /* every uniform the draws set must exist, or the draws silently test defaults */
    static const char *need[] = {"iTime", "iSeed", "uLv0", "uLv1", "uLv2"};
    for (unsigned i = 0; i < sizeof need / sizeof need[0]; i++)
        if (glGetUniformLocation(prog, need[i]) < 0) { fprintf(stderr, "uniform %s missing\n", need[i]); return 1; }

    /* arc times: silhouette, hull, interior, gate, colour fields, coda, quiet */
    static const float at[] = {20, 90, 200, 290, 320, 345, 375};
    static const int nopt[8] = {4, 3, 6, 3, 6, 3, 2, 0}; /* choices incl. auto */
    unsigned char px[W * H * 4];
    int fails = 0;
    for (int k = 0; k < draws; k++) {
        float lv0[4], lv1[4], lv2[4], seed[4];
        for (int i = 0; i < 4; i++) seed[i] = frand();
        for (int i = 0; i < 4; i++) lv0[i] = floorf(frand() * nopt[i]) - 1.f;
        lv1[0] = floorf(frand() * nopt[4]) - 1.f;
        lv1[1] = floorf(frand() * nopt[5]) - 1.f;
        lv1[2] = floorf(frand() * 2.f);
        lv1[3] = frand() < 0.5f ? 0.f : 380.f + 520.f * frand();
        lv2[0] = 0.7f + 1.3f * frand();
        lv2[1] = frand() < 0.3f ? -9.f : 6.2831853f * frand();
        lv2[2] = 0.3f + 0.7f * frand();
        lv2[3] = 0.25f + 1.75f * frand();
        glUniform4fv(glGetUniformLocation(prog, "iSeed"), 1, seed);
        glUniform4fv(glGetUniformLocation(prog, "uLv0"), 1, lv0);
        glUniform4fv(glGetUniformLocation(prog, "uLv1"), 1, lv1);
        glUniform4fv(glGetUniformLocation(prog, "uLv2"), 1, lv2);
        for (unsigned j = 0; j < sizeof at / sizeof at[0]; j++) {
            /* The shader's arc time is iTime / arc-scale (uLv2.x, clamped to
             * [0.7, 2]; lv2[0] is drawn inside that range), so rendering at
             * iTime = at[j] * lv2[0] shows exactly arc time at[j]: the act
             * predicates below are keyed on at[j] on purpose. */
            float t = at[j] * lv2[0];
            glUniform1f(glGetUniformLocation(prog, "iTime"), t);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
            double sum = 0, sum2 = 0;
            int maxc = 0;
            for (int i = 0; i < W * H; i++) {
                const unsigned char *p = px + 4 * i;
                double l = (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) / 255.0;
                sum += l;
                sum2 += l * l;
                for (int ch = 0; ch < 3; ch++) if (p[ch] > maxc) maxc = p[ch];
            }
            double mean = sum / (W * H);
            double sd = sqrt(fmax(sum2 / (W * H) - mean * mean, 0.0));
            int gate = at[j] >= 276 && at[j] <= 352;
            const char *why = NULL;
            if (at[j] < 360 && sd < 0.012) why = "flat frame";
            if (gate && maxc > 217 + 2) why = "gate peak above 0.85";
            if (gate && mean > 0.50) why = "gate mean above 0.50";
            if (why) {
                fails++;
                printf("FAIL draw %d arc %.0f s: %s (mean %.3f sd %.3f max %d) lv0=%g,%g,%g,%g lv1=%g,%g,%g,%g lv2=%g,%g,%g,%g\n",
                       k, at[j], why, mean, sd, maxc, lv0[0], lv0[1], lv0[2], lv0[3], lv1[0], lv1[1], lv1[2], lv1[3],
                       lv2[0], lv2[1], lv2[2], lv2[3]);
            }
        }
    }
    printf("%d draws x %u arc times: %d failures\n", draws, (unsigned)(sizeof at / sizeof at[0]), fails);
    return fails ? 1 : 0;
}
