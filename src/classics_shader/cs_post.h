/* cs_post.h - optional post-processing for the enhanced style of the
 * shader-engine classics: scene FBO, quarter-resolution separable glow,
 * FXAA-lite edge smoothing, vignette and dither in one composite pass.
 *
 * Budget: on the enhanced style the cost is one full-size composite read plus
 * three quarter-size passes; setting glow to 0 skips the glow passes, and the
 * whole module is bypassed (scene renders straight to the window) when the
 * hack does not need it.  All programs are built once at init.
 */
#ifndef CS_POST_H
#define CS_POST_H
#include "cs_common.h"

typedef struct {
    GLuint fbo, tex, rbdepth;
    GLuint bfbo[2], btex[2];
    GLuint prog_down, prog_blur, prog_comp;
    GLuint vao;
    int w, h, bw, bh, hdr, depth;
    GLint u_blur_dir, u_comp_glow, u_comp_exp, u_comp_vig, u_comp_aa, u_comp_res, u_comp_tone;
    int ok;
} cs_post;

static const char *CS_POST_DOWN = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform sampler2D uTex; uniform vec2 uTexel;
    void main() {
        vec3 c = texture(uTex, vUv + uTexel * vec2(-1.0, -1.0)).rgb + texture(uTex, vUv + uTexel * vec2(1.0, -1.0)).rgb
               + texture(uTex, vUv + uTexel * vec2(-1.0, 1.0)).rgb + texture(uTex, vUv + uTexel * vec2(1.0, 1.0)).rgb;
        c *= 0.25;
        float l = max(c.r, max(c.g, c.b));
        o = vec4(c * smoothstep(0.35, 0.9, l), 1.0);
    });
static const char *CS_POST_BLUR = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform sampler2D uTex; uniform vec2 uDir;
    void main() {
        vec3 c = texture(uTex, vUv).rgb * 0.2270270270;
        c += texture(uTex, vUv + uDir * 1.3846153846).rgb * 0.3162162162;
        c += texture(uTex, vUv - uDir * 1.3846153846).rgb * 0.3162162162;
        c += texture(uTex, vUv + uDir * 3.2307692308).rgb * 0.0702702703;
        c += texture(uTex, vUv - uDir * 3.2307692308).rgb * 0.0702702703;
        o = vec4(c, 1.0);
    });
static const char *CS_POST_COMP = CS_GLSL(
    in vec2 vUv; out vec4 o; uniform sampler2D uScene; uniform sampler2D uGlow;
    uniform vec2 uRes; uniform float uGlowAmt, uExposure, uVig, uAA, uTone;
    float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
    void main() {
        vec2 px = 1.0 / uRes;
        vec3 c = texture(uScene, vUv).rgb;
        if (uAA > 0.5) {
            float lM = luma(c);
            float lN = luma(texture(uScene, vUv + vec2(0.0, px.y)).rgb), lS = luma(texture(uScene, vUv - vec2(0.0, px.y)).rgb);
            float lE = luma(texture(uScene, vUv + vec2(px.x, 0.0)).rgb), lW = luma(texture(uScene, vUv - vec2(px.x, 0.0)).rgb);
            float lo = min(lM, min(min(lN, lS), min(lE, lW))), hi = max(lM, max(max(lN, lS), max(lE, lW)));
            if (hi - lo > max(0.06, hi * 0.125)) {
                vec2 dir = vec2(lS - lN, lE - lW);
                float red = max((lN + lS + lE + lW) * 0.03125, 1.0 / 128.0);
                dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + red), -4.0, 4.0) * px;
                vec3 a = 0.5 * (texture(uScene, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uScene, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);
                vec3 b = a * 0.5 + 0.25 * (texture(uScene, vUv + dir * -0.5).rgb + texture(uScene, vUv + dir * 0.5).rgb);
                float lb = luma(b);
                c = (lb < lo || lb > hi) ? a : b;
            }
        }
        c += texture(uGlow, vUv).rgb * uGlowAmt;
        c *= uExposure;
        if (uTone > 0.5) c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
        vec2 q = vUv - 0.5;
        c *= 1.0 - uVig * dot(q, q) * 1.6;
        c += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
        o = vec4(c, 1.0);
    });

/* NOTE: never call plain glDrawArrays here: this file is only compiled
 * into the GLES3 harness, whose draw path is the ncz_* VBO helpers. */
