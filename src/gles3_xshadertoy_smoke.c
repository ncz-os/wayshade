/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_xshadertoy_smoke.c — HEADLESS smoke tester for the
 *                             xshadertoy-format shaders under
 *                             vendor/xshadertoy/glsl/.
 *
 * Patterned after gles3_transitions_smoke.c: opens a surfaceless
 * EGL context (no Wayland compositor required), compiles the
 * shader using the xshadertoy preamble from src/gles3_xshadertoy.c,
 * renders multiple frames at fixed iTime values, and dumps each as
 * a real PNG via libpng. The output answers three questions per
 * shader:
 *
 *   1. Does it compile on this GPU + driver + EGL implementation?
 *   2. Does it link?
 *   3. Does it render non-black pixels at the requested iTimes?
 *
 * This is the validation path used for new shader additions during
 * evidence capture — the production driver needs a Wayland session
 * and a compositor, neither of which are always present on a
 * headless dev box.
 *
 * Usage:
 *   gles3_xshadertoy_smoke <shader.glsl> <out-dir> [t1 t2 t3 ...]
 *
 * If no times are given, captures at 2.0 / 10.0 / 25.0 seconds.
 * The output PNGs are named <basename>_t<time>.png and a one-line
 * summary is printed to stderr.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <png.h>

/* ---------- xshadertoy preamble (mirrors src/gles3_xshadertoy.c) ---------- */

static const char *frag_preamble =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "uniform vec3  iResolution;\n"
    "uniform float iTime;\n"
    "uniform float iTimeDelta;\n"
    "uniform float iFrameRate;\n"
    "uniform int   iFrame;\n"
    "uniform vec4  iDate;\n"
    "uniform vec4  iMouse;\n"
    "\n"
    "uniform vec3  iChannelResolution[4];\n"
    "uniform float iChannelTime[4];\n"
    "\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n"
    "uniform vec4 iSeed;\n";

static const char *frag_tail =
    "\nvoid main() {\n"
    "  vec4 col = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  mainImage(col, gl_FragCoord.xy);\n"
    /* Must mirror src/xst_core.c::XST_TAIL: the production wrapper forces
     * opaque output because several vendored ports leave .a at 0 and a
     * layer-shell surface with alpha 0 composites to nothing. */
    "  frag_color = vec4(col.rgb, 1.0);\n"
    "}\n";

/* ---------- shader-body preprocessing ---------- */

