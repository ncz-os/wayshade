/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ncz-screensaver-calibrate - one-shot GPU speed test for the screensaver GPU class.
 *
 * Renders a 160-step bent-ray marcher into a 1920x1080 RGBA8 target (the "heavy raymarch"
 * case of tools/probe/gles_probe.c, which this program is derived from) and prints one JSON
 * line: {"renderer":..., "version":..., "platform":..., "ms":..., "timer":"gpu"|"wall"}.
 * It uses whatever GPU the environment selects (the compositor's, or PRIME/DRI_PRIME when
 * the caller sets them), never a software renderer: exit 3 on llvmpipe/softpipe/swrast.
 * Exit 2 on any other failure. The run takes about 2 seconds; no window is created and
 * nothing is read back.
 *
 * --identify prints only renderer, version and platform (about 0.3 s).
 * Env: NCZ_CALIBRATE_MS (timing budget per measurement, default 600).
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define W 1920
#define H 1080
#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB
#define GL_QUERY_RESULT_EXT 0x8866

static const char *VS =
    "#version 300 es\n"
    "void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2);"
    " gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }\n";

static const char *FS =
    "#version 300 es\nprecision highp float; uniform vec2 r; uniform float t; uniform int steps;"
    " uniform float hz; uniform float rmax; out vec4 o;\n"
    "float h(vec3 p){ p=fract(p*.3183099+.1); p*=17.; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }\n"
    "float n3(vec3 x){ vec3 i=floor(x), f=fract(x); f=f*f*(3.-2.*f);\n"
    "  return mix(mix(mix(h(i),h(i+vec3(1,0,0)),f.x),mix(h(i+vec3(0,1,0)),h(i+vec3(1,1,0)),f.x),f.y),\n"
    "             mix(mix(h(i+vec3(0,0,1)),h(i+vec3(1,0,1)),f.x),mix(h(i+vec3(0,1,1)),h(i+vec3(1,1,1)),f.x),f.y),f.z); }\n"
    "vec3 bb(float T){ T=clamp(T,1000.,12000.)/100.; vec3 c; c.r=T<=66.?1.:clamp(1.29*pow(T-60.,-.133),0.,1.);\n"
    "  c.g=T<=66.?clamp(.39*log(T)-.63,0.,1.):clamp(1.13*pow(T-60.,-.0755),0.,1.);"
    " c.b=T>=66.?1.:T<=19.?0.:clamp(.54*log(T-10.)-1.19,0.,1.); return c; }\n"
    "void main(){\n"
    "  vec2 uv = (gl_FragCoord.xy*2.-r)/r.y;\n"
    "  float a = t*.2; vec3 ro = vec3(9.*sin(a), 1.6, -9.*cos(a));\n"
    "  vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0,1,0), fw)), up = cross(fw, rt);\n"
    "  vec3 v = normalize(fw*1.6 + rt*uv.x + up*uv.y), p = ro; vec3 col = vec3(0); float T = 1.;\n"
    "  for (int i=0;i<steps;i++){\n"
    "    float r2 = dot(p,p), rr = sqrt(r2); if (rr < hz) { T = 0.; break; } if (rr > rmax) break;\n"
    "    float dt = clamp(.06*rr, .02, .8);\n"
    "    v = normalize(v - 1.5*p/(r2*rr)*dt*(1.+1.5/r2)); vec3 q = p + v*dt;\n"
    "    if (p.y*q.y < 0.) { vec3 hp = mix(p,q,p.y/(p.y-q.y)); float d = length(hp.xz);\n"
    "      if (d > 2.6 && d < 12.) { float ang = atan(hp.z,hp.x) + 3.*t/(d*sqrt(d));\n"
    "        vec3 s = vec3(d*1.2, cos(ang)*3., sin(ang)*3.); float nz = 0.;\n"
    "        for (int k=0;k<4;k++){ nz += n3(s*exp2(float(k)))/exp2(float(k)); }\n"
    "        float dop = 1. + .5*sin(ang - atan(ro.z,ro.x));\n"
    "        col += T * bb(2000. + 7000./sqrt(d)*dop) * nz * dop*dop * (1.6/d) * .5; T *= .8; } }\n"
    "    p = q; }\n"
    "  vec3 sky = vec3(pow(h(floor(v*220.)), 60.));\n"
    "  col += T*sky; o = vec4(col,1.); }\n";

