/* SPDX-License-Identifier: Apache-2.0 */
/* xshadertoy_render.c - headless renderer and GPU bench for the xshadertoy
 * family, built on the same source assembly, options, seed, palette and
 * director code as the screensaver driver (src/xst_core.c). No compositor.
 *
 * Frames at exact times (contact sheets; same seed = same frames as a live
 * run at those times):
 *   xshadertoy_render SHADER.glsl --out DIR [--size 1920x1080] [--times 5,20,45]
 *                     [--tag NAME] [--iseed a,b,c,d] [--preset=ID] [--OPTION=VALUE ...]
 * Sustained GPU bench (glFinish per frame, back to back, motion time = wall):
 *   xshadertoy_render SHADER.glsl --bench SECONDS [--window 60] [--size WxH] [--OPTION=VALUE ...]
 *   prints one "[xst-bench] window ..." line per window and a final JSON line
 *   "[xst-bench-json] {...}" with p50/p95/p99/max over the run, per-window p95
 *   and drift = last window p95 / first window p95.
 *
 * Exit 77 (skip) on a software renderer unless NCZ_ALLOW_SOFTWARE=1.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <png.h>

#include "xst_core.h"
#include "ncz_harness_cfg.h"

/* --dump-schema / --list-presets / --print-config go through the harness's own
 * code (src/ncz_harness_cfg.c) with this spec as the hack table, so the
 * output is byte-identical to the screensaver binary's. */
static XstSpec g_spec;
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    if (!g_spec.has_saver) { *n = 0; return NULL; }
    *n = g_spec.n; *prefix = g_spec.prefix; *group = g_spec.group;
    return g_spec.defs;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int egl_up(void) {
    EGLDisplay d = EGL_NO_DISPLAY;
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    const char *ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (gpd && ext && strstr(ext, "EGL_MESA_platform_surfaceless"))
        d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    int surfaceless = d != EGL_NO_DISPLAY;
    if (!surfaceless) d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) { fprintf(stderr, "egl: no display\n"); return -1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, surfaceless ? 0 : EGL_PBUFFER_BIT,
                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(d, ca, &cfg, 1, &n) || n < 1) { fprintf(stderr, "egl: no config\n"); return -1; }
    EGLint cx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cx);
    if (c == EGL_NO_CONTEXT) { fprintf(stderr, "egl: no context\n"); return -1; }
    EGLSurface s = EGL_NO_SURFACE;
    if (!surfaceless) {
        EGLint pa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        s = eglCreatePbufferSurface(d, cfg, pa);
    }
    if (!eglMakeCurrent(d, s, s, c)) { fprintf(stderr, "egl: make current failed\n"); return -1; }
    return 0;
}

static GLuint compile(GLenum st, const char *src) {
    GLuint s = glCreateShader(st);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192]; glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "compile failed:\n%s\n", log);
        return 0;
    }
    return s;
}

static int write_png(const char *path, int w, int h, const unsigned char *rgba) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = p ? png_create_info_struct(p) : NULL;
    unsigned char *row = malloc((size_t)w * 3);
    if (!p || !i || !row) { png_destroy_write_struct(p ? &p : NULL, i ? &i : NULL); free(row); fclose(f); return -1; }
    if (setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p, &i); free(row); fclose(f); return -1; }
    png_init_io(p, f);
    png_set_IHDR(p, i, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, i);
    for (int y = h - 1; y >= 0; y--) {     /* GL rows are bottom-up */
        for (int x = 0; x < w; x++) memcpy(row + x * 3, rgba + ((size_t)y * w + x) * 4, 3);
        png_write_row(p, row);
    }
    free(row);
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &i);
    fclose(f);
    return 0;
}

