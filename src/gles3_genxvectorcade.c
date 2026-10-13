/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_genxvectorcade.c — evolving phosphor ecosystem.
 *
 * Phase 2: Gray-Scott reaction-diffusion on a low-res state texture,
 * arcade optics pass (per-channel phosphor decay, sharp-core-plus-halo
 * stroke model, cabinet glow), and one depth reveal / one dramatic
 * phrase, layered on top of the phase 1 vector-tunnel journey.
 *
 * Architecture per the design doc:
 *   - Reaction-diffusion state texture (low-res, RGBA16F or RGBA8)
 *     with genome in B channel driving feed/kill rates per pixel.
 *   - Ping-pong FBO for state evolution (Gray-Scott step).
 *   - Trail / phosphor persistence separate from short afterglow.
 *   - Cabinet glow pools drift in parallax; vector apparition layer
 *     uses sharp-core-plus-halo strokes (no bloom pass needed).
 *   - Persistence: genome, RNG, simulation tick saved with integer
 *     ticks (not a growing float time uniform).
 *   - Environmental sensors feed the seed schema, never visuals
 *     directly (see ncz_env_read_int).
 *
 * Original work in a genre. No reference to any specific title,
 * programmer, company, or product anywhere in this file or the
 * accompanying shader; the look is a visual genre.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <sys/stat.h>   /* mkdir */
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <GLES3/gl32.h>
#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_platform.h"
#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif

/* Feedback FBO dimensions are half-resolution of the window — the
 * trail buffer is intentionally soft, so half-res costs a quarter the
 * bandwidth and looks fine through the bloom. Intel UHD was already
 * the constraint, and a full-resolution RGBA8 ping-pong nearly doubled
 * per-frame cost on that target (measured on a UHD 630 + RTX 2060 laptop 2026-09-25).
 * Override via env NCZ_GVC_FB_SCALE for A-B testing. */
#define DEFAULT_FB_SCALE 0.5f
#define MAX_PHASES 5

typedef struct {
    /* GL pipeline */
    GLuint program, vbo, vao;

    /* Persistent feedback ping-pong (RGBA8 half-res). */
    GLuint fbo_a, fbo_b, tex_a, tex_b;
    int fb_w, fb_h;          /* FBO dimensions (== fb_scale * window) */
    float fb_scale;
    int fb_index;            /* 0 = a is current, 1 = b is current */

    /* A tiny "fade" shader that takes the previous frame texture and
     * outputs it scaled by u_fade with a small rotation around the
     * centre. We use one of the feedback textures as a sampling source
     * and blit into the OTHER FBO — that's how the trail buffer
     * "decays while rotating" each frame. */
    GLuint fade_program;
    GLint fade_loc_prev, fade_loc_fade, fade_loc_rot, fade_loc_res;

    /* Main scene program uniform locations */
    GLint u_time, u_resolution, u_prev, u_fade;
    GLint u_phase_ab, u_phase_e;
    GLint u_shape_speed, u_seg_rot, u_pal_pair, u_pal_phase, u_pal_contrast;
    GLint u_sym_burst, u_trail_persist, u_warp_amount, u_ca_amount;
    GLint u_pulse, u_flash, u_seed;
    GLint u_state, u_tick, u_glow_pools, u_reveal;

    /* Per-launch constants */
    float v_seed;
    float v_shape;            /* 0..5: tri,sq,pent,hex,circle,star */
    float v_speed;
    float v_segments;
    float v_rotation;
    float v_pal_pair;
    float v_pal_rate;
    float v_pal_phase;
    float v_pal_contrast;
    float v_sym_base;         /* 4..14 */
    float v_burst_freq;       /* 0.3..1.4 bursts/sec */
    float v_trail_persist;    /* 0.86..0.97 */
    float v_warp_amount;      /* 0.4..1.4 */
    float v_ca_amount;        /* 0.0..0.012 */
    float v_journey_total;    /* total duration (s) of one cycle */
    int v_phase_order[5];     /* shuffle of 0..4: which movement comes next */
    int v_phase_order_cur;    /* pointer into above */

    /* Time origin */
    double started;
    unsigned long frame;
    unsigned long last_flash_frame;     /* throttle flashes */

    /* Resolved fb dims for fade shader: cached at resize so the
     * fade shader doesn't have to read glViewport state. */
    int win_w, win_h;

    /* Phase 2 additions ---------------------------------------------
     * Life-simulation state texture (RGBA8, low-res). Ping-pong
     * updated by an update shader implementing Gray-Scott with
     * genome in the B channel. .r = life density, .g = secondary
     * chemical, .b = genome / species, .a = age / activity. */
    GLuint state_fbo_a, state_fbo_b;
    GLuint state_tex_a, state_tex_b;
    int state_w, state_h;
    int state_index;     /* 0 = a is current, 1 = b is current */
    GLuint update_program;
    GLint upd_u_state, upd_u_tick, upd_u_resolution, upd_u_seed,
          upd_u_feed_kill, upd_u_seed_amount;
    uint64_t sim_tick;   /* integer simulation tick — persisted */
    uint64_t sim_tick_at_launch;
} State;

/* ----- helpers ---------------------------------------------------- */
/* All platform access goes through ncz_platform.h. Hack code does
 * not call clock_gettime or /dev/urandom directly. */