/* A 1x1 pass that samples the result so tile-based GPUs cannot discard the work. */
static const char *FS_CONSUME =
    "#version 300 es\nprecision highp float; uniform highp sampler2D t; out vec4 o;\n"
    "void main(){ o = texture(t, vec2(0.5)) + texture(t, vec2(0.25, 0.75)) + texture(t, vec2(0.9, 0.1)); }\n";

typedef void (*pfn_gen_q)(GLsizei, GLuint *);
typedef void (*pfn_q)(GLenum, GLuint);
typedef void (*pfn_end_q)(GLenum);
typedef void (*pfn_get_q)(GLuint, GLenum, GLuint64 *);

static pfn_gen_q p_gen_q;
static pfn_q p_begin_q;
static pfn_end_q p_end_q;
static pfn_get_q p_get_q64;
static int have_timer;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static GLuint prog(const char *fs)
{
    const char *src[2] = {VS, fs};
    GLenum ty[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    GLuint p = glCreateProgram();
    for (int i = 0; i < 2; i++) {
        GLuint s = glCreateShader(ty[i]);
        GLint ok = 0;
        glShaderSource(s, 1, &src[i], NULL);
        glCompileShader(s);
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
            return 0;
        glAttachShader(p, s);
    }
    GLint ok = 0;
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

static GLuint main_prog, cons_prog, main_fbo, main_tex, cons_fbo;
static int strips = 64; /* short jobs for the first, cold frame; resized after it */

static void frame(void)
{
    GLint l;
    glBindFramebuffer(GL_FRAMEBUFFER, main_fbo);
    glViewport(0, 0, W, H);
    glUseProgram(main_prog);
    glUniform2f(glGetUniformLocation(main_prog, "r"), W, H);
    glUniform1f(glGetUniformLocation(main_prog, "t"), 1.3f);
    glUniform1i(glGetUniformLocation(main_prog, "steps"), 160);
    glUniform1f(glGetUniformLocation(main_prog, "hz"), 1.0f);
    glUniform1f(glGetUniformLocation(main_prog, "rmax"), 40.0f);
    GLenum att = GL_COLOR_ATTACHMENT0;
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &att);
    glEnable(GL_SCISSOR_TEST);
    for (int s = 0; s < strips; s++) {
        int y0 = H * s / strips, y1 = H * (s + 1) / strips;
        glScissor(0, y0, W, y1 - y0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, cons_fbo);
    glViewport(0, 0, 1, 1);
    glUseProgram(cons_prog);
    glBindTexture(GL_TEXTURE_2D, main_tex);
    l = glGetUniformLocation(cons_prog, "t");
    glUniform1i(l, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static double time_frames(int n, int *used_timer)
{
    *used_timer = 0;
    if (have_timer) {
        GLuint q;
        GLint dis = 0;
        p_gen_q(1, &q);
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &dis);
        p_begin_q(GL_TIME_ELAPSED_EXT, q);
        for (int i = 0; i < n; i++)
            frame();
        p_end_q(GL_TIME_ELAPSED_EXT);
        glFinish();
        dis = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &dis);
        GLuint64 ns = 0;
        p_get_q64(q, GL_QUERY_RESULT_EXT, &ns);
        if (!dis && ns > 0) {
            *used_timer = 1;
            return ns / 1e6 / n;
        }
    }
    double t0 = now_ms();
    for (int i = 0; i < n; i++)
        frame();
    glFinish();
    return (now_ms() - t0) / n;
}

static EGLDisplay get_display(const char **how)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (getenv("WAYLAND_DISPLAY") && gpd) {
        void *lib = dlopen("libwayland-client.so.0", RTLD_NOW);
        void *(*conn)(const char *) = lib ? dlsym(lib, "wl_display_connect") : NULL;
        void *d = conn ? conn(NULL) : NULL;
        if (d) {
            EGLDisplay dpy = gpd(EGL_PLATFORM_WAYLAND_KHR, d, NULL);
            if (dpy != EGL_NO_DISPLAY) {
                *how = "wayland";
                return dpy;
            }
        }
    }
    if (gpd) {
        EGLDisplay dpy = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
        if (dpy != EGL_NO_DISPLAY) {
            *how = "surfaceless";
            return dpy;
        }
    }
    *how = "default";
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

static void json_str(const char *key, const char *val)
{
    printf("\"%s\":\"", key);
    for (const char *c = val ? val : ""; *c; c++) {
        if (*c == '"' || *c == '\\')
            putchar('\\');
        if ((unsigned char)*c >= 0x20)
            putchar(*c);
    }
    printf("\",");
}

int main(int argc, char **argv)
{
    const char *how = "";
    EGLDisplay dpy = get_display(&how);
    EGLint maj, min;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &maj, &min)) {
        fprintf(stderr, "calibrate: eglInitialize failed\n");
        return 2;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg;
    EGLint nc = 0, ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    if (!eglChooseConfig(dpy, ca, &cfg, 1, &nc) || nc < 1) {
        fprintf(stderr, "calibrate: no GLES3 config\n");
        return 2;
    }
    EGLint cattr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "calibrate: eglCreateContext failed\n");
        return 2;
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        EGLint pa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        EGLSurface s = eglCreatePbufferSurface(dpy, cfg, pa);
        if (s == EGL_NO_SURFACE || !eglMakeCurrent(dpy, s, s, ctx)) {
            fprintf(stderr, "calibrate: eglMakeCurrent failed\n");
            return 2;
        }
    }
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    if (!renderer || strcasestr(renderer, "llvmpipe") || strcasestr(renderer, "softpipe") ||
        strcasestr(renderer, "swrast") || strcasestr(renderer, "software")) {
        fprintf(stderr, "calibrate: software renderer (%s), refusing\n", renderer ? renderer : "?");
        return 3;
    }
    if (argc > 1 && !strcmp(argv[1], "--identify")) {
        printf("{");
        json_str("renderer", renderer);
        json_str("version", version);
        json_str("platform", how);
        printf("\"identify\":true}\n");
        return 0;
    }
    GLint nex = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &nex);
    for (GLint i = 0; i < nex; i++)
    {
        const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
        if (ext && !strcmp(ext, "GL_EXT_disjoint_timer_query"))
            have_timer = 1;
    }
    if (have_timer) {
        p_gen_q = (pfn_gen_q)eglGetProcAddress("glGenQueriesEXT");
        p_begin_q = (pfn_q)eglGetProcAddress("glBeginQueryEXT");
        p_end_q = (pfn_end_q)eglGetProcAddress("glEndQueryEXT");
        p_get_q64 = (pfn_get_q)eglGetProcAddress("glGetQueryObjectui64vEXT");
        have_timer = p_gen_q && p_begin_q && p_end_q && p_get_q64;
    }
    main_prog = prog(FS);
    cons_prog = prog(FS_CONSUME);
    if (!main_prog || !cons_prog) {
        fprintf(stderr, "calibrate: shader build failed\n");
        return 2;
    }
    glGenTextures(1, &main_tex);
    glBindTexture(GL_TEXTURE_2D, main_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &main_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, main_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_tex, 0);
    GLuint t2, ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenTextures(1, &t2);
    glBindTexture(GL_TEXTURE_2D, t2);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
    glGenFramebuffers(1, &cons_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, cons_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t2, 0);
    if (!ok) {
        fprintf(stderr, "calibrate: framebuffer incomplete\n");
        return 2;
    }

    int used;
    double budget = getenv("NCZ_CALIBRATE_MS") ? atof(getenv("NCZ_CALIBRATE_MS")) : 600.0;
    double pred = 0;
    for (int k = 0; k < 2; k++)
        pred = time_frames(1, &used); /* the second run is warm */
    if (pred <= 0)
        pred = 1.0;
    strips = pred > 40.0 ? (int)ceil(pred / 30.0) : 1; /* keep each GPU job short */
    if (strips > 64)
        strips = 64;
    int n = (int)(budget / pred);
    n = n < 3 ? 3 : (n > 300 ? 300 : n);
    double ms = time_frames(n, &used);
    if (!(ms > 0.0)) {
        fprintf(stderr, "calibrate: no usable timing\n");
        return 2;
    }
    printf("{");
    json_str("renderer", renderer);
    json_str("version", version);
    json_str("platform", how);
    printf("\"timer\":\"%s\",\"frames\":%d,\"ms\":%.3f}\n", used ? "gpu" : "wall", n, ms);
    return 0;
}