/* JSON string body: escapes quotes, backslashes and control bytes */
static void json_str(FILE *f, const char *s) {
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
}

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static float pct(float *v, int n, double q) { return n ? v[(int)(n * q) < n ? (int)(n * q) : n - 1] : 0.f; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: see the top of xshadertoy_render.c\n"); return 2; }
    const char *shader = argv[1], *out = NULL, *times = "5,20,45", *tag = NULL, *iseed_s = NULL;
    int W = 1920, H = 1080, quality = -1;
    int gate = 0;   /* --gate: fail (exit 3) when a frame is unclearable */
    double bench = 0, window = 60;
    char **rest = calloc((size_t)argc + 1, sizeof *rest);
    int nrest = 0;
    rest[nrest++] = argv[0];
    const char *preset = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &W, &H) != 2) return 2; }
        else if (!strcmp(a, "--times") && i + 1 < argc) times = argv[++i];
        else if (!strcmp(a, "--tag") && i + 1 < argc) tag = argv[++i];
        else if (!strcmp(a, "--iseed") && i + 1 < argc) iseed_s = argv[++i];
        else if (!strcmp(a, "--bench") && i + 1 < argc) bench = atof(argv[++i]);
        else if (!strcmp(a, "--window") && i + 1 < argc) {
            window = atof(argv[++i]);
            if (!isfinite(window) || window < 0.5) { fprintf(stderr, "--window needs seconds >= 0.5\n"); return 2; }
        }
        else if (!strcmp(a, "--class-quality") && i + 1 < argc) quality = atoi(argv[++i]);
        else if (!strcmp(a, "--gate")) gate = 1;
        else { if (!strncmp(a, "--preset=", 9)) preset = a + 9; rest[nrest++] = argv[i]; }
    }
    if (W <= 0 || H <= 0 || W > 16384 || H > 16384) { fprintf(stderr, "bad --size\n"); return 2; }
    if (quality < 0) {
        const char *e = getenv("NCZ_SHADER_QUALITY");
        quality = e && *e ? atoi(e) : 2;
    }
    char used[1024];
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-schema") || !strcmp(argv[i], "--list-presets") || !strcmp(argv[i], "--print-config")) {
            char *r0 = xst_load_shader(shader, used, sizeof used);
            char *b0 = r0 ? xst_splice_near(r0, used) : NULL;
            if (!b0) return 1;
            xst_spec_parse(&g_spec, shader, b0);
            char *av[64]; int ac = 0;
            av[ac++] = argv[0];
            for (int j = 2; j < argc && ac < 63; j++) av[ac++] = argv[j];
            av[ac] = NULL;
            int rc = ncz_harness_cfg_init(ac, av);
            return rc < 0 ? 0 : rc;
        }
    }
    char *raw = xst_load_shader(shader, used, sizeof used);
    if (!raw) return 1;
    char *body = xst_splice_near(raw, used);
    free(raw);
    if (!body) return 1;
    static XstSpec spec;
    xst_spec_parse(&spec, shader, body);
    ncz_opts o;
    int have_opts = spec.has_saver && ncz_opts_init(&o, spec.defs, spec.n) == 0;
    if (have_opts) {
        if (preset) {
            for (const char *c = preset; *c; c++)
                if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) {
                    fprintf(stderr, "bad preset name '%s'\n", preset);
                    return 2;
                }
            char path[1024];
            snprintf(path, sizeof path, "assets/screensaver-chooser/options/presets/%s/%s.conf", spec.group, preset);
            FILE *f = fopen(path, "r");
            if (!f) { fprintf(stderr, "preset %s not found (%s)\n", preset, path); return 2; }
            fclose(f);
            ncz_opts_apply_keyfile(&o, path, spec.group, "preset");
        }
        int fl = ncz_opts_apply_argv(&o, nrest, rest, argv[0]);
        if (fl & (NCZ_ARG_UNKNOWN_OPTION | NCZ_ARG_STRAY_ARGUMENT)) return 2;
    } else if (nrest > 1) {
        fprintf(stderr, "note: %s has no //@saver; options ignored\n", shader);
    }
    static XstRun run;
    xst_run_init(&run, &spec, have_opts ? &o : NULL, quality);
    if (iseed_s) sscanf(iseed_s, "%f,%f,%f,%f", &run.iseed[0], &run.iseed[1], &run.iseed[2], &run.iseed[3]);
    fprintf(stderr, "[xst-render] iSeed=%.6f,%.6f,%.6f,%.6f\n", run.iseed[0], run.iseed[1], run.iseed[2], run.iseed[3]);

    if (egl_up() != 0) return 1;
    const char *rend = (const char *)glGetString(GL_RENDERER);
    fprintf(stderr, "[xst-render] renderer=%s\n", rend ? rend : "?");
    if (rend && (strstr(rend, "llvmpipe") || strstr(rend, "softpipe") || strstr(rend, "SwiftShader"))) {
        const char *al = getenv("NCZ_ALLOW_SOFTWARE");
        if (!al || strcmp(al, "1")) { fprintf(stderr, "software renderer: skipping\n"); return 77; }
    }
    char *fs_src = xst_fragment_source(&run, body);
    static const char *vs_src = "#version 300 es\nlayout(location=0) in vec2 a;\nvoid main(){gl_Position=vec4(a,0.0,1.0);}\n";
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src), fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 1;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a");
    glLinkProgram(prog);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { fprintf(stderr, "link failed\n"); return 1; }
    glUseProgram(prog);

    GLuint fbo, tex;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 1; }
    static const float quad[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glViewport(0, 0, W, H);
    GLint l_res = glGetUniformLocation(prog, "iResolution"), l_t = glGetUniformLocation(prog, "iTime");
    GLint l_dt = glGetUniformLocation(prog, "iTimeDelta"), l_fr = glGetUniformLocation(prog, "iFrame");
    GLint l_fps = glGetUniformLocation(prog, "iFrameRate"), l_seed = glGetUniformLocation(prog, "iSeed");
    GLint l_pal = glGetUniformLocation(prog, "uPal"), l_dir = glGetUniformLocation(prog, "uDir");
    GLint l_date = glGetUniformLocation(prog, "iDate");
    GLint l_bind[XST_MAX_BINDS];
    for (int b = 0; b < run.nbind; b++) l_bind[b] = glGetUniformLocation(prog, run.bind_uniform[b]);
    glUniform3f(l_res, (float)W, (float)H, 1.f);
    glUniform4fv(l_seed, 1, run.iseed);
    float date[4] = {2026, 9, 29, 43200};
    glUniform4fv(l_date, 1, date);
    glUniform1f(l_fps, 60.f);
    for (int b = 0; b < run.nbind; b++) glUniform4fv(l_bind[b], 1, run.bind_val[b]);

    if (spec.has_saver) {
        char line[4096];
        xst_run_describe(&run, line, sizeof line, W, H, xst_fnv1a(body));
        fprintf(stderr, "%s\n", line);
    }

    /* one frame at wall time t: motion = t * speed */
    #define SET_TIME(t_wall, frame) do { \
        double _m = (t_wall) * (double)run.speed; \
        glUniform1f(l_t, (float)_m); glUniform1f(l_dt, (float)(run.speed / 60.0)); glUniform1i(l_fr, (frame)); \
        if (spec.has_saver) { float _d[16], _p[12]; xst_director(&run, _m, _d); xst_palette_at(&run, (t_wall), _p); \
            glUniform4fv(l_dir, 4, _d); glUniform3fv(l_pal, 4, _p); } } while (0)

    if (bench > 0) {
        int cap = 1 << 20, n = 0, wn = 0, nwin = 0;
        float *all = malloc(sizeof(float) * (size_t)cap), *win = malloc(sizeof(float) * (size_t)cap);
        if (!all || !win) { fprintf(stderr, "out of memory\n"); return 1; }
        float wp95[1024];
        /* warm up: compile, first frames */
        for (int i = 0; i < 10; i++) { SET_TIME(0.0, i); glDrawArrays(GL_TRIANGLES, 0, 6); }
        glFinish();
        double t0 = now_s(), tw = t0, last = t0;
        int frame = 0;
        while (1) {
            double t = now_s();
            if (t - t0 >= bench) break;
            SET_TIME(t - t0, frame);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glFinish();
            double t2 = now_s();
            float ms = (float)((t2 - last) * 1000.0);
            last = t2;
            frame++;
            if (n < cap) all[n++] = ms;
            if (wn < cap) win[wn++] = ms;
            if (t2 - tw >= window) {
                qsort(win, (size_t)wn, sizeof(float), cmpf);
                float p95 = pct(win, wn, 0.95);
                if (nwin < 1024) wp95[nwin++] = p95;
                fprintf(stderr, "[xst-bench] window=%d t=%.0f n=%d p50=%.2f p95=%.2f p99=%.2f max=%.2f\n",
                        nwin, t2 - t0, wn, pct(win, wn, 0.5), p95, pct(win, wn, 0.99), win[wn - 1]);
                wn = 0; tw = t2;
            }
        }
        qsort(all, (size_t)n, sizeof(float), cmpf);
        printf("[xst-bench-json] {\"shader\":\"");
        json_str(stdout, spec.has_saver ? spec.id : shader);
        printf("\",\"renderer\":\"");
        json_str(stdout, rend ? rend : "?");
        printf("\",\"size\":\"%dx%d\",\"seed\":%u,\"quality\":%d,"
               "\"seconds\":%.0f,\"frames\":%d,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f,\"window_p95\":[",
               W, H, run.seed, run.quality, bench, n,
               pct(all, n, 0.5), pct(all, n, 0.95), pct(all, n, 0.99), n ? all[n - 1] : 0.f);
        for (int i = 0; i < nwin; i++) printf("%s%.3f", i ? "," : "", wp95[i]);
        printf("],\"drift\":%.3f}\n", nwin >= 2 && wp95[0] > 0 ? wp95[nwin - 1] / wp95[0] : 1.0);
        return 0;
    }

    if (!out) { fprintf(stderr, "--out DIR required for frames\n"); return 2; }
    mkdir(out, 0755);
    unsigned char *px = malloc((size_t)W * H * 4);
    if (!px) { fprintf(stderr, "out of memory\n"); return 1; }
    const char *base = strrchr(shader, '/');
    base = base ? base + 1 : shader;
    char stem[128];
    snprintf(stem, sizeof stem, "%s", tag ? tag : base);
    char *dot = strstr(stem, ".glsl"); if (dot) *dot = 0;
    char tl[512]; snprintf(tl, sizeof tl, "%s", times);
    int frame = 0;
    int gate_fail = 0;
    for (char *tok = strtok(tl, ","); tok; tok = strtok(NULL, ",")) {
        double t = atof(tok);
        SET_TIME(t, (int)(t * 60.0));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        /* Frame stats: mean R/G/B/A and mean luma over the whole frame.  The
         * alpha mean is the blank-saver gate: a shader that leaves .a at 0
         * composites to nothing even when its RGB is correct. */
        size_t np = (size_t)W * (size_t)H;
        double sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumL = 0;
        for (size_t k = 0; k < np; k++) {
            const unsigned char *p = px + k * 4;
            double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0,
                   al = p[3] / 255.0;
            sumR += r; sumG += g; sumB += b; sumA += al;
            sumL += 0.2126 * r + 0.7152 * g + 0.0722 * b;
        }
        double meanA = sumA / (double)np, meanL = sumL / (double)np;
        fprintf(stderr,
                "[xst-stats] shader=%s t=%.2f meanR=%.4f meanG=%.4f meanB=%.4f "
                "meanA=%.4f meanL=%.4f\n",
                stem, t, sumR / (double)np, sumG / (double)np, sumB / (double)np,
                meanA, meanL);
        if (gate) {
            if (meanA < 0.99) {
                fprintf(stderr,
                        "[xst-stats] GATE FAIL %s t=%.2f: mean alpha %.4f < 0.99\n",
                        stem, t, meanA);
                gate_fail = 1;
            }
            if (!(meanL > 0.05)) {
                fprintf(stderr,
                        "[xst-stats] GATE FAIL %s t=%.2f: mean luma %.4f <= 0.05\n",
                        stem, t, meanL);
                gate_fail = 1;
            }
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s_t%07.2f.png", out, stem, t);
        if (write_png(path, W, H, px) != 0) { fprintf(stderr, "write %s failed\n", path); return 1; }
        fprintf(stderr, "[xst-render] %s\n", path);
        frame++;
    }
    if (gate_fail) return 3;
    return glGetError() == GL_NO_ERROR ? 0 : 1;
}