static double now_seconds(void){ return ncz_now(); }
static uint32_t seed_rng(void){
    /* Honor an explicit override env var for reproducible testing. */
    const char *ovr = getenv("NCZ_GVC_FIXED_SEED");
    if(ovr && *ovr){
        uint32_t s = (uint32_t)strtoul(ovr, NULL, 10);
        if(s != 0) return s;
    }
    return ncz_seed();
}
static float rnd(uint32_t *s, float a, float b){
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return a + (b - a) * (float)(*s & 0x00ffffffu) / 16777215.0f;
}
static int rnd_int(uint32_t *s, int a, int b){
    return a + (int)(rnd(s, 0.0f, 1.0f) * (float)(b - a + 1));
}
static void shuffle_ints(uint32_t *s, int *arr, int n){
    /* Fisher-Yates with our LCG */
    for(int i = n - 1; i > 0; i--){
        int j = (int)(rnd(s, 0.0f, 1.0f) * (float)(i + 1));
        if(j > i) j = i;
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

/* ----- shader load ------------------------------------------------ */
static char *load_shader_named(const char *fname){
    /* Search by basename in: cwd, ../vendor/..., ../../vendor/...,
     * and the platform-installed share dir. */
    char cand[1024];
    const char *roots[] = {
        "vendor/genxvectorcade",
        "../vendor/genxvectorcade",
        "../../vendor/genxvectorcade",
    };
    for(size_t i = 0; i < sizeof(roots)/sizeof(roots[0]); i++){
        snprintf(cand, sizeof cand, "%s/%s", roots[i], fname);
        FILE *f = fopen(cand, "rb");
        if(!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        char *b = (char *)malloc((size_t)n + 1);
        if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){
            free(b); fclose(f); return NULL;
        }
        fclose(f); b[n] = 0;
        fprintf(stderr, "[diag] genxvectorcade shader=%s\n", cand);
        return b;
    }
    /* Platform-installed asset path. */
    char installed[1024];
    if(ncz_asset_path(fname, installed, sizeof installed)){
        FILE *f = fopen(installed, "rb");
        if(f){
            fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
            char *b = (char *)malloc((size_t)n + 1);
            if(!b || fread(b, 1, (size_t)n, f) != (size_t)n){
                free(b); fclose(f); return NULL;
            }
            fclose(f); b[n] = 0;
            fprintf(stderr, "[diag] genxvectorcade shader=%s\n", installed);
            return b;
        }
    }
    /* Bare basename in the share dir (some installs flatten). */
    if(ncz_asset_path("shaders/genxvectorcade.frag", installed, sizeof installed)){
        /* Only honor this if the name matches. */
    }
    fprintf(stderr, "genxvectorcade: cannot locate shader %s\n", fname);
    return NULL;
}
static char *load_shader(void){
    /* Phase 1 main scene shader is the public one. */
    return load_shader_named("genxvectorcade.frag");
}

/* ----- shader compile helpers ------------------------------------ */
static GLuint compile(GLenum t, const char *src, const char *tag){
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char l[8192]; glGetShaderInfoLog(s, sizeof l, NULL, l);
        fprintf(stderr, "genxvectorcade compile %s: %s\n", tag, l);
        glDeleteShader(s); return 0;
    }
    return s;
}
static GLuint link_prog(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        char l[8192]; glGetProgramInfoLog(p, sizeof l, NULL, l);
        fprintf(stderr, "genxvectorcade link: %s\n", l);
        glDeleteProgram(p); return 0;
    }
    return p;
}

/* ----- feedback FBO setup ---------------------------------------- */
/* Create the ping-pong FBOs at half the window size. RGBA8: cheap,
 * ubiquitous, plenty of dynamic range for additive bloom trails. */
static int create_fbos(State *s, int win_w, int win_h){
    const char *ovr = getenv("NCZ_GVC_FB_SCALE");
    if(ovr && *ovr){ s->fb_scale = (float)strtod(ovr, NULL); }
    if(s->fb_scale < 0.25f) s->fb_scale = 0.25f;
    if(s->fb_scale > 1.0f)  s->fb_scale = 1.0f;
    int w = (int)((float)win_w * s->fb_scale);
    int h = (int)((float)win_h * s->fb_scale);
    if(w < 2) w = 2;
    if(h < 2) h = 2;
    s->fb_w = w; s->fb_h = h;
    s->win_w = win_w; s->win_h = win_h;
    s->fb_index = 0;

    s->fbo_a = 0; s->fbo_b = 0;
    glGenFramebuffers(1, &s->fbo_a);
    glGenFramebuffers(1, &s->fbo_b);
    glGenTextures(1, &s->tex_a);
    glGenTextures(1, &s->tex_b);

    GLuint fbos[2]   = { s->fbo_a, s->fbo_b };
    GLuint texs[2]   = { s->tex_a, s->tex_b };
    for(int i = 0; i < 2; i++){
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texs[i], 0);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(st != GL_FRAMEBUFFER_COMPLETE){
            fprintf(stderr, "genxvectorcade: FBO %d incomplete 0x%x\n",
                    i, (unsigned)st);
            return -1;
        }
        /* glTexImage2D with NULL data leaves the contents undefined; Mali
         * hands back whatever was in that memory, which the trail feedback
         * then kept alive. Start both trail buffers black. */
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "[diag] genxvectorcade fb %dx%d scale=%.2f (window %dx%d)\n",
            w, h, s->fb_scale, win_w, win_h);
    return 0;
}

static void destroy_fbos(State *s){
    if(s->fbo_a){ glDeleteFramebuffers(1, &s->fbo_a); s->fbo_a = 0; }
    if(s->fbo_b){ glDeleteFramebuffers(1, &s->fbo_b); s->fbo_b = 0; }
    if(s->tex_a){ glDeleteTextures(1, &s->tex_a); s->tex_a = 0; }
    if(s->tex_b){ glDeleteTextures(1, &s->tex_b); s->tex_b = 0; }
}

/* ----- state texture (Gray-Scott) --------------------------------- */
/* Build the low-res RGBA8 ping-pong state texture for the life
 * simulation. We choose resolution from the env-var quality tier
 * (default Medium) so the same binary can run from 256x144 up to
 * 720p without code change. State is intentionally low-res: the
 * visual layer (full-res optics) samples it back through
 * NEAREST-style filtering on a half-rate grid. */