static int cs_has_ext(const char *name) {
    const char *e = (const char *)glGetString(GL_EXTENSIONS);
    return e && strstr(e, name) != NULL;
}
static void cs_post_alloc(cs_post *p) {
    GLenum ifmt = p->hdr ? GL_RGBA16F : GL_RGBA8, type = p->hdr ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, p->w, p->h, 0, GL_RGBA, type, NULL);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, p->btex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, p->bw, p->bh, 0, GL_RGBA, type, NULL);
    }
    if (p->depth) {
        glBindRenderbuffer(GL_RENDERBUFFER, p->rbdepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, p->w, p->h);
    }
}
static void cs_tex_params(GLuint t) {
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
/* Returns 1 on success.  On failure p->ok == 0 and the caller renders direct. */
static int cs_post_init(cs_post *p, int w, int h, int want_depth) {
    memset(p, 0, sizeof *p);
    p->w = w; p->h = h; p->bw = (w + 3) / 4; p->bh = (h + 3) / 4; p->depth = want_depth;
    p->hdr = cs_has_ext("GL_EXT_color_buffer_half_float") || cs_has_ext("GL_EXT_color_buffer_float");
    glGenTextures(1, &p->tex); glGenTextures(2, p->btex);
    cs_tex_params(p->tex); cs_tex_params(p->btex[0]); cs_tex_params(p->btex[1]);
    if (want_depth) glGenRenderbuffers(1, &p->rbdepth);
    cs_post_alloc(p);
    glGenFramebuffers(1, &p->fbo); glGenFramebuffers(2, p->bfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
    if (want_depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p->rbdepth);
    int good = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!good && p->hdr) {            /* half float not renderable here: retry 8 bit */
        p->hdr = 0; cs_post_alloc(p);
        good = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, p->bfbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->btex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) good = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p->prog_down = cs_program(CS_FULLSCREEN_VS, CS_POST_DOWN, "post down");
    p->prog_blur = cs_program(CS_FULLSCREEN_VS, CS_POST_BLUR, "post blur");
    p->prog_comp = cs_program(CS_FULLSCREEN_VS, CS_POST_COMP, "post composite");
    glGenVertexArrays(1, &p->vao);
    p->ok = good && p->prog_down && p->prog_blur && p->prog_comp;
    if (!p->ok) fprintf(stderr, "[classics] post-processing unavailable (fbo %d), rendering direct\n", good);
    return p->ok;
}
static void cs_post_resize(cs_post *p, int w, int h) {
    if (!p->ok || (w == p->w && h == p->h)) return;
    p->w = w; p->h = h; p->bw = (w + 3) / 4; p->bh = (h + 3) / 4;
    cs_post_alloc(p);
    GLuint f[3] = { p->fbo, p->bfbo[0], p->bfbo[1] };
    for (int i = 0; i < 3; i++) {                       /* a resize that leaves an FBO incomplete must not go on drawing black */
        glBindFramebuffer(GL_FRAMEBUFFER, f[i]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) p->ok = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void cs_post_begin(cs_post *p) {
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glViewport(0, 0, p->w, p->h);
    cs_gl_check("post begin");
}
/* glow: strength (0 = skip glow passes), exposure, vignette 0..1, aa 0/1, tone 0/1 */
static void cs_post_end(cs_post *p, float glow, float exposure, float vig, int aa, int tone) {
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(p->vao);
    if (glow > 0.001f) {
        glViewport(0, 0, p->bw, p->bh);
        glBindFramebuffer(GL_FRAMEBUFFER, p->bfbo[0]);
        glUseProgram(p->prog_down);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, p->tex);
        glUniform1i(glGetUniformLocation(p->prog_down, "uTex"), 0);
        glUniform2f(glGetUniformLocation(p->prog_down, "uTexel"), 1.0f / p->w, 1.0f / p->h);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
        glUseProgram(p->prog_blur);
        glUniform1i(glGetUniformLocation(p->prog_blur, "uTex"), 0);
        GLint ud = glGetUniformLocation(p->prog_blur, "uDir");
        glBindFramebuffer(GL_FRAMEBUFFER, p->bfbo[1]);
        glBindTexture(GL_TEXTURE_2D, p->btex[0]);
        glUniform2f(ud, 1.0f / p->bw, 0);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, p->bfbo[0]);
        glBindTexture(GL_TEXTURE_2D, p->btex[1]);
        glUniform2f(ud, 0, 1.0f / p->bh);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, p->w, p->h);
    glUseProgram(p->prog_comp);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, p->tex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, p->btex[0]);
    glUniform1i(glGetUniformLocation(p->prog_comp, "uScene"), 0);
    glUniform1i(glGetUniformLocation(p->prog_comp, "uGlow"), 1);
    glUniform2f(glGetUniformLocation(p->prog_comp, "uRes"), (float)p->w, (float)p->h);
    glUniform1f(glGetUniformLocation(p->prog_comp, "uGlowAmt"), glow > 0.001f ? glow : 0.0f);
    glUniform1f(glGetUniformLocation(p->prog_comp, "uExposure"), exposure);
    glUniform1f(glGetUniformLocation(p->prog_comp, "uVig"), vig);
    glUniform1f(glGetUniformLocation(p->prog_comp, "uAA"), aa ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(p->prog_comp, "uTone"), tone ? 1.0f : 0.0f);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    cs_gl_check("post composite");
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
}
static void cs_post_free(cs_post *p) {
    if (!p->fbo) return;
    glDeleteFramebuffers(1, &p->fbo); glDeleteFramebuffers(2, p->bfbo);
    glDeleteTextures(1, &p->tex); glDeleteTextures(2, p->btex);
    if (p->rbdepth) glDeleteRenderbuffers(1, &p->rbdepth);
    glDeleteProgram(p->prog_down); glDeleteProgram(p->prog_blur); glDeleteProgram(p->prog_comp);
    glDeleteVertexArrays(1, &p->vao);
}
#endif