static const char *
lskip(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
static const char *leol(const char *p) {
    while (*p && *p != '\n') p++;
    return p;
}

/* Strip the leading contiguous block of `#version`, `#extension`,
 * `#pragma`, `precision`, blank and //-comment lines. Matches
 * src/gles3_xshadertoy.c::skip_leading_directives. */
static const char *
skip_leading_directives(const char *src) {
    const char *p = src;
    while (*p) {
        const char *line_start = p;
        const char *q = lskip(line_start);
        if (*q == '\n') { p = q + 1; continue; }
        if (*q == '\0') return p;
        if (*q == '/' && q[1] == '/') { p = leol(q); continue; }
        if (*q != '#') return p;
        static const char *kd[] = {
            "version", "extension", "pragma", "precision", NULL
        };
        int matched = 0;
        for (int i = 0; kd[i]; i++) {
            size_t L = strlen(kd[i]);
            if (strncmp(q + 1, kd[i], L) == 0 &&
                (q[L + 1] == ' ' || q[L + 1] == '\t' ||
                 q[L + 1] == '\n')) {
                p = leol(q);
                matched = 1;
                break;
            }
        }
        if (!matched) return p;
    }
    return p;
}

/* ---------- file I/O ---------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "smoke: cannot open %s: %s\n",
                path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static int write_png_rgba(const char *path,
                          const unsigned char *rgba,
                          unsigned int w, unsigned int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[png] cannot open %s: %s\n",
                path, strerror(errno));
        return -1;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) { fclose(fp); return -1; }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    if (!rows) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }
    for (unsigned int y = 0; y < h; y++) {
        rows[y] = (png_bytep)(rgba + (size_t)y * w * 4);
    }
    png_write_image(png, rows);
    png_write_end(png, NULL);
    free(rows);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}

/* ---------- EGL setup ---------- */

static EGLDisplay egl_disp;
static EGLContext  egl_ctx;
static EGLSurface  egl_pbuf;
static GLuint      program;
static GLuint      vao;
static GLuint      vbo;
static GLint       loc_iResolution;
static GLint       loc_iTime;
static GLint       loc_iTimeDelta;
static GLint       loc_iFrameRate;
static GLint       loc_iFrame;
static GLint       loc_iDate;
static GLint       loc_iMouse;
static GLint       loc_iSeed;
static GLint       loc_iChannelRes[4];
static GLint       loc_iChannelTime[4];

/* iSeed: parsed from --seed=N (repeatable). The defaults below give
 * a deterministic run if no override is supplied. With overrides,
 * frame k uses g_seed_overrides[k % g_n_seed_overrides] so a single
 * invocation can probe multiple compositions. */
static float g_seed_overrides[16][4];
static int   g_n_seed_overrides = 0;
static GLuint      dummy_tex;

/* glGetError guard.  A persistent GL error is a latent bug that masks real
 * ones, so the smoke render fails loudly instead of passing silently.  The
 * name is set by main() for the per-frame diagnostics. */
static const char *g_basename = "shader";
static int g_gl_errors = 0;

/* Drain every queued error.  GL keeps only one error code at a time, so a
 * loop is not strictly needed, but it makes the count unambiguous if a
 * driver ever changes that. */
static void smoke_check_gl(float t) {
    GLenum e;
    int guard = 0;
    while ((e = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "[gl] %s t=%.1f GL_ERROR=0x%x\n",
                g_basename, t, (unsigned)e);
        g_gl_errors++;
        if (++guard >= 16) break;
    }
}

static int dst_w = 320, dst_h = 180;

static int
init_egl(void) {
    setenv("EGL_PLATFORM", "surfaceless", 1);
    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        fprintf(stderr, "smoke: eglGetDisplay failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_disp, &major, &minor)) {
        fprintf(stderr, "smoke: eglInitialize failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    fprintf(stderr, "[diag] EGL %d.%d vendor=%s\n", major, minor,
            eglQueryString(egl_disp, EGL_VENDOR));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "smoke: eglBindAPI failed\n");
        return -1;
    }
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfg = 0;
    if (!eglChooseConfig(egl_disp, cfg_attr, &cfg, 1, &num_cfg) ||
        num_cfg < 1) {
        fprintf(stderr, "smoke: eglChooseConfig failed\n");
        return -1;
    }
    EGLint pbuf_attr[] = {
        EGL_WIDTH, dst_w, EGL_HEIGHT, dst_h, EGL_NONE
    };
    egl_pbuf = eglCreatePbufferSurface(egl_disp, cfg, pbuf_attr);
    if (egl_pbuf == EGL_NO_SURFACE) {
        fprintf(stderr, "smoke: eglCreatePbufferSurface failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE
    };
    egl_ctx = eglCreateContext(egl_disp, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "smoke: eglCreateContext failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(egl_disp, egl_pbuf, egl_pbuf, egl_ctx)) {
        fprintf(stderr, "smoke: eglMakeCurrent failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    fprintf(stderr, "[diag] GL_VERSION=%s\n",
            (const char *)glGetString(GL_VERSION));
    fprintf(stderr, "[diag] GL_RENDERER=%s\n",
            (const char *)glGetString(GL_RENDERER));
    return 0;
}

/* ---------- shader compile / link ---------- */

static char *
build_frag_src(const char *body) {
    size_t plen = strlen(frag_preamble);
    const char *body_start = skip_leading_directives(body);
    size_t blen = strlen(body_start);
    size_t tlen = strlen(frag_tail);
    char *out = (char *)malloc(plen + blen + tlen + 4);
    if (!out) return NULL;
    memcpy(out, frag_preamble, plen);
    out[plen] = '\n';
    memcpy(out + plen + 1, body_start, blen);
    memcpy(out + plen + 1 + blen, frag_tail, tlen + 1);
    return out;
}

static GLuint compile_shader(GLenum stage, const char *src,
                             char *log_out, size_t log_cap) {
    GLuint s = glCreateShader(stage);
    if (!s) {
        snprintf(log_out, log_cap, "glCreateShader failed");
        return 0;
    }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        GLsizei got = 0;
        glGetShaderInfoLog(s, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8), log[0] ? log : "no info log");
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs,
                           char *log_out, size_t log_cap) {
    GLuint p = glCreateProgram();
    if (!p) {
        snprintf(log_out, log_cap, "glCreateProgram failed");
        return 0;
    }
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        GLsizei got = 0;
        glGetProgramInfoLog(p, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8), log[0] ? log : "no info log");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ---------- one fullscreen quad ---------- */

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static int setup_fullscreen_quad(void) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    static const float verts[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    /* 1x1 dummy texture for iChannel0..3. */
    glGenTextures(1, &dummy_tex);
    glBindTexture(GL_TEXTURE_2D, dummy_tex);
    static const unsigned char black[4] = {0, 0, 0, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

/* ---------- render one frame at a given iTime ---------- */

static int render_frame(int frame_idx, float t, const char *out_png,
                        double *out_coverage, double *out_red,
                        double *out_green, double *out_blue,
                        double *out_alpha,
                        double *out_brightness) {
    int frame = (int)t;
    glUseProgram(program);

    glUniform3f(loc_iResolution, (float)dst_w, (float)dst_h, 1.0f);
    glUniform1f(loc_iTime, t);
    glUniform1f(loc_iTimeDelta, 1.0f / 60.0f);
    glUniform1f(loc_iFrameRate, 60.0f);
    glUniform1i(loc_iFrame, frame);
    glUniform4f(loc_iDate, 2026.0f, 9.0f, 26.0f, 0.5f);
    glUniform4f(loc_iMouse, 0.0f, 0.0f, 0.0f, 0.0f);
    /* iSeed: per-frame from the override list if present (cycles),
     * otherwise the deterministic default. */
    if (g_n_seed_overrides > 0) {
        const float *src = g_seed_overrides[frame_idx % g_n_seed_overrides];
        glUniform4f(loc_iSeed, src[0], src[1], src[2], src[3]);
    } else {
        glUniform4f(loc_iSeed, 0.137f, 0.421f, 0.731f, 0.953f);
    }
    for (int i = 0; i < 4; i++) {
        glUniform3f(loc_iChannelRes[i], 1.0f, 1.0f, 1.0f);
        glUniform1f(loc_iChannelTime[i], 0.0f);
    }
    /* Bind dummy 1x1 to all 4 channel units. */
    for (int i = 0; i < 4; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, dummy_tex);
    }
    /* The shader's iChannel0..3 are sampler uniforms — bind them
     * to texture units. GLES3 binds sampler uniforms via
     * glUniform1i on their location. */
    GLint loc;
    loc = glGetUniformLocation(program, "iChannel0");
    if (loc >= 0) glUniform1i(loc, 0);
    loc = glGetUniformLocation(program, "iChannel1");
    if (loc >= 0) glUniform1i(loc, 1);
    loc = glGetUniformLocation(program, "iChannel2");
    if (loc >= 0) glUniform1i(loc, 2);
    loc = glGetUniformLocation(program, "iChannel3");
    if (loc >= 0) glUniform1i(loc, 3);

    glBindVertexArray(vao);
    glViewport(0, 0, dst_w, dst_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    unsigned char *pixels = (unsigned char *)malloc((size_t)dst_w * dst_h * 4);
    if (!pixels) return -1;
    glReadPixels(0, 0, dst_w, dst_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    /* Coverage + colour stats. */
    size_t nonblack = 0;
    double sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumL = 0;
    size_t npix = (size_t)dst_w * dst_h;
    for (size_t i = 0; i < npix; i++) {
        const unsigned char *p = pixels + i * 4;
        if (p[0] || p[1] || p[2]) nonblack++;
        double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
        double L = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        sumR += r; sumG += g; sumB += b; sumA += p[3] / 255.0; sumL += L;
    }
    *out_coverage = (double)nonblack / (double)npix;
    *out_red      = sumR / (double)npix;
    *out_green    = sumG / (double)npix;
    *out_blue     = sumB / (double)npix;
    *out_alpha    = sumA / (double)npix;
    *out_brightness = sumL / (double)npix;

    if (out_png) {
        /* Flip vertically for PNG (GL origin bottom-left). */
        unsigned char *flipped = (unsigned char *)malloc((size_t)dst_w * dst_h * 4);
        if (flipped) {
            for (int y = 0; y < dst_h; y++) {
                memcpy(flipped + y * dst_w * 4,
                       pixels + ((size_t)(dst_h - 1 - y)) * dst_w * 4,
                       (size_t)dst_w * 4);
            }
            write_png_rgba(out_png, flipped, (unsigned)dst_w,
                           (unsigned)dst_h);
            free(flipped);
        }
    }
    free(pixels);
    smoke_check_gl(t);
    return 0;
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <shader.glsl> <out-dir> [t1 t2 t3 ...] "
                "[--seed=A,B,C,D (repeatable)]\n",
                argv[0]);
        return 2;
    }
    const char *shader_path = argv[1];
    const char *out_dir = argv[2];

    float times[8];
    int ntimes = 0;
    int perf_n = 0;  /* --perf=N: do N silent renders for timing */
    for (int i = 3; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--seed=", 7) == 0) {
            /* Parse "A,B,C,D" — four floats in [0,1]. */
            if (g_n_seed_overrides < 16) {
                float *s = g_seed_overrides[g_n_seed_overrides++];
                int got = 0;
                const char *p = a + 7;
                while (*p && got < 4) {
                    char *end = NULL;
                    float v = strtof(p, &end);
                    if (end == p) break;
                    s[got++] = v;
                    p = end;
                    if (*p == ',') p++;
                }
                if (got != 4) {
                    fprintf(stderr,
                            "smoke: --seed expects 4 floats, got %d\n", got);
                    return 2;
                }
            }
            continue;
        }
        if (strncmp(a, "--perf=", 7) == 0) {
            perf_n = atoi(a + 7);
            continue;
        }
        if (ntimes < 8) {
            times[ntimes++] = strtof(a, NULL);
        }
    }
    if (ntimes == 0) {
        times[0] = 2.0f;  times[1] = 10.0f; times[2] = 25.0f;
        ntimes = 3;
    }

    /* Get basename for output naming. */
    const char *slash = strrchr(shader_path, '/');
    const char *base = slash ? slash + 1 : shader_path;
    char basename[128];
    snprintf(basename, sizeof basename, "%s", base);
    char *dot = strrchr(basename, '.');
    if (dot) *dot = '\0';
    g_basename = basename;

    mkdir(out_dir, 0755);

    size_t src_len = 0;
    char *body = read_file(shader_path, &src_len);
    if (!body) return 1;

    char *frag_src = build_frag_src(body);
    if (!frag_src) {
        fprintf(stderr, "smoke: out of memory building frag src\n");
        free(body);
        return 1;
    }
    free(body);

    if (init_egl() != 0) {
        fprintf(stderr, "smoke: EGL init failed\n");
        free(frag_src);
        return 1;
    }

    char compile_log[1024] = {0};
    char link_log[1024] = {0};
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src,
                               compile_log, sizeof compile_log);
    if (!vs) {
        fprintf(stderr, "smoke: vertex compile failed: %s\n", compile_log);
        free(frag_src);
        return 1;
    }
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src,
                               compile_log, sizeof compile_log);
    if (!fs) {
        fprintf(stderr,
                "smoke: %s fragment COMPILE_FAIL: %s\n",
                basename, compile_log);
        free(frag_src);
        return 1;
    }
    program = link_program(vs, fs, link_log, sizeof link_log);
    if (!program) {
        fprintf(stderr,
                "smoke: %s LINK_FAIL: %s\n", basename, link_log);
        free(frag_src);
        return 1;
    }
    free(frag_src);

    loc_iResolution = glGetUniformLocation(program, "iResolution");
    loc_iTime       = glGetUniformLocation(program, "iTime");
    loc_iTimeDelta  = glGetUniformLocation(program, "iTimeDelta");
    loc_iFrameRate  = glGetUniformLocation(program, "iFrameRate");
    loc_iFrame      = glGetUniformLocation(program, "iFrame");
    loc_iDate       = glGetUniformLocation(program, "iDate");
    loc_iMouse      = glGetUniformLocation(program, "iMouse");
    loc_iSeed       = glGetUniformLocation(program, "iSeed");
    char chname[16];
    for (int i = 0; i < 4; i++) {
        snprintf(chname, sizeof chname, "iChannelResolution[%d]", i);
        loc_iChannelRes[i] = glGetUniformLocation(program, chname);
        snprintf(chname, sizeof chname, "iChannelTime[%d]", i);
        loc_iChannelTime[i] = glGetUniformLocation(program, chname);
    }

    setup_fullscreen_quad();

    fprintf(stderr,
            "smoke: %s compiled+linked, capturing %d frames (%dx%d)\n",
            basename, ntimes, dst_w, dst_h);

    int all_ok = 1;
    for (int i = 0; i < ntimes; i++) {
        char out_path[1024];
        snprintf(out_path, sizeof out_path,
                 "%s/%s_t%.1fs.png", out_dir, basename, times[i]);
        double cov = 0, r = 0, g = 0, b = 0, a = 0, L = 0;
        render_frame(i, times[i], out_path, &cov, &r, &g, &b, &a, &L);
        int rendered = (cov > 0.05) ? 1 : 0;
        if (!rendered) all_ok = 0;
        fprintf(stderr,
                "[frame] %s t=%.1fs coverage=%.3f R=%.3f G=%.3f B=%.3f "
                "A=%.3f L=%.3f %s -> %s\n",
                basename, times[i], cov, r, g, b, a, L,
                rendered ? "RENDER_OK" : "RENDER_FAIL",
                out_path);
    }
    fprintf(stderr, "[diag] %s %s\n",
            basename, all_ok ? "PASS" : "PARTIAL");

    /* A GL error left behind by any frame is a hard failure: it means the
     * render path issued an invalid call (e.g. a glUniform with no program
     * bound, or an invalid glEnable cap) that a green image otherwise hides. */
    if (g_gl_errors > 0) {
        fprintf(stderr,
                "[diag] %s GL_ERRORS=%d (glGetError != 0 after render)\n",
                basename, g_gl_errors);
        all_ok = 0;
    }

    /* Optional --perf=N: do N silent renders at the LAST captured
     * time, measure total wall time. Useful for benchmarking a
     * shader at a fixed resolution. */
    if (perf_n > 0) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < perf_n; i++) {
            double cov, r, g, b, a, L;
            render_frame(i, times[(i < ntimes) ? i : 0], NULL,
                         &cov, &r, &g, &b, &a, &L);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double dt = (t1.tv_sec - t0.tv_sec) +
                    (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        fprintf(stderr,
                "[perf] %s N=%d size=%dx%d total=%.3fs "
                "avg=%.3fms (%.1f fps)\n",
                basename, perf_n, dst_w, dst_h, dt,
                dt * 1000.0 / perf_n,
                perf_n / dt);
    }
    if (g_gl_errors > 0) return 4;
    return all_ok ? 0 : 3;
}