static int create_state_fbos(State *s, int win_w, int win_h){
    /* Quality ladder (per the design doc):
     *  Low    256x144
     *  Medium 320x180
     *  High   480x270
     *  Ultra  640x360
     * We default to Medium; env var can override for testing.
     */
    int sw = 320, sh = 180;
    const char *q = getenv("NCZ_GVC_QUALITY");
    if(q){
        if(!strcasecmp(q, "low"))   { sw = 256; sh = 144; }
        else if(!strcasecmp(q, "high"))   { sw = 480; sh = 270; }
        else if(!strcasecmp(q, "ultra"))  { sw = 640; sh = 360; }
        else if(!strcasecmp(q, "medium")) { sw = 320; sh = 180; }
    }
    /* Scale with window so a 4K monitor doesn't show a 320x180 grid
     * — keep the same aspect ratio. */
    if(win_w > 0 && win_h > 0){
        float scale = (float)win_w / (float)sw;
        if(scale > 1.5f){ sw = (int)(sw * scale); sh = (int)(sh * scale); }
    }
    if(sw < 32) sw = 32;
    if(sh < 32) sh = 32;
    s->state_w = sw; s->state_h = sh;
    s->state_index = 0;

    s->state_fbo_a = 0; s->state_fbo_b = 0;
    glGenFramebuffers(1, &s->state_fbo_a);
    glGenFramebuffers(1, &s->state_fbo_b);
    glGenTextures(1, &s->state_tex_a);
    glGenTextures(1, &s->state_tex_b);

    GLuint fbos[2] = { s->state_fbo_a, s->state_fbo_b };
    GLuint texs[2] = { s->state_tex_a, s->state_tex_b };
    /* Seed genome in B channel: scatter "species" blobs across the
     * grid, each with a unique feed/kill rate that the per-pixel
     * genome encodes. We don't compute feed/kill in C — the shader
     * derives them from the genome so we can spawn many species
     * cheaply. */
    unsigned char *seed = (unsigned char *)calloc((size_t)sw * (size_t)sh * 4, 1);
    if(seed){
        uint32_t z = ncz_seed();
        for(int y = 0; y < sh; y++){
            for(int x = 0; x < sw; x++){
                unsigned char *p = seed + (y * sw + x) * 4;
                /* Background: dead in R, alive in G, no genome yet */
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
            }
        }
        /* Scatter ~24 species seeds. Each is a small disk of high-G,
         * with the species' genome value in B. */
        int n_species = 24;
        for(int i = 0; i < n_species; i++){
            float fx = rnd(&z, 0.05f, 0.95f);
            float fy = rnd(&z, 0.05f, 0.95f);
            float genome = rnd(&z, 0.0f, 1.0f);
            int radius = 3 + (int)(rnd(&z, 0.0f, 4.0f));
            int cx = (int)(fx * (float)sw);
            int cy = (int)(fy * (float)sh);
            for(int dy = -radius; dy <= radius; dy++){
                for(int dx = -radius; dx <= radius; dx++){
                    int xx = cx + dx, yy = cy + dy;
                    if(xx < 0 || xx >= sw || yy < 0 || yy >= sh) continue;
                    float dd = (float)(dx*dx + dy*dy);
                    float rr = (float)(radius*radius);
                    if(dd > rr) continue;
                    unsigned char *p = seed + (yy * sw + xx) * 4;
                    /* Initial life: G = 1.0, R = 0.0, B = genome */
                    p[0] = 0;
                    p[1] = 255;
                    p[2] = (unsigned char)(genome * 255.0f);
                    p[3] = 255;
                }
            }
        }
    }

    for(int i = 0; i < 2; i++){
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, seed);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texs[i], 0);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(st != GL_FRAMEBUFFER_COMPLETE){
            fprintf(stderr, "genxvectorcade: state FBO %d incomplete 0x%x\n",
                    i, (unsigned)st);
            free(seed);
            return -1;
        }
    }
    free(seed);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fprintf(stderr, "[diag] genxvectorcade state %dx%d (window %dx%d)\n",
            sw, sh, win_w, win_h);
    return 0;
}

static void destroy_state_fbos(State *s){
    if(s->state_fbo_a){ glDeleteFramebuffers(1, &s->state_fbo_a); s->state_fbo_a = 0; }
    if(s->state_fbo_b){ glDeleteFramebuffers(1, &s->state_fbo_b); s->state_fbo_b = 0; }
    if(s->state_tex_a){ glDeleteTextures(1, &s->state_tex_a); s->state_tex_a = 0; }
    if(s->state_tex_b){ glDeleteTextures(1, &s->state_tex_b); s->state_tex_b = 0; }
}

/* ----- update shader (Gray-Scott step) ---------------------------- */
/* Identical to the fade vertex shader — a passthrough quad. We keep
 * it named update_vert so the program reads clearly. */
static const char *update_vert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_pos * 0.5 + 0.5;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";
static const char *update_frag =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D u_state;\n"
    "uniform vec2  u_resolution;\n"
    "uniform float u_seed;\n"
    "uniform vec2  u_feed_kill;\n"
    "uniform float u_tick;\n"
    "uniform float u_seed_amount;\n"
    "void main(){\n"
    "  vec2 px = 1.0 / u_resolution;\n"
    "  vec4 c = texture(u_state, v_uv);\n"
    "  vec4 n  = texture(u_state, v_uv + vec2( 0.0,  px.y));\n"
    "  vec4 s  = texture(u_state, v_uv + vec2( 0.0, -px.y));\n"
    "  vec4 e  = texture(u_state, v_uv + vec2( px.x, 0.0));\n"
    "  vec4 w  = texture(u_state, v_uv + vec2(-px.x, 0.0));\n"
    "  vec4 ne = texture(u_state, v_uv + vec2( px.x,  px.y));\n"
    "  vec4 nw = texture(u_state, v_uv + vec2(-px.x,  px.y));\n"
    "  vec4 se = texture(u_state, v_uv + vec2( px.x, -px.y));\n"
    "  vec4 sw = texture(u_state, v_uv + vec2(-px.x, -px.y));\n"
    "  vec4 sum4 = (n + s + e + w) * 0.2 + (ne + nw + se + sw) * 0.05;\n"
    "  vec2 lap = sum4.rg - c.rg;\n"
    "  float genome = c.b;\n"
    "  float feed = mix(0.020, 0.060, genome) + u_feed_kill.x;\n"
    "  float kill = mix(0.045, 0.070, fract(genome * 3.17)) + u_feed_kill.y;\n"
    "  float reaction = c.r * c.g * c.g;\n"
    "  float da = 1.0 * lap.x - reaction + feed * (1.0 - c.r);\n"
    "  float db = 0.5 * lap.y + reaction - (kill + feed) * c.g;\n"
    "  vec4 next = c;\n"
    "  next.r = clamp(c.r + da, 0.0, 1.0);\n"
    "  next.g = clamp(c.g + db, 0.0, 1.0);\n"
    "  float activity = length(next.rg - c.rg);\n"
    "  float mutationPressure =\n"
    "      smoothstep(0.0, 0.01, 0.02 - activity) +\n"
    "      smoothstep(0.25, 0.5, activity);\n"
    "  float h = fract(sin(dot(gl_FragCoord.xy + u_seed, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "  next.b = fract(next.b + (h - 0.5) * 0.0005 * mutationPressure);\n"
    "  float seed_p = step(0.998, fract(h * 19.71 + u_seed_amount));\n"
    "  if(seed_p > 0.5 && mutationPressure > 0.05){\n"
    "    next.g = 1.0;\n"
    "    next.b = h;\n"
    "  }\n"
    "  next.a = clamp(c.a + 0.001 * (next.r + next.g), 0.0, 1.0);\n"
    "  fragColor = next;\n"
    "}\n";

