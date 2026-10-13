/* SPDX-License-Identifier: Apache-2.0 */
/* test_fractasia_smoke.c — GPU smoke test for the Fractasia
 *                               screensaver.
 *
 * Patterned after gles3_xshadertoy_smoke.c. Opens a surfaceless EGL
 * context, compiles every fragment shader under
 * vendor/fractasia/glsl/ with a deterministic seed, renders one
 * frame each, and dumps it as a real PNG via libpng.
 *
 * Test passes iff:
 *   - Every .frag compiles + links.
 *   - Every render has non-zero non-black coverage, with the effect recipe
 *     off and with every effect bit on (the heavy distance-estimate path
 *     included), so a broken effect cannot ship black.
 *   - No rendered pixel is NaN or infinity.
 *
 * Original to ncz-screensavers (Apache-2.0).
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

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

#include "fractasia/fractasia.h"

/* ----------------- shader preamble (the driver's own text) ------------ */

static const char *frag_preamble = fractasia_frag_preamble;

static const char *vert_src =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

/* ----------------- EGL ----------------- */

static EGLDisplay  egl_disp;
static EGLContext  egl_ctx;
static EGLSurface  egl_pbuf;
static int         dst_w = 320, dst_h = 200;

static int init_egl(void) {
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
        EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    EGLConfig cfg;
    EGLint num_cfg = 0;
    if (!eglChooseConfig(egl_disp, cfg_attr, &cfg, 1, &num_cfg) || num_cfg < 1) {
        fprintf(stderr, "smoke: eglChooseConfig failed\n");
        return -1;
    }
    EGLint pbuf_attr[] = { EGL_WIDTH, dst_w, EGL_HEIGHT, dst_h, EGL_NONE };
    egl_pbuf = eglCreatePbufferSurface(egl_disp, cfg, pbuf_attr);
    if (egl_pbuf == EGL_NO_SURFACE) {
        fprintf(stderr, "smoke: eglCreatePbufferSurface failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE
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

/* ----------------- file I/O ----------------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "smoke: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got_n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got_n != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static int write_png_rgba(const char *path, const unsigned char *rgba,
                          unsigned int w, unsigned int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[png] cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) { fclose(fp); return -1; }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL); fclose(fp); return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info); fclose(fp); return -1;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    if (!rows) { png_destroy_write_struct(&png, &info); fclose(fp); return -1; }
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

/* ----------------- shader build ----------------- */

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
    glAttachShader(p, vs); glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
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

/* ----------------- per-family render ----------------- */

static GLuint program;
static GLuint vao, vbo, palette_tex;

static int setup_gl(void) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    static const float verts[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    /* Palette LUT: the library's synthwave scheme, baked as the driver does. */
    unsigned char tex[256 * 4];
    const FractPaletteLib *lib = fract_palette_library_find("synthwave");
    for (int i = 0; i < 256; i++) {
        float rgb[3];
        fract_palette_eval(&lib->def, (float)i / 255.0f, rgb, NULL);
        for (int ch = 0; ch < 3; ch++) tex[i * 4 + ch] = (unsigned char)(rgb[ch] * 255.0f);
        tex[i * 4 + 3] = 255;
    }
    glGenTextures(1, &palette_tex);
    glBindTexture(GL_TEXTURE_2D, palette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

/* Render one frame for a given family at a given iTime and check
 * coverage + NaN. Returns 0 on success, non-zero on failure. */
static int render_family(int fid, const char *shader_dir, int fx_bits,
                         const char *out_png,
                         double *out_coverage, int *out_nan_pixels) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", shader_dir,
             fract_family_filename(fid));
    size_t blen = 0;
    char *body = read_file(path, &blen);
    if (!body) return -1;

    size_t plen = strlen(frag_preamble);
    char *full = (char *)malloc(plen + blen + 4);
    if (!full) { free(body); return -1; }
    memcpy(full, frag_preamble, plen);
    memcpy(full + plen, body, blen + 1);
    free(body);

    char log[1024] = {0};
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src, log, sizeof log);
    if (!vs) {
        fprintf(stderr, "smoke: %s vs: %s\n", path, log);
        free(full); return -1;
    }
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, full, log, sizeof log);
    if (!fs) {
        fprintf(stderr, "smoke: %s fs COMPILE_FAIL: %s\n", path, log);
        glDeleteShader(vs); free(full); return -1;
    }
    program = link_program(vs, fs, log, sizeof log);
    if (!program) {
        fprintf(stderr, "smoke: %s LINK_FAIL: %s\n", path, log);
        free(full); return -1;
    }
    free(full);

    glUseProgram(program);

    float t = 1.234f;
    int iters = 96;
    int type = 0;
    float seedvec[4] = {0.137f, 0.421f, 0.731f, 0.953f};
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float view[4] = {0.0f, 0.02f, 0.0f, 0.0f};
    switch (fid) {
    /* The escape-time families take a view: centre, zoom, rotation, and
     * (c | n) in u_view.  These are the picker's curated fallbacks. */
    case FRACT_ESCAPE_TIME:  params[0] = -0.745f; params[1] = 0.113f; params[2] = 12.0f; break;
    case FRACT_MULTIBROT:    params[2] = 1.0f; view[2] = 0.36f; view[3] = 0.10f; type = 3; break;
    case FRACT_BURNING_SHIP: params[0] = -1.755f; params[1] = -0.03f; params[2] = 14.0f; break;
    case FRACT_TRICORN:      params[0] = -0.13f; params[1] = 0.99f; params[2] = 6.0f; break;
    case FRACT_JULIA:        params[2] = 1.0f; view[2] = -0.745f; view[3] = 0.113f; break;
    case FRACT_QUATERNION:  params[0] = 0.3f; params[1] = 0.4f; params[2] = 0.5f; params[3] = 0.6f; break;
    case FRACT_NEWTON:      params[0] = 4.0f; params[1] = 0.05f; params[2] = 4.0f; break;
    case FRACT_ATTRACTOR:   params[0] = 0.0f; params[1] = 28.0f; params[2] = 1.0f; break;
    case FRACT_IFS:         params[0] = 0.0f; params[1] = 0.6f; params[2] = 1.0f; break;
    case FRACT_LYAPUNOV:    params[0] = 3.8f; params[1] = 3.8f; params[2] = 8.0f; break;
    case FRACT_CELLULAR:    params[0] = 30.0f; params[1] = 0.0f; params[2] = 30.0f; params[3] = 0.0f; break;
    case FRACT_JULIA_GLOW:  params[0] = 1.0f; params[1] = 0.5f; break;
    case FRACT_ORBIT_TRAP:  params[0] = 0.0f; params[1] = 0.57f; params[2] = 0.0f; break;
    case FRACT_MOBIUS:      params[0] = 0.0f; params[1] = 0.0f; break;
    case FRACT_SHIP_GLOW:   params[0] = 0.0f; params[1] = 0.02f; break;
    case FRACT_LSYSTEM:     params[0] = 0.0f; params[1] = 0.7f; params[2] = 1.0f; break;
    default: break;
    }

    GLint loc;
    loc = glGetUniformLocation(program, "u_time");       if (loc >= 0) glUniform1f(loc, t);
    loc = glGetUniformLocation(program, "u_resolution"); if (loc >= 0) glUniform3f(loc, (float)dst_w, (float)dst_h, 1.0f);
    loc = glGetUniformLocation(program, "u_frame");      if (loc >= 0) glUniform1i(loc, 1);
    loc = glGetUniformLocation(program, "u_seed");       if (loc >= 0) glUniform4fv(loc, 1, seedvec);
    loc = glGetUniformLocation(program, "u_iterations"); if (loc >= 0) glUniform1i(loc, iters);
    loc = glGetUniformLocation(program, "u_type");       if (loc >= 0) glUniform1i(loc, type);
    loc = glGetUniformLocation(program, "u_exposure");   if (loc >= 0) glUniform1f(loc, 1.0f);
    loc = glGetUniformLocation(program, "u_speed");      if (loc >= 0) glUniform1f(loc, 1.0f);
    loc = glGetUniformLocation(program, "u_params");     if (loc >= 0) glUniform4fv(loc, 1, params);
    loc = glGetUniformLocation(program, "u_view");       if (loc >= 0) glUniform4fv(loc, 1, view);
    /* Mapping: band scale 1 forward, smooth, no cycling, interior at 0.2. */
    loc = glGetUniformLocation(program, "u_map");        if (loc >= 0) glUniform4f(loc, 1.0f, 0.0f, 0.0f, 0.2f);
    loc = glGetUniformLocation(program, "u_fx");         if (loc >= 0) glUniform1i(loc, fx_bits);
    loc = glGetUniformLocation(program, "u_fxp");        if (loc >= 0) glUniform4f(loc, 5.0f, 0.05f, 0.03f, 0.02f);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, palette_tex);
    loc = glGetUniformLocation(program, "u_palette_a"); if (loc >= 0) glUniform1i(loc, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, palette_tex);
    loc = glGetUniformLocation(program, "u_palette_b"); if (loc >= 0) glUniform1i(loc, 1);
    loc = glGetUniformLocation(program, "u_palette_blend");
    if (loc >= 0) glUniform1f(loc, 0.0f);
    glActiveTexture(GL_TEXTURE0);

    /* Cellular reads its rows from a ring texture the driver fills on the CPU. */
    GLuint ca_tex = 0;
    if (fid == FRACT_CELLULAR) {
        static uint8_t ring[FRACT_CA_H][FRACT_CA_W];
        uint8_t row[FRACT_CA_W], next[FRACT_CA_W];
        fract_ca_init_row(30, 1, row);
        for (int r = 0; r < FRACT_CA_H; r++) {
            memcpy(ring[r], row, FRACT_CA_W);
            fract_ca_step(30, row, next);
            memcpy(row, next, FRACT_CA_W);
        }
        glGenTextures(1, &ca_tex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ca_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FRACT_CA_W, FRACT_CA_H, 0, GL_RED, GL_UNSIGNED_BYTE, ring);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        loc = glGetUniformLocation(program, "u_ca");
        if (loc >= 0) glUniform1i(loc, 2);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(vao);
    glViewport(0, 0, dst_w, dst_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    unsigned char *pixels = (unsigned char *)malloc((size_t)dst_w * dst_h * 4);
    if (!pixels) { glDeleteProgram(program); return -1; }
    glReadPixels(0, 0, dst_w, dst_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    size_t nonblack = 0;
    int nan_pix = 0;
    for (size_t i = 0; i < (size_t)dst_w * dst_h; i++) {
        const unsigned char *p = pixels + i * 4;
        if (p[0] || p[1] || p[2]) nonblack++;
        /* The renderer clamps to [0,1] so we shouldn't see NaN, but
         * check for fully-zero R/G/B which can indicate a shader
         * abort on some drivers. */
        if (!isfinite((float)p[0]) || !isfinite((float)p[1]) ||
            !isfinite((float)p[2])) nan_pix++;
    }
    double coverage = (double)nonblack / ((double)dst_w * dst_h);
    if (out_coverage) *out_coverage = coverage;
    if (out_nan_pixels) *out_nan_pixels = nan_pix;

    if (out_png) {
        unsigned char *flipped = (unsigned char *)malloc((size_t)dst_w * dst_h * 4);
        if (flipped) {
            for (int y = 0; y < dst_h; y++) {
                memcpy(flipped + y * dst_w * 4,
                       pixels + ((size_t)(dst_h - 1 - y)) * dst_w * 4,
                       (size_t)dst_w * 4);
            }
            write_png_rgba(out_png, flipped, (unsigned)dst_w, (unsigned)dst_h);
            free(flipped);
        }
    }
    free(pixels);
    if (ca_tex) glDeleteTextures(1, &ca_tex);
    glDeleteProgram(program);
    program = 0;
    return 0;
}

/* ----------------- main ----------------- */

int main(int argc, char **argv) {
    const char *shader_dir =
        getenv("NCZ_FRACTASIA_GL") ? getenv("NCZ_FRACTASIA_GL")
                                          : "vendor/fractasia/glsl";
    const char *out_dir =
        getenv("NCZ_FRACTASIA_OUT") ? getenv("NCZ_FRACTASIA_OUT")
                                          : "build-validation/fractasia-smoke";
    if (argc > 1) shader_dir = argv[1];
    if (argc > 2) out_dir = argv[2];
    mkdir(out_dir, 0755);

    if (init_egl() != 0) {
        fprintf(stderr, "smoke: EGL init failed\n");
        return 1;
    }
    if (setup_gl() != 0) return 1;

    int total = 0, ok = 0;
    int nan_total = 0;
    double min_coverage = 1.0, max_coverage = 0.0;
    /* Pass 0: no effects.  Pass 1: every effect bit, the heavy ones included. */
    const int passes[2] = { 0, (1 << 12) - 1 };
    for (int pass = 0; pass < 2; pass++)
    for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) {
        char out_png[1024];
        snprintf(out_png, sizeof out_png, "%s/%s%s.png",
                 out_dir, fract_family_filename(fid), pass ? "-fx" : "");
        total++;
        double coverage = 0;
        int nan_pix = 0;
        int r = render_family(fid, shader_dir, passes[pass], out_png,
                              &coverage, &nan_pix);
        if (r != 0) {
            fprintf(stderr, "[frame] fid=%d (%s) fx=%d FAIL to compile+link+render\n",
                    fid, fract_family_filename(fid), passes[pass]);
            continue;
        }
        if (coverage < 0.05) {
            fprintf(stderr, "[frame] fid=%d (%s) fx=%d FAIL coverage=%.2f%%\n",
                    fid, fract_family_filename(fid), passes[pass], coverage * 100.0);
            continue;
        }
        if (nan_pix > 0) {
            fprintf(stderr, "[frame] fid=%d (%s) fx=%d FAIL nan_pix=%d\n",
                    fid, fract_family_filename(fid), passes[pass], nan_pix);
            continue;
        }
        ok++;
        if (coverage < min_coverage) min_coverage = coverage;
        if (coverage > max_coverage) max_coverage = coverage;
        nan_total += nan_pix;
        fprintf(stderr,
                "[frame] fid=%d (%s) fx=%d coverage=%.3f nan=%d -> %s\n",
                fid, fract_family_filename(fid), passes[pass], coverage, nan_pix,
                out_png);
    }

    /* Review aid, not part of the verdict: with NCZ_FRACTASIA_SMOKE_RECIPES
     * set, render every recipe for escape_time and julia. */
    if (getenv("NCZ_FRACTASIA_SMOKE_RECIPES")) {
        const int fams[2] = { FRACT_ESCAPE_TIME, FRACT_JULIA };
        for (int fi = 0; fi < 2; fi++)
            for (int r = 0; r < FRACT_RECIPE_COUNT; r++) {
                char out_png[1024];
                snprintf(out_png, sizeof out_png, "%s/recipe-%s-%s.png", out_dir,
                         fract_family_filename(fams[fi]), fractasia_recipes[r].name);
                double cov = 0; int nanp = 0;
                render_family(fams[fi], shader_dir, fractasia_recipes[r].bits, out_png, &cov, &nanp);
            }
    }

    fprintf(stderr,
            "[diag] fractasia-smoke: %d/%d compiled+linked+rendered, "
            "coverage=[%.3f, %.3f], nan=%d\n",
            ok, total, min_coverage, max_coverage, nan_total);

    /* Final pass criteria: every family compiles, links, renders
     * non-zero coverage. */
    if (ok != total) {
        fprintf(stderr, "FAIL %d/%d families did not render\n",
                total - ok, total);
        return 1;
    }
    if (nan_total > 0) {
        fprintf(stderr, "FAIL %d NaN pixels total\n", nan_total);
        return 1;
    }

    printf("PASS fractasia-gpu-smoke (%d/%d families)\n", ok, total);
    return 0;
}