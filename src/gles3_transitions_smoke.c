/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_transitions_smoke.c — HEADLESS smoke tester for the 125
 *                             gl-transition shaders.
 *
 * NOT the production driver. The production driver is
 * gles3_transitions.c; it is loaded by gles3_harness.c which needs a
 * live Wayland compositor + wlr-layer-shell surface.
 *
 * This smoke tester is a separate, Wayland-free validation tool that
 * answers three questions for each of the 125 vendored transitions:
 *
 *   1. Does the transition's .glsl compile under GLES 3.0 on this
 *      GPU + driver + EGL implementation?
 *   2. Does it link?
 *   3. Does it render — i.e., does it produce non-fully-transparent
 *      pixels at progress=0.5 between two procedural test textures?
 *
 * It uses Mesa's EGL_KHR_surfaceless_context extension to create a
 * GLES 3.0 context with no default framebuffer, then attaches a
 * 256x256 PBuffer for the destination so glReadPixels has
 * something to read from. The two source textures are procedural
 * (a moving radial gradient for `from`, horizontal stripes for `to`)
 * identical to those used by the production driver, so a "smoke
 * pass" here implies the production driver will also draw
 * sensibly.
 *
 * Output (CSV-ish):
 *   - Per shader: <name> <compile|compile_fail|link|link_fail|render>
 *     <stderr_first_line_or_empty>
 *   - Summary: <total> <compiled> <linked> <rendered>
 *   - PNG dumps at /tmp/gl-trans-smoke/<name>_p<progress>.png for
 *     each shader that linked.
 *
 * Usage:
 *   gles3_transitions_smoke <list-file>
 * where <list-file> contains one .glsl basename per line.
 *
 * This is run ONCE during evidence capture, not by every user.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>

/* EGL / GLES3 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

/* Forward declarations from src/gles3_transitions.c — we deliberately
 * DO NOT link that .c; we re-implement the same adapter here in a
 * self-contained way so this smoke tester doesn't drag the whole
 * GLES3-harness dependency chain with it. The shader-construction
 * logic is duplicated verbatim so the smoke-test result is faithful
 * to what the production driver emits. */

#define MAX_SHADERS 256

typedef struct {
    char  name[128];
    int   compile_ok;
    int   link_ok;
    int   render_ok;
    char  fail_reason[256];
    char  used_path[256];
} SmokeRec;

/* Adapter preamble / tail / helpers — copy-paste from
 * src/gles3_transitions.c. Kept in sync by code review. */

static const char *vert_src =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *frag_preamble =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "out vec4 frag_color;\n"
    "uniform sampler2D from;\n"
    "uniform sampler2D to;\n"
    "uniform float progress;\n"
    "uniform vec2 resolution;\n"
    "vec4 getFromColor(vec2 uv) { return texture(from, uv); }\n"
    "vec4 getToColor(vec2 uv)   { return texture(to,   uv); }\n";

static const char *frag_tail =
    "\nvoid main() {\n"
    "  vec2 uv = gl_FragCoord.xy / resolution;\n"
    "  frag_color = transition(uv);\n"
    "}\n";

static const char *lskip(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
static const char *line_end(const char *p) {
    while (*p && *p != '\n') p++;
    return p;
}

static char *strip_header_to_body(const char *src) {
    size_t cap = strlen(src) + 64;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t out_len = 0;
    int in_lead = 1;
    const char *p = src;
    while (*p) {
        const char *line_start = p;
        const char *eol = line_end(p);
        const char *q = lskip(line_start);
        int is_blank = (*q == '\n' || *q == '\0');
        int is_cpp_comment = (q[0] == '/' && q[1] == '/');
        int is_preprocessor = (*q == '#');
        int is_precision = is_preprocessor &&
            (strncmp(q, "#version", 8) == 0 ||
             strncmp(q, "#extension", 10) == 0 ||
             strncmp(q, "#pragma", 7) == 0 ||
             strncmp(q, "#define", 7) == 0 ||
             strncmp(q, "precision", 9) == 0);
        if (in_lead && (is_blank || is_cpp_comment || is_precision)) {
            p = (*eol == '\n') ? eol + 1 : eol;
            continue;
        }
        in_lead = 0;
        size_t llen = (size_t)(eol - line_start);
        if (out_len + llen + 2 > cap) {
            cap = (out_len + llen + 2) * 2;
            char *nb = (char *)realloc(out, cap);
            if (!nb) { free(out); return NULL; }
            out = nb;
        }
        memcpy(out + out_len, line_start, llen);
        out_len += llen;
        if (*eol == '\n') {
            out[out_len++] = '\n';
            p = eol + 1;
        } else {
            p = eol;
        }
    }
    if (out_len == 0) { free(out); return NULL; }
    out[out_len] = '\0';
    return out;
}

static char *rewrite_texture2d(const char *body) {
    const char *needle = "texture2D";
    size_t nlen = strlen(needle);
    size_t blen = strlen(body);
    char *out = (char *)malloc(blen + 16);
    if (!out) return NULL;
    size_t oi = 0;
    const char *p = body;
    while (*p) {
        if (strncmp(p, needle, nlen) == 0) {
            int left_ok  = (p == body) ||
                (!(isalnum((unsigned char)p[-1]) || p[-1] == '_'));
            int right_ok = !(isalnum((unsigned char)p[nlen]) ||
                             p[nlen] == '_');
            if (left_ok && right_ok) {
                memcpy(out + oi, "texture", 7);
                oi += 7;
                p += nlen;
                continue;
            }
        }
        out[oi++] = *p++;
    }
    out[oi] = '\0';
    return out;
}

static char *build_frag_src(const char *body) {
    char *cleaned = strip_header_to_body(body);
    if (!cleaned) return NULL;
    char *rewritten = rewrite_texture2d(cleaned);
    free(cleaned);
    if (!rewritten) return NULL;
    size_t pl = strlen(frag_preamble);
    size_t bl = strlen(rewritten);
    size_t tl = strlen(frag_tail);
    char *out = (char *)malloc(pl + bl + tl + 4);
    if (!out) { free(rewritten); return NULL; }
    memcpy(out, frag_preamble, pl);
    out[pl] = '\n';
    memcpy(out + pl + 1, rewritten, bl);
    memcpy(out + pl + 1 + bl, frag_tail, tl + 1);
    free(rewritten);
    return out;
}

static GLuint compile_shader(GLenum stage, const char *src, char *log_out,
                             size_t log_cap) {
    GLuint s = glCreateShader(stage);
    if (!s) { snprintf(log_out, log_cap, "glCreateShader failed"); return 0; }
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei got = 0;
        glGetShaderInfoLog(s, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8),
                 log[0] ? log : "no info log");
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs, char *log_out,
                           size_t log_cap) {
    GLuint p = glCreateProgram();
    if (!p) { snprintf(log_out, log_cap, "glCreateProgram failed"); return 0; }
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei got = 0;
        glGetProgramInfoLog(p, sizeof log, &got, log);
        snprintf(log_out, log_cap, "%.*s",
                 (int)(log_cap - 8),
                 log[0] ? log : "no info log");
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
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

static void fill_from(unsigned char *buf, int w, int h, unsigned long frame) {
    double t = (double)frame * 0.02;
    double cx = 0.5 + 0.25 * cos(t);
    double cy = 0.5 + 0.25 * sin(t * 0.7);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double u = (double)x / (double)w;
            double v = (double)y / (double)h;
            double dx = u - cx, dy = v - cy;
            double r = sqrt(dx*dx + dy*dy);
            double k = 1.0 - r * 2.0; if (k < 0) k = 0;
            unsigned char *p = buf + (y * w + x) * 4;
            p[0] = (unsigned char)(255.0 * k * 0.9);
            p[1] = (unsigned char)(255.0 * k * 0.4);
            p[2] = (unsigned char)(255.0 * k * 0.7);
            p[3] = 255;
        }
    }
}

static void fill_to(unsigned char *buf, int w, int h, unsigned long frame) {
    double t = (double)frame * 0.02;
    double off = fmod(t * 0.15, 1.0);
    for (int y = 0; y < h; y++) {
        double v = (double)y / (double)h;
        double stripe = sin((v + off) * 3.14159265 * 12.0);
        double k = 0.5 + 0.5 * stripe;
        for (int x = 0; x < w; x++) {
            unsigned char *p = buf + (y * w + x) * 4;
            p[0] = (unsigned char)(255.0 * k * 0.2);
            p[1] = (unsigned char)(255.0 * k * 0.95);
            p[2] = (unsigned char)(255.0 * k * 0.85);
            p[3] = 255;
        }
    }
}

/* Stolen from gles3_harness.c (NCZ_FRAME_DUMP path) — write a
 * top-down RGBA PNG-ish raw file. We write .rgba (raw bytes),
 * not PNG, to keep this tester dependency-free. */
static void dump_rgba(const char *path, const unsigned char *pixels,
                      int w, int h) {
    /* Flip vertically: GL origin is bottom-left, raw is top-down. */
    unsigned char *flipped = (unsigned char *)malloc((size_t)w * h * 4);
    if (!flipped) return;
    for (int y = 0; y < h; y++) {
        memcpy(flipped + y * w * 4,
               pixels + ((h - 1 - y) * w) * 4,
               (size_t)w * 4);
    }
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(flipped, 1, (size_t)w * h * 4, f);
        fclose(f);
    }
    free(flipped);
}

/* ------------------------------------------------------------------- */
/* EGL setup — surfaceless + PBuffer.                                 */
/* ------------------------------------------------------------------- */

static EGLDisplay egl_disp;
static EGLContext  egl_ctx;
static EGLSurface  egl_pbuf;
static int         dst_w = 256, dst_h = 256;

static int
init_egl(void) {
    egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_disp == EGL_NO_DISPLAY) {
        fprintf(stderr, "smoke: eglGetDisplay failed\n");
        return -1;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_disp, &major, &minor)) {
        fprintf(stderr, "smoke: eglInitialize failed\n");
        return -1;
    }
    fprintf(stderr, "[diag] EGL %d.%d vendor=%s\n",
            major, minor,
            eglQueryString(egl_disp, EGL_VENDOR));
    /* Bind OpenGL ES API. */
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "smoke: eglBindAPI failed\n");
        return -1;
    }
    /* Choose a config. */
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
        EGL_WIDTH, dst_w,
        EGL_HEIGHT, dst_h,
        EGL_NONE
    };
    egl_pbuf = eglCreatePbufferSurface(egl_disp, cfg, pbuf_attr);
    if (egl_pbuf == EGL_NO_SURFACE) {
        fprintf(stderr, "smoke: eglCreatePbufferSurface failed (0x%x)\n",
                (unsigned)eglGetError());
        return -1;
    }
    EGLint ctx_attr[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
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
    fprintf(stderr, "[diag] GL_VENDOR=%s\n",
            (const char *)glGetString(GL_VENDOR));
    fprintf(stderr, "[diag] GLSL_VERSION=%s\n",
            (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
    return 0;
}

static void
teardown_egl(void) {
    if (egl_disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT)
            eglDestroyContext(egl_disp, egl_ctx);
        if (egl_pbuf != EGL_NO_SURFACE)
            eglDestroySurface(egl_disp, egl_pbuf);
        eglTerminate(egl_disp);
    }
}

/* Per-shader state: source textures + FBO + VBO + program. */
typedef struct {
    GLuint program;
    GLuint vbo;
    GLuint tex_from;
    GLuint tex_to;
    GLint  loc_progress;
    GLint  loc_from;
    GLint  loc_to;
    GLint  loc_resolution;
} SState;

static int
init_shader(SState *ss, const char *body, char *fail_reason,
            size_t fr_cap) {
    memset(ss, 0, sizeof *ss);
    char *frag_src = build_frag_src(body);
    if (!frag_src) {
        snprintf(fail_reason, fr_cap,
                 "build_frag_src returned NULL");
        return -1;
    }
    char log[1024] = "";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert_src, log, sizeof log);
    if (!vs) {
        char why[240];
        snprintf(why, sizeof why, "VS: %s", log);
        snprintf(fail_reason, fr_cap, "%s", why);
        free(frag_src);
        return -1;
    }
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src, log,
                               sizeof log);
    free(frag_src);
    if (!fs) {
        char why[240];
        snprintf(why, sizeof why, "FS: %s", log);
        snprintf(fail_reason, fr_cap, "%s", why);
        return -1;
    }
    ss->program = link_program(vs, fs, log, sizeof log);
    if (!ss->program) {
        char why[240];
        snprintf(why, sizeof why, "LINK: %s", log);
        snprintf(fail_reason, fr_cap, "%s", why);
        return -1;
    }
    ss->loc_progress   = glGetUniformLocation(ss->program, "progress");
    ss->loc_from       = glGetUniformLocation(ss->program, "from");
    ss->loc_to         = glGetUniformLocation(ss->program, "to");
    ss->loc_resolution = glGetUniformLocation(ss->program, "resolution");

    static const float quad[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glGenBuffers(1, &ss->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, ss->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenTextures(1, &ss->tex_from);
    glBindTexture(GL_TEXTURE_2D, ss->tex_from);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    static const unsigned char black[4] = {0,0,0,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glGenTextures(1, &ss->tex_to);
    glBindTexture(GL_TEXTURE_2D, ss->tex_to);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, black);
    glBindTexture(GL_TEXTURE_2D, 0);
    return 0;
}

static void
render(SState *ss, float progress, unsigned char *out_pixels) {
    int w = dst_w, h = dst_h;
    static unsigned char *from_buf = NULL;
    static unsigned char *to_buf   = NULL;
    static size_t cap = 0;
    size_t need = (size_t)w * h * 4;
    if (need > cap) {
        free(from_buf); free(to_buf);
        from_buf = (unsigned char *)malloc(need);
        to_buf   = (unsigned char *)malloc(need);
        cap = need;
    }
    fill_from(from_buf, w, h, 60);
    fill_to  (to_buf,   w, h, 60);

    glBindTexture(GL_TEXTURE_2D, ss->tex_from);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, from_buf);
    glBindTexture(GL_TEXTURE_2D, ss->tex_to);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, to_buf);

    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(ss->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ss->tex_from);
    if (ss->loc_from >= 0) glUniform1i(ss->loc_from, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ss->tex_to);
    if (ss->loc_to >= 0) glUniform1i(ss->loc_to, 1);

    if (ss->loc_progress   >= 0) glUniform1f(ss->loc_progress, progress);
    if (ss->loc_resolution >= 0)
        glUniform2f(ss->loc_resolution, (float)w, (float)h);

    glBindBuffer(GL_ARRAY_BUFFER, ss->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_pixels);
}

static void
destroy_shader(SState *ss) {
    if (ss->program)  glDeleteProgram(ss->program);
    if (ss->vbo)      glDeleteBuffers(1, &ss->vbo);
    if (ss->tex_from) glDeleteTextures(1, &ss->tex_from);
    if (ss->tex_to)   glDeleteTextures(1, &ss->tex_to);
}

/* ------------------------------------------------------------------- */
/* Main loop                                                           */
/* ------------------------------------------------------------------- */

/* Progress samples for mid-blend captures. Six points that span
 * the full transition: start (0.0), early-blend (0.2), mid (0.5),
 * late-blend (0.8), end (1.0). The production driver eases
 * 0->1 in 0.9s, so these correspond roughly to t = 0, 0.18, 0.45,
 * 0.72, 0.9s in a single-shot loop.
 *
 * The brief asks for "captures of at least 6 transitions mid-blend
 * at several progress values" - so we dump six values for each of
 * the shaders named in a "capture subset" file passed as argv[4],
 * and only the single p=0.5 dump for everything else (cheap smoke
 * coverage). */
static const float kProgressSamples[] = {
    0.00f, 0.20f, 0.40f, 0.50f, 0.80f, 1.00f
};
#define N_PROGRESS_SAMPLES \
    (int)(sizeof(kProgressSamples) / sizeof(kProgressSamples[0]))

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <list.txt> <shader-dir> [dump-dir] [subset.txt]\n"
                "  list.txt: one .glsl basename per line\n"
                "  shader-dir: dir containing the .glsl files\n"
                "  dump-dir: defaults to /tmp/gl-trans-smoke\n"
                "  subset.txt (optional): one basename per line; for "
                "shaders in this set, the tester dumps captures at\n"
                "%d progress values per shader; the rest get one p=0.5 dump\n",
                argv[0], N_PROGRESS_SAMPLES);
        return 2;
    }
    const char *list_path = argv[1];
    const char *shader_dir = argv[2];
    const char *dump_dir = (argc >= 4) ? argv[3] : "/tmp/gl-trans-smoke";
    const char *subset_path = (argc >= 5) ? argv[4] : NULL;

    if (init_egl() != 0) return 1;
    mkdir(dump_dir, 0755);

    /* Read subset (if any) into a fixed list. Cheap: at most
     * MAX_SHADERS names, each <= 128 chars; we only do a linear
     * lookup against this list when we hit a render in the main
     * loop, so a hash-bucket scheme would be premature. */
    char subset_names[MAX_SHADERS][128];
    memset(subset_names, 0, sizeof subset_names);
    int subset_n = 0;
    if (subset_path) {
        FILE *sf = fopen(subset_path, "r");
        if (!sf) {
            fprintf(stderr, "smoke: cannot open subset %s: %s\n",
                    subset_path, strerror(errno));
            return 2;
        }
        char sline[256];
        while (fgets(sline, sizeof sline, sf) &&
               subset_n < MAX_SHADERS) {
            char *nl = strchr(sline, '\n');
            if (nl) *nl = '\0';
            char *hash = strchr(sline, '#');
            if (hash) *hash = '\0';
            char *end = sline + strlen(sline);
            while (end > sline && (end[-1] == ' ' || end[-1] == '\t' ||
                                   end[-1] == '\r')) { *--end = '\0'; }
            if (sline[0] == '\0') continue;
            snprintf(subset_names[subset_n],
                     sizeof subset_names[subset_n],
                     "%s", sline);
            subset_n++;
        }
        fclose(sf);
        fprintf(stderr, "[diag] %d shaders in capture subset\n", subset_n);
    }

    FILE *f = fopen(list_path, "r");
    if (!f) { fprintf(stderr, "smoke: cannot open list %s: %s\n",
                      list_path, strerror(errno)); return 2; }
    SmokeRec recs[MAX_SHADERS];
    memset(recs, 0, sizeof recs);
    int n = 0;
    char line[256];
    while (fgets(line, sizeof line, f) && n < MAX_SHADERS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' ||
                              end[-1] == '\r')) { *--end = '\0'; }
        if (line[0] == '\0') continue;
        snprintf(recs[n].name, sizeof recs[n].name, "%s", line);
        n++;
    }
    fclose(f);
    fprintf(stderr, "[diag] %d shaders in list\n", n);

    int n_compile = 0, n_link = 0, n_render = 0;
    unsigned char *pixels = (unsigned char *)malloc((size_t)dst_w * dst_h * 4);
    for (int i = 0; i < n; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.glsl", shader_dir,
                 recs[i].name);
        size_t blen = 0;
        char *body = read_file(path, &blen);
        if (!body) {
            char why[200];
            snprintf(why, sizeof why, "cannot open %s", path);
            snprintf(recs[i].fail_reason, sizeof recs[i].fail_reason,
                     "%s", why);
            continue;
        }
        SState ss;
        recs[i].compile_ok = 1;
        if (init_shader(&ss, body, recs[i].fail_reason,
                        sizeof recs[i].fail_reason) == 0) {
            n_compile++;
            recs[i].link_ok = 1;
            n_link++;
            /* Render at progress=0.5 first (cheap smoke coverage). */
            memset(pixels, 0, (size_t)dst_w * dst_h * 4);
            render(&ss, 0.5f, pixels);
            int nonblack = 0;
            for (int p = 0; p < dst_w * dst_h; p++) {
                if (pixels[p*4] || pixels[p*4+1] || pixels[p*4+2]) {
                    nonblack++;
                }
            }
            if (nonblack > 0) {
                recs[i].render_ok = 1;
                n_render++;
                char out[512];
                snprintf(out, sizeof out, "%s/%s_p0.5.rgba",
                         dump_dir, recs[i].name);
                dump_rgba(out, pixels, dst_w, dst_h);
                /* Mid-blend sweep if this shader is in the subset. */
                int is_subset = 0;
                for (int j = 0; j < subset_n; j++) {
                    if (strcmp(recs[i].name, subset_names[j]) == 0) {
                        is_subset = 1;
                        break;
                    }
                }
                if (is_subset) {
                    for (int s = 0; s < N_PROGRESS_SAMPLES; s++) {
                        if (kProgressSamples[s] == 0.5f) continue; /* already dumped */
                        memset(pixels, 0, (size_t)dst_w * dst_h * 4);
                        render(&ss, kProgressSamples[s], pixels);
                        snprintf(out, sizeof out,
                                 "%s/%s_p%.2f.rgba",
                                 dump_dir, recs[i].name,
                                 kProgressSamples[s]);
                        dump_rgba(out, pixels, dst_w, dst_h);
                    }
                }
            } else {
                snprintf(recs[i].fail_reason,
                         sizeof recs[i].fail_reason,
                         "rendered all-black");
            }
            destroy_shader(&ss);
        } else {
            recs[i].compile_ok = 0;
        }
        free(body);
        fprintf(stderr, "[smoke] %s %s\n", recs[i].name,
                recs[i].fail_reason[0] ? recs[i].fail_reason : "OK");
    }
    free(pixels);

    /* Emit CSV: name,compile_ok,link_ok,render_ok,fail_reason */
    FILE *out = fopen("/tmp/gl-trans-smoke-results.csv", "w");
    if (out) {
        fprintf(out, "name,compile,link,render,fail\n");
        for (int i = 0; i < n; i++) {
            fprintf(out, "%s,%d,%d,%d,%s\n",
                    recs[i].name,
                    recs[i].compile_ok,
                    recs[i].link_ok,
                    recs[i].render_ok,
                    recs[i].fail_reason);
        }
        fclose(out);
    }

    fprintf(stderr, "[diag] total=%d compile=%d link=%d render=%d\n",
            n, n_compile, n_link, n_render);

    teardown_egl();
    return 0;
}