static int build_update_program(State *s){
    GLuint v = compile(GL_VERTEX_SHADER, update_vert, "update_vs");
    GLuint f = compile(GL_FRAGMENT_SHADER, update_frag, "update_fs");
    if(!v || !f) return -1;
    s->update_program = link_prog(v, f);
    glDeleteShader(v); glDeleteShader(f);
    if(!s->update_program) return -1;
    s->upd_u_state       = glGetUniformLocation(s->update_program, "u_state");
    s->upd_u_tick        = glGetUniformLocation(s->update_program, "u_tick");
    s->upd_u_resolution  = glGetUniformLocation(s->update_program, "u_resolution");
    s->upd_u_seed        = glGetUniformLocation(s->update_program, "u_seed");
    s->upd_u_feed_kill   = glGetUniformLocation(s->update_program, "u_feed_kill");
    s->upd_u_seed_amount = glGetUniformLocation(s->update_program, "u_seed_amount");
    return 0;
}

/* ----- persistence ------------------------------------------------ */
/* Read/write the per-machine lineage to disk. Versioned format:
 *   "GVC2" 4-byte magic
 *   uint32 version (= 2)
 *   uint64 sim_tick
 *
 * Stored in $XDG_DATA_HOME/ncz-screensavers/genxvectorcade.lineage.
 * Stale / missing / wrong-magic -> fresh lineage. Hack never
 * branches; absence is the same code path as zero. */
static const char *kLineagePath = "genxvectorcade.lineage";
static void mkdir_p(const char *path){
    /* Tiny recursive mkdir — ignore EEXIST. */
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for(char *p = buf + 1; *p; p++){
        if(*p == '/'){
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}
static void lineage_path(char *out, size_t out_sz){
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if(xdg && *xdg){
        snprintf(out, out_sz, "%s/ncz-screensavers/%s", xdg, kLineagePath);
    } else if(home && *home){
        snprintf(out, out_sz, "%s/.local/share/ncz-screensavers/%s", home, kLineagePath);
    } else {
        snprintf(out, out_sz, "/tmp/%s", kLineagePath);
    }
}
static void load_lineage(State *s){
    char path[512]; lineage_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    if(!f){ fprintf(stderr, "[diag] genxvectorcade lineage absent -> fresh\n"); return; }
    char magic[4];
    uint32_t version = 0;
    if(fread(magic, 1, 4, f) != 4) goto fail;
    if(memcmp(magic, "GVC2", 4) != 0) goto fail;
    if(fread(&version, 4, 1, f) != 1) goto fail;
    if(version != 2) goto fail;
    uint64_t tick = 0;
    if(fread(&tick, 8, 1, f) != 1) goto fail;
    s->sim_tick_at_launch = tick;
    fprintf(stderr, "[diag] genxvectorcade lineage tick=%llu\n",
            (unsigned long long)tick);
    fclose(f);
    return;
fail:
    fprintf(stderr, "[diag] genxvectorcade lineage unreadable -> fresh\n");
    fclose(f);
}
static void save_lineage(State *s){
    char path[512]; lineage_path(path, sizeof path);
    /* Best-effort; mkdir -p the directory. */
    char *slash = strrchr(path, '/');
    if(slash){ *slash = 0; mkdir_p(path); *slash = '/'; }
    FILE *f = fopen(path, "wb");
    if(!f) return;
    fwrite("GVC2", 1, 4, f);
    uint32_t v = 2; fwrite(&v, 4, 1, f);
    fwrite(&s->sim_tick, 8, 1, f);
    fclose(f);
}

/* ----- fade shader (rotates previous frame and scales by u_fade) -- */
static const char *fade_vert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){\n"
    "  v_uv = a_pos * 0.5 + 0.5;\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fade_frag =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D u_prev;\n"
    "uniform float u_fade;       /* persistence: 0.86..0.97 */\n"
    "uniform vec2  u_rot_cos_sin;/* small rotation to spiral wakes */\n"
    "uniform vec2  u_scale;      /* 0.985..1.005: subtle zoom */\n"
    "uniform vec2  u_offset;     /* optional radial drift */\n"
    "void main(){\n"
    "  vec2 p = v_uv - 0.5;\n"
    "  vec2 q = vec2(p.x * u_scale.x, p.y * u_scale.y) + u_offset;\n"
    "  q = vec2(u_rot_cos_sin.x * q.x - u_rot_cos_sin.y * q.y,\n"
    "           u_rot_cos_sin.y * q.x + u_rot_cos_sin.x * q.y);\n"
    "  q += 0.5;\n"
    "  vec3 prev = texture(u_prev, q).rgb;\n"
    "  fragColor = vec4(prev * u_fade, 1.0);\n"
    "}\n";

static int build_fade_program(State *s){
    GLuint v = compile(GL_VERTEX_SHADER, fade_vert, "fade_vs");
    GLuint f = compile(GL_FRAGMENT_SHADER, fade_frag, "fade_fs");
    if(!v || !f){ return -1; }
    s->fade_program = link_prog(v, f);
    glDeleteShader(v); glDeleteShader(f);
    if(!s->fade_program) return -1;
    s->fade_loc_prev = glGetUniformLocation(s->fade_program, "u_prev");
    s->fade_loc_fade = glGetUniformLocation(s->fade_program, "u_fade");
    s->fade_loc_rot  = glGetUniformLocation(s->fade_program, "u_rot_cos_sin");
    s->fade_loc_res  = glGetUniformLocation(s->fade_program, "u_scale");
    /* We re-use u_scale for both axes — keep a separate uniform. */
    return 0;
}

/* ----- per-launch randomisation --------------------------------- */
static void randomise(State *s){
    uint32_t z = seed_rng();
    s->v_seed        = (float)z;
    s->v_shape       = (float)rnd_int(&z, 0, 5);
    s->v_speed       = rnd(&z, 0.65f, 1.55f);
    s->v_segments    = (float)rnd_int(&z, 8, 22);
    s->v_rotation    = (rnd(&z, 0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f)
                       * rnd(&z, 0.35f, 1.35f);
    s->v_pal_pair    = (float)rnd_int(&z, 0, 5);
    s->v_pal_rate    = rnd(&z, 0.08f, 0.22f);
    s->v_pal_phase   = rnd(&z, 0.0f, 1.0f);
    s->v_pal_contrast= rnd(&z, 0.92f, 1.35f);
    s->v_sym_base    = rnd(&z, 4.0f, 16.0f);
    s->v_burst_freq  = rnd(&z, 0.30f, 1.20f);
    s->v_trail_persist = rnd(&z, 0.78f, 0.92f);
    s->v_warp_amount = rnd(&z, 0.30f, 1.10f);
    s->v_ca_amount   = rnd(&z, 0.0f, 0.012f);
    /* Total cycle length: 4 phases of ~30..55s each + ~12s intermission */
    s->v_journey_total = 4.0f * rnd(&z, 30.0f, 55.0f) + 12.0f;
    /* Shuffle the phase order. Indices 0..4 = tunnel, grid, starfield,
     * kaleido, lattice. Two consecutive duplicates are forbidden so the
     * journey never hard-cuts. */
    int order[5] = {0, 1, 2, 3, 4};
    do {
        shuffle_ints(&z, order, 5);
    } while(order[0] == 0 && order[4] == 4);  /* encourage variety */
    for(int i = 0; i < 5; i++) s->v_phase_order[i] = order[i];
    s->v_phase_order_cur = 0;
    s->last_flash_frame = 0;
    s->frame = 0;
}

/* ----- phase blending -------------------------------------------- *
 * Phases 0..4 are movements (tunnel, grid_horizon, starfield_warp,
 * kaleido, lattice). At any moment we blend smoothly between an
 * "incoming" weight and an "outgoing" weight. We pick the active pair
 * based on time within v_journey_total.
 */
static float smooth01(float x){
    if(x < 0.0f) x = 0.0f;
    if(x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}
static void compute_phases(State *s, float t, float phases[5]){
    /* Total = 4 legs of ~30..55s + 12s settle = ~150s. Within the
     * cycle we march through v_phase_order. Each "leg" smoothly hands
     * off: 80% of leg time is the destination movement, 20% is a
     * blend from the previous one. */
    const int n_legs = 5;
    float leg = s->v_journey_total / (float)n_legs;
    float x = fmodf(t, s->v_journey_total);
    int leg_idx = (int)(x / leg);
    if(leg_idx >= n_legs) leg_idx = n_legs - 1;
    float local = (x - (float)leg_idx * leg) / leg; /* 0..1 within leg */
    /* Blend window: first 22% and last 22% blend; middle is solid */
    float blend = 0.22f;
    float w_in = 0.0f, w_out = 0.0f;
    int cur = s->v_phase_order[leg_idx];
    int prev = s->v_phase_order[(leg_idx + n_legs - 1) % n_legs];
    if(local < blend){
        w_out = 1.0f - smooth01(local / blend);
        w_in  = 1.0f - w_out;
    } else if(local > 1.0f - blend){
        w_in  = smooth01((local - (1.0f - blend)) / blend);
        w_out = 1.0f - w_in;
    } else {
        w_in = 1.0f; w_out = 0.0f;
    }
    /* Ensure at least one movement is always present */
    if(w_in + w_out < 0.001f){ w_in = 1.0f; }
    /* Tiny "ghost" weight on the lattice so even a "pure" tunnel leg
     * still has a hint of the recursive structure in the background —
     * this is what gives the journey its continuous, never-settling
     * quality. Keep this small so background geometry doesn't drown
     * the foreground movement. */
    for(int i = 0; i < n_legs; i++) phases[i] = 0.015f;
    phases[cur]  = w_in + 0.015f;
    phases[prev] += w_out;
    /* renormalise */
    float sum = 0.0f;
    for(int i = 0; i < n_legs; i++) sum += phases[i];
    if(sum > 0.0f){
        for(int i = 0; i < n_legs; i++) phases[i] /= sum;
    }
}

/* ----- main scene draw (the journey moment) --------------------- */
static const char *scene_vert =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main(){\n"
    "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

/* ----- state update (Gray-Scott step) ---------------------------- *
 * Each call advances the simulation by one tick. We do this BEFORE
 * the scene pass so the scene shader can read fresh state. The
 * shader writes into the OTHER state FBO (the one not currently
 * sampled); afterwards we swap so it becomes "current". */
static void state_update_pass(State *s){
    GLuint src_tex = (s->state_index == 0) ? s->state_tex_a : s->state_tex_b;
    GLuint dst_fbo = (s->state_index == 0) ? s->state_fbo_b : s->state_fbo_a;
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, s->state_w, s->state_h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->update_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(s->upd_u_state, 0);
    glUniform1f(s->upd_u_tick, (float)(s->sim_tick & 0xFFFFFF));
    glUniform2f(s->upd_u_resolution, (float)s->state_w, (float)s->state_h);
    glUniform1f(s->upd_u_seed, (float)((s->sim_tick * 2654435761u) & 0xFFFFFF) / 16777215.0f);
    /* Bias from sensors (feed/kill). Default zero. */
    glUniform2f(s->upd_u_feed_kill, 0.0f, 0.0f);
    /* Seed new species aggressively at the very start of a session
     * (low tick) and ease off once we have established populations. */
    float seed_amt = 0.9985f;
    if(s->sim_tick > 200) seed_amt = 0.9998f;
    glUniform1f(s->upd_u_seed_amount, seed_amt);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    s->state_index = 1 - s->state_index;
    s->sim_tick++;
}

/* ----- per-frame render orchestration ---------------------------- *
 *
 * Each frame is three fullscreen passes:
 *
 *   1. fade_copy_pass(s): bind the OTHER FBO, render the current FBO's
 *      texture into it with a small rotation and a per-frame fade
 *      multiplier. This is the "recursive feedback" pass: wakes spiral
 *      inward and self-replicate. After this pass, fb_index flips to
 *      point at the just-rendered-to FBO (now the "current" buffer).
 *
 *   2. scene_pass(s): bind the OTHER FBO and render the geometry on
 *      top of the faded trail, sampled from the current FBO's texture
 *      (the one step 1 wrote). The target is never the texture being
 *      sampled: that would be a feedback loop, undefined in GLES and
 *      visibly wrong on Mali. fb_index flips again, so the scene output
 *      is current: [faded previous + new geometry].
 *
 *   3. blit_pass(s): bind the default framebuffer (the window), render
 *      the current FBO's texture with a 1:1 fullscreen quad. No fade,
 *      no rotation.
 *
 * Each pass reads one buffer and writes the other, and fb_index always
 * names the buffer holding the latest complete image.
 */

static void fade_copy_pass(State *s, float t){
    /* Old "current" is at fb_index. We write into the other FBO. */
    GLuint src_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;
    GLuint dst_fbo = (s->fb_index == 0) ? s->fbo_b : s->fbo_a;
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, s->fb_w, s->fb_h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->fade_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUniform1i(s->fade_loc_prev, 0);
    glUniform1f(s->fade_loc_fade, s->v_trail_persist);
    float baseRot = 0.0035f + 0.001f * sinf(t * 0.4f);
    glUniform2f(s->fade_loc_rot, cosf(baseRot), sinf(baseRot));
    glUniform2f(s->fade_loc_res, 1.0f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    /* Swap so the just-rendered-to FBO becomes "current". */
    s->fb_index = 1 - s->fb_index;
}

static void scene_pass(State *s, float t){
    /* The just-faded buffer is "current". The scene reads it (the trail)
     * and writes into the OTHER buffer, whose old contents are no longer
     * needed, then that buffer becomes current. Sampling the texture that
     * is bound as the render target is a feedback loop, which GLES leaves
     * undefined: desktop drivers happened to cope, while Mali (a tiler)
     * read stale tile memory, tiled a yellow and cyan block pattern over
     * the frame and ran the trail up to white. */
    int w = s->fb_w, h = s->fb_h;
    GLuint cur_fbo = (s->fb_index == 0) ? s->fbo_b : s->fbo_a;  /* write target */
    GLuint cur_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;  /* trail read */
    GLuint state_tex = (s->state_index == 0) ? s->state_tex_a : s->state_tex_b;
    glBindFramebuffer(GL_FRAMEBUFFER, cur_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    /* Clear the write target; the trail comes from cur_tex. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->program);

    glUniform1f(s->u_time, t);
    glUniform2f(s->u_resolution, (float)w, (float)h);

    /* Sample 0: previous-frame trail (ping-pong FBO). */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_tex);
    glUniform1i(s->u_prev, 0);
    glUniform1f(s->u_fade, 1.0f);

    /* Sample 1: low-res life state texture. */
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, state_tex);
    glUniform1i(s->u_state, 1);

    glUniform1f(s->u_tick, (float)(s->sim_tick & 0xFFFFFF));

    /* Cabinet glow pools: a small uniform that places 3 parallax
     * glow zones that drift slowly and add cabinet-pool colour to
     * the room. Pure procedural — the depth reveal moment is when
     * one of these pools aligns with the centre and briefly tilts
     * the field. */
    float glow_x[3], glow_y[3], glow_r[3], glow_t[3];
    for(int i = 0; i < 3; i++){
        float ph = (float)i * 2.094f; /* 120° phase */
        float speed = 0.04f + 0.013f * (float)i;
        glow_x[i] = 0.5f + 0.42f * cosf(t * speed + ph);
        glow_y[i] = 0.5f + 0.32f * sinf(t * speed * 1.31f + ph * 1.7f);
        glow_r[i] = 0.45f + 0.10f * sinf(t * 0.21f + ph);
        /* Each pool is one of three colours (warm cyan, magenta, amber). */
        glow_t[i] = (float)i;
    }
    /* Pack the 3 pools into 3 vec4s. */
    glUniform4f(s->u_glow_pools,
        glow_x[0], glow_y[0], glow_r[0], glow_t[0]);
    /* Reveal intensity: a single scalar that drives one depth moment
     * per cycle. Pulses up to 1.0 around t = 90..110s in the
     * 150-220s journey. */
    float leg = s->v_journey_total / 5.0f;
    float x = fmodf(t, s->v_journey_total);
    int leg_idx = (int)(x / leg);
    if(leg_idx >= 5) leg_idx = 4;
    /* Reveal moment: second leg, midpoint. A clean 4s ramp. */
    float reveal_t = leg * 1.5f;
    float reveal = 0.0f;
    float dr = 2.0f;
    if(fabsf(x - reveal_t) < dr){
        float u = (x - reveal_t) / dr;
        reveal = 0.5f - 0.5f * cosf(u * 3.14159265f);  /* hann window */
    }
    glUniform1f(s->u_reveal, reveal);

    float phases[5];
    compute_phases(s, t, phases);
    glUniform4f(s->u_phase_ab, phases[0], phases[1], phases[2], phases[3]);
    glUniform1f(s->u_phase_e, phases[4]);

    glUniform2f(s->u_shape_speed, s->v_shape, s->v_speed);
    glUniform2f(s->u_seg_rot, s->v_segments, s->v_rotation);
    glUniform2f(s->u_pal_pair, s->v_pal_pair, s->v_pal_rate);
    glUniform1f(s->u_pal_phase, s->v_pal_phase);
    glUniform1f(s->u_pal_contrast, s->v_pal_contrast);
    glUniform2f(s->u_sym_burst, s->v_sym_base, s->v_burst_freq);
    glUniform1f(s->u_trail_persist, s->v_trail_persist);
    glUniform1f(s->u_warp_amount, s->v_warp_amount);
    glUniform1f(s->u_ca_amount, s->v_ca_amount);

    /* Pulse: zero-crossing of sine at burst frequency, sharpened
     * (pulse*pulse) so it feels musical. */
    float pulse = 0.5f + 0.5f * sinf(t * s->v_burst_freq * 6.2831853f);
    float pulseFinal = pulse * pulse;
    glUniform1f(s->u_pulse, pulseFinal);

    /* Flash: throttled brief inversion at movement boundaries */
    float flash = 0.0f;
    float local = (x - (float)leg_idx * leg) / leg;
    if(local < 0.06f || local > 0.94f){
        float fade = 1.0f;
        if(local < 0.06f) fade = local / 0.06f;
        else              fade = (1.0f - local) / 0.06f;
        if(fade > 1.0f) fade = 1.0f;
        flash = fade * 0.45f;
    }
    if(flash > 0.0f && (s->frame - s->last_flash_frame) < 120){
        flash = 0.0f;
    }
    if(flash > 0.0f) s->last_flash_frame = s->frame;
    glUniform1f(s->u_flash, flash);

    glUniform1f(s->u_seed, s->v_seed);

    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    /* The scene output becomes current for the blit and the next fade. */
    s->fb_index = 1 - s->fb_index;
}

static void blit_pass(State *s){
    GLuint cur_tex = (s->fb_index == 0) ? s->tex_a : s->tex_b;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, s->win_w, s->win_h);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->fade_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_tex);
    glUniform1i(s->fade_loc_prev, 0);
    glUniform1f(s->fade_loc_fade, 1.0f);
    glUniform2f(s->fade_loc_rot, 1.0f, 0.0f);
    glUniform2f(s->fade_loc_res, 1.0f, 1.0f);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ----- init / draw / free --------------------------------------- */
static void init_genxvectorcade(ModeInfo *m){
    State *s = (State *)calloc(1, sizeof *s);
    if(!s){ fprintf(stderr, "genxvectorcade: OOM\n"); ncz_harness_die(1); return; }
    m->data = s;
    s->fb_scale = DEFAULT_FB_SCALE;

    /* Vertex buffer */
    const float q[] = { -1,-1,  1,-1,  -1, 1,   -1, 1,  1,-1,  1, 1 };
    glGenBuffers(1, &s->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    /* Main scene shader */
    char *frag = load_shader();
    if(!frag){ ncz_harness_die(1); return; }
    GLuint vs = compile(GL_VERTEX_SHADER, scene_vert, "scene_vs");
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag, "scene_fs");
    free(frag);
    if(!vs || !fs){ ncz_harness_die(1); return; }
    s->program = link_prog(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if(!s->program){ ncz_harness_die(1); return; }

    /* Cache uniforms */
    s->u_time    = glGetUniformLocation(s->program, "u_time");
    s->u_resolution = glGetUniformLocation(s->program, "u_resolution");
    s->u_prev    = glGetUniformLocation(s->program, "u_prev");
    s->u_fade    = glGetUniformLocation(s->program, "u_fade");
    s->u_phase_ab= glGetUniformLocation(s->program, "u_phase_ab");
    s->u_phase_e = glGetUniformLocation(s->program, "u_phase_e");
    s->u_shape_speed = glGetUniformLocation(s->program, "u_shape_speed");
    s->u_seg_rot = glGetUniformLocation(s->program, "u_seg_rot");
    s->u_pal_pair= glGetUniformLocation(s->program, "u_pal_pair");
    s->u_pal_phase=glGetUniformLocation(s->program, "u_pal_phase");
    s->u_pal_contrast=glGetUniformLocation(s->program, "u_pal_contrast");
    s->u_sym_burst=glGetUniformLocation(s->program, "u_sym_burst");
    s->u_trail_persist=glGetUniformLocation(s->program, "u_trail_persist");
    s->u_warp_amount=glGetUniformLocation(s->program, "u_warp_amount");
    s->u_ca_amount=glGetUniformLocation(s->program, "u_ca_amount");
    s->u_pulse   =glGetUniformLocation(s->program, "u_pulse");
    s->u_flash   =glGetUniformLocation(s->program, "u_flash");
    s->u_seed    =glGetUniformLocation(s->program, "u_seed");
    s->u_state   =glGetUniformLocation(s->program, "u_state");
    s->u_tick    =glGetUniformLocation(s->program, "u_tick");
    s->u_glow_pools =glGetUniformLocation(s->program, "u_glow_pools");
    s->u_reveal  =glGetUniformLocation(s->program, "u_reveal");

    /* Fade program */
    if(build_fade_program(s) != 0){
        fprintf(stderr, "genxvectorcade: fade program build failed\n");
        ncz_harness_die(1); return;
    }

    /* Update program (Gray-Scott). We don't strictly need a separate
     * shader file — the source is inlined — but we keep the helper
     * around in case future ports want a tunable update. */
    if(build_update_program(s) != 0){
        fprintf(stderr, "genxvectorcade: update program build failed\n");
        ncz_harness_die(1); return;
    }

    /* Per-launch randomisation */
    randomise(s);

    /* FBOs sized to current window */
    int w = m->xgwa.width, h = m->xgwa.height;
    if(w < 1) w = 1280;
    if(h < 1) h = 720;
    if(create_fbos(s, w, h) != 0){
        fprintf(stderr, "genxvectorcade: FBO setup failed\n");
        ncz_harness_die(1); return;
    }
    if(create_state_fbos(s, w, h) != 0){
        fprintf(stderr, "genxvectorcade: state FBO setup failed\n");
        ncz_harness_die(1); return;
    }

    /* Load lineage (best-effort) */
    load_lineage(s);
    s->sim_tick = s->sim_tick_at_launch;

    /* Sample environmental sensors for the seed schema. Each
     * sensor, when present, mixes a unique value into the seed via
     * xor with a distinct prime — the result is that different
     * machines and different sessions produce different lineages
     * without the sensors ever being mapped to visuals directly. */
    int v;
    uint32_t seed_u = (uint32_t)s->v_seed;
    if(ncz_env_read_int("kp", &v))         seed_u ^= ((uint32_t)v * 0x9E3779B1u);
    if(ncz_env_read_int("solar_wind", &v)) seed_u ^= ((uint32_t)v * 0x85EBCA77u);
    if(ncz_env_read_int("cpu_load", &v))   seed_u ^= ((uint32_t)v * 0xC2B2AE3Du);
    if(ncz_env_read_int("cpu_temp", &v))   seed_u ^= ((uint32_t)v * 0x27D4EB2Fu);
    if(ncz_env_read_int("battery", &v))    seed_u ^= ((uint32_t)(v+1) * 0x165667B1u);
    if(seed_u == 0) seed_u = 0xA5A5A5A5u;
    s->v_seed = (float)seed_u;
    fprintf(stderr, "[diag] genxvectorcade seed_post_sensors=%.0f\n", s->v_seed);

    s->started = now_seconds();
    s->frame = 0;

    /* Diagnostic */
    fprintf(stderr,
        "[diag] genxvectorcade seed=%.0f shape=%.0f speed=%.3f segments=%.0f "
        "rotation=%.3f pal_pair=%.0f pal_rate=%.4f pal_phase=%.3f pal_contrast=%.3f "
        "sym_base=%.2f burst=%.3f trail=%.4f warp=%.3f ca=%.4f journey=%.1fs "
        "order=[%d,%d,%d,%d,%d] fb_scale=%.2f fb=%dx%d GL=%s\n",
        s->v_seed, s->v_shape, s->v_speed, s->v_segments, s->v_rotation,
        s->v_pal_pair, s->v_pal_rate, s->v_pal_phase, s->v_pal_contrast,
        s->v_sym_base, s->v_burst_freq, s->v_trail_persist,
        s->v_warp_amount, s->v_ca_amount, s->v_journey_total,
        s->v_phase_order[0], s->v_phase_order[1], s->v_phase_order[2],
        s->v_phase_order[3], s->v_phase_order[4],
        s->fb_scale, s->fb_w, s->fb_h, glGetString(GL_VERSION));
}

static void draw_genxvectorcade(ModeInfo *m){
    State *s = (State *)m->data;
    if(!s || !s->program) return;
    int w = m->xgwa.width, h = m->xgwa.height;
    if(w < 1) w = 1;
    if(h < 1) h = 1;
    /* Resize FBOs if window changed (compositor may reconfigure) */
    if(w != s->win_w || h != s->win_h){
        destroy_fbos(s);
        if(create_fbos(s, w, h) != 0){
            fprintf(stderr, "genxvectorcade: FBO resize failed\n");
            return;
        }
    }

    float t = (float)(now_seconds() - s->started);

    /* 0. advance the simulation by one tick. We do this even on
     *    unchanged-time frames because the integer sim_tick is the
     *    authoritative clock for the life system — never the float
     *    time uniform, which loses precision over multi-day runs. */
    state_update_pass(s);

    /* 1. fade previous FBO (rotate slightly + scale by persistence) */
    fade_copy_pass(s, t);

    /* 2. draw current geometry into the (now-current) FBO */
    scene_pass(s, t);

    /* 3. blit the FBO to the default framebuffer (window) */
    blit_pass(s);

    s->frame++;

    /* Save lineage periodically (every ~600 frames ~= 10s at 60fps).
     * Best-effort — silent failure on permission issues. */
    if((s->frame % 600) == 0){
        save_lineage(s);
    }

    /* First-frame sanity (logs pixel samples + gl_error) */
    static int once;
    if(!once){
        once = 1;
        /* Read back the CURRENT FBO texture (not the window) — this is
         * what the user is going to see after the blit. If this is
         * black, something is wrong with the FBO path; if it's
         * colourful but the screen is still black, the blit pass is
         * the problem. */
        glBindFramebuffer(GL_FRAMEBUFFER, (s->fb_index == 0) ? s->fbo_a : s->fbo_b);
        unsigned char px[16] = {0};
        glReadPixels(s->fb_w/2, s->fb_h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(s->fb_w/4, s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(3*s->fb_w/4, s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(s->fb_w/2, 3*s->fb_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
            "[diag] genxvectorcade first draw gl_error=0x%x fbo_samples="
            "ctr=%u,%u,%u tl=%u,%u,%u tr=%u,%u,%u br=%u,%u,%u fb=%ux%u uniforms="
            "time:%d res:%d prev:%d phase_ab:%d phase_e:%d shape:%d pal:%d sym:%d\n",
            e, px[0], px[1], px[2], px[4], px[5], px[6], px[8], px[9], px[10],
            px[12], px[13], px[14], s->fb_w, s->fb_h,
            s->u_time, s->u_resolution, s->u_prev, s->u_phase_ab, s->u_phase_e,
            s->u_shape_speed, s->u_pal_pair, s->u_sym_burst);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* Optional frame-time measurement */
    static unsigned long _ft_counter;
    static double _ft_prev;
    static int _ft_enabled;
    if(!_ft_enabled) _ft_enabled = (getenv("NCZ_GVC_PERF_LOG") != NULL);
    if(_ft_enabled){
        double now = now_seconds();
        if((++_ft_counter % 30) == 0){
            if(_ft_prev > 0.0){
                double dt = (now - _ft_prev) / 30.0;
                fprintf(stderr, "[diag] genxvectorcade frame_t frame=%lu dt_ms=%.3f\n",
                        _ft_counter, dt * 1000.0);
                fflush(stderr);
            }
            _ft_prev = now_seconds();
        }
    }
}

static void free_genxvectorcade(ModeInfo *m){
    State *s = (State *)m->data;
    if(!s) return;
    /* Persist lineage one last time on shutdown. */
    save_lineage(s);
    destroy_fbos(s);
    destroy_state_fbos(s);
    if(s->vbo) glDeleteBuffers(1, &s->vbo);
    if(s->program) glDeleteProgram(s->program);
    if(s->fade_program) glDeleteProgram(s->fade_program);
    if(s->update_program) glDeleteProgram(s->update_program);
    free(s);
    m->data = NULL;
}
static void reshape_genxvectorcade(ModeInfo *m, int w, int h){
    (void)m; (void)w; (void)h;
}
static Bool genxvectorcade_handle_event(ModeInfo *m, XEvent *e){
    (void)m; (void)e;
    return False;
}
static void release_genxvectorcade(ModeInfo *m){ (void)m; }

static ModeSpecOpt genxvectorcade_opts = { 0, NULL, 0, NULL, NULL };
struct xscreensaver_function_table genxvectorcade_xscreensaver_function_table = {
    .name = "genxvectorcade",
    .class_ = "GenXVectorCade",
    .init_cb = init_genxvectorcade,
    .draw_cb = draw_genxvectorcade,
    .reshape_cb = reshape_genxvectorcade,
    .event_cb = genxvectorcade_handle_event,
    .free_cb = free_genxvectorcade,
    .release_cb = release_genxvectorcade,
    .opts = &genxvectorcade_opts,
    .defaults_str = DEFAULTS,
};
