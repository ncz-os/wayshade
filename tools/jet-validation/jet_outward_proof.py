#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offscreen EGL/GLES3 proof that both black hole jet poles stream OUTWARD.

The real ``vendor/blackhole/blackhole.frag`` is compiled with its ``main``
renamed, and a diagnostic ``main`` appended that calls the UNMODIFIED
``jet_glow()`` from that same file.  Nothing about the jet maths is
reimplemented here -- only the camera and the sampling live in this tool.

For each jet axis it renders the jet at two fixed times with a fixed seed,
writes the PPM frames, and cross-correlates the per-pole brightness profile
against unsigned height h to measure the drift of the knots.  Outward drift
is positive on both poles.  Run it against the pre-fix shader (negative
control) to see the negative pole drift the other way.

Requires libEGL + libGLESv2 via ctypes (Linux).  No third-party Python deps.
Fails closed: any shader compile/link or GL error aborts with the log.

  # --out-dir is REQUIRED (standing policy: never default test/tool output into
  # /tmp or a stage/tmp dir). Point it at an explicit scratch dir on Projects
  # disk, e.g. $SCRATCH/proj/jetproof where SCRATCH=/home/$USER/Projects.
  tools/jet-validation/jet_outward_proof.py --out-dir "$SCRATCH/proj/jetproof"
  git show <base>:vendor/blackhole/blackhole.frag > "$SCRATCH/proj/old.frag"
  tools/jet-validation/jet_outward_proof.py --shader "$SCRATCH/proj/old.frag" \
      --out-dir "$SCRATCH/proj/old"
"""

import argparse
import ctypes
import hashlib
import json
import math
import os
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

EGL_PLATFORM_SURFACELESS_MESA = 0x31DD
EGL_OPENGL_ES_API = 0x30A0
EGL_SURFACE_TYPE = 0x3033
EGL_PBUFFER_BIT = 0x0001
EGL_RENDERABLE_TYPE = 0x3040
EGL_OPENGL_ES3_BIT = 0x00000040
EGL_NONE = 0x3038
EGL_WIDTH, EGL_HEIGHT = 0x3057, 0x3056
EGL_CONTEXT_CLIENT_VERSION = 0x3098
EGL_VERSION = 0x3054
EGL_EXTENSIONS = 0x3055

GL_COLOR_BUFFER_BIT = 0x4000
GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE = 0x8058, 0x1908, 0x1401
GL_FRAMEBUFFER, GL_RENDERBUFFER = 0x8D40, 0x8D41
GL_FRAMEBUFFER_COMPLETE = 0x8CD5
GL_COLOR_ATTACHMENT0 = 0x8CE0
GL_FRAGMENT_SHADER, GL_VERTEX_SHADER = 0x8B30, 0x8B31
GL_COMPILE_STATUS, GL_LINK_STATUS = 0x8B81, 0x8B82
GL_TEXTURE_2D, GL_TEXTURE0 = 0x0DE1, 0x84C0
GL_TRIANGLES = 0x0004
GL_NO_ERROR = 0
GL_VERSION = 0x1F02
GL_RENDERER = 0x1F01
GL_VENDOR = 0x1F00
GL_SHADING_LANGUAGE_VERSION = 0x8B8C
GL_EXTENSIONS = 0x1F03

W, H = 256, 256
# World units per pixel; a jet up to h = 60 fits in the 256px frame.
SCALE = 0.5
EXPOSURE = 3.0
# Knot drift speed in the shader is 1.6/0.55 world units per second.
T0, T1 = 0.0, 2.0
SEED = 7.0
JET_LEN = 26.0
ENH = 0.0  # u_enh == 0 disables the precession term, so the axis is frozen

AXES = {"world_up": (0.0, 1.0, 0.0), "tilted": (0.6, 0.8, 0.0)}
# The profile is sampled at EXACTLY one world unit of SCALE (= one pixel) per
# step, so a shift reported in samples IS the shift in world units when
# multiplied by SCALE.  Spacing 2.0 world units with a sample-index shift would
# have overstated every drift by 2x -- the bug this spacing pins down.
H_MIN, H_MAX = 8.0, 54.0
H_SAMPLES = [SCALE * i for i in range(
    int(round(H_MIN / SCALE)), int(round(H_MAX / SCALE)) + 1)]
assert all(abs((b - a) - SCALE) < 1e-9
           for a, b in zip(H_SAMPLES, H_SAMPLES[1:])), "H_SAMPLES spacing must be SCALE"
assert H_SAMPLES[0] >= H_MIN - 1e-9 and H_SAMPLES[-1] <= H_MAX + 1e-9

# Bounded physical search window, in world units, converted to samples.
SEARCH_WORLD = 8.0
SEARCH_SAMPLES = int(round(SEARCH_WORLD / SCALE))
# A pole only counts as "measured" if the two profiles actually have structure
# (non-degenerate contrast) and overlap well enough to localise a peak.
MIN_CONTRAST = 2.0e-3  # std of each profile segment, 0..1 luminance units
MIN_OVERLAP_FRAC = 0.5
# Drift the shader is expected to produce, used by --expect-outward.
EXPECTED_WORLD = (1.6 / 0.55) * (T1 - T0)
SHIFT_TOL_WORLD = 1.0


def _norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def _cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _neg(v):
    return (-v[0], -v[1], -v[2])


# ---------------------------------------------------------------------------
# Canonical prototypes.
#
# Every entry point is (name, argtypes, restype) and is bound through _bind()
# before use.  argtypes/restype are NOT optional here: on LP64 (aarch64) ctypes
# defaults every return type to c_int, so a bare `gl.glGenFramebuffers(1)` would
# truncate a 64-bit pointer to 32 bits.  Getting these wrong is what made the
# real offscreen-render run die before the first render.
#
# Signatures are transcribed from the canonical installed headers on this host:
#   /usr/include/EGL/egl.h        (eglDisplay/eglConfig/eglContext are pointers,
#                                  eglQueryString returns const char *)
#   /usr/include/EGL/eglext.h     (eglGetPlatformDisplayEXT takes EGLint*)
#   /usr/include/GLES3/gl3.h      (GLuint/GLenum/GLint/GLsizei widths, and the
#                                  out-pointer forms of glGenFramebuffers,
#                                  glGenRenderbuffers and the 4-arg
#                                  glRenderbufferStorage)
#   /usr/include/GLES2/gl2.h
# ---------------------------------------------------------------------------
EGL_NONE_T = ctypes.c_void_p
P = ctypes.c_void_p         # any pointer / void *
I = ctypes.c_int            # EGLint / GLint / GLsizei / GLenum
U = ctypes.c_uint           # EGLBoolean / GLuint / GLbitfield
F = ctypes.c_float          # GLfloat
S = ctypes.c_char_p         # const char *
B = ctypes.POINTER(ctypes.c_int)        # EGLint *
UP = ctypes.POINTER(ctypes.c_uint)      # GLuint * (glGenFramebuffers, etc.)
VP = ctypes.POINTER(ctypes.c_void_p)    # EGLConfig *
SP = ctypes.POINTER(ctypes.c_char_p)    # const GLchar *const * (glShaderSource)

EGL_PROTOTYPES = {
    "eglInitialize": ([P, B, B], U),
    "eglTerminate": ([P], U),
    "eglBindAPI": ([I], U),
    "eglGetError": ([], I),
    "eglChooseConfig": ([P, B, VP, I, B], U),
    "eglCreateContext": ([P, P, P, B], P),
    "eglDestroyContext": ([P, P], U),
    "eglMakeCurrent": ([P, P, P, P], U),
    "eglQueryString": ([P, I], S),
    # Exported by libEGL.so.1 only through eglGetProcAddress on some builds;
    # resolved defensively in _display().
    "eglGetPlatformDisplay": ([I, P, ctypes.POINTER(ctypes.c_longlong)], P),
    "eglGetPlatformDisplayEXT": ([I, P, B], P),
    "eglGetProcAddress": ([S], P),
}

GL_PROTOTYPES = {
    "glGetError": ([], I),
    "glGetString": ([I], P),                     # const GLubyte* -> 64-bit!
    "glGetStringi": ([I, U], P),
    "glViewport": ([I, I, I, I], None),
    "glClear": ([U], None),
    "glClearColor": ([F, F, F, F], None),
    "glDrawArrays": ([I, I, I], None),
    "glReadPixels": ([I, I, I, I, I, I, P], None),
    # Out-pointer forms -- the original code passed a count and no destination.
    "glGenFramebuffers": ([I, UP], None),
    "glGenRenderbuffers": ([I, UP], None),
    "glBindFramebuffer": ([I, U], None),
    "glBindRenderbuffer": ([I, U], None),
    # 4-arg form: (target, internalformat, width, height).
    "glRenderbufferStorage": ([I, I, I, I], None),
    "glFramebufferRenderbuffer": ([I, I, I, U], None),
    "glCheckFramebufferStatus": ([I], I),
    "glDeleteFramebuffers": ([I, UP], None),
    "glDeleteRenderbuffers": ([I, UP], None),
    "glCreateShader": ([I], U),
    "glShaderSource": ([U, I, SP, B], None),
    "glCompileShader": ([U], None),
    "glGetShaderiv": ([U, I, B], None),
    "glGetShaderInfoLog": ([U, I, P, P], None),
    "glDeleteShader": ([U], None),
    "glCreateProgram": ([], U),
    "glAttachShader": ([U, U], None),
    "glLinkProgram": ([U], None),
    "glGetProgramiv": ([U, I, B], None),
    "glGetProgramInfoLog": ([U, I, P, P], None),
    "glUseProgram": ([U], None),
    "glDeleteProgram": ([U], None),
    "glGetUniformLocation": ([U, S], I),
    "glUniform1f": ([I, F], None),
    "glUniform2f": ([I, F, F], None),
    "glUniform3f": ([I, F, F, F], None),
    "glUniform4f": ([I, F, F, F, F], None),
}


class GlError(RuntimeError):
    """Raised on any EGL/GL failure; carries the raw numeric code."""


def _fmt_e(err):
    names = {0x3003: "EGL_NOT_INITIALIZED", 0x3005: "EGL_BAD_ACCESS",
             0x3006: "EGL_BAD_ALLOC", 0x3007: "EGL_BAD_ATTRIBUTE",
             0x300C: "EGL_BAD_CONFIG", 0x300D: "EGL_BAD_CONTEXT",
             0x300E: "EGL_BAD_DISPLAY", 0x300F: "EGL_BAD_MATCH",
             0x3011: "EGL_BAD_NATIVE_WINDOW", 0x3012: "EGL_BAD_PARAMETER",
             0x3009: "EGL_BAD_SURFACE", 0x3010: "EGL_CONTEXT_LOST"}
    return "%s (0x%04X)" % (names.get(err, "EGL_UNKNOWN"), err)


def _fmt_gl(err):
    names = {0x0500: "GL_INVALID_ENUM", 0x0501: "GL_INVALID_VALUE",
             0x0502: "GL_INVALID_OPERATION", 0x0505: "GL_OUT_OF_MEMORY",
             0x0506: "GL_INVALID_FRAMEBUFFER_OPERATION"}
    return "%s (0x%04X)" % (names.get(err, "GL_UNKNOWN"), err)


class Gl:
    """Minimal surfaceless EGL + GLES3 context (ctypes only).

    Every EGL/GL entry point is bound with an explicit argtypes/restype
    signature before it is called; nothing is called through a bare CDLL
    attribute.  Holds the offscreen FBO and renderbuffer, and releases every
    GL/EGL resource it created in close() (context manager).
    """

    def __init__(self, egl_lib="libEGL.so.1", gl_lib="libGLESv2.so.2"):
        self.egl = ctypes.CDLL(egl_lib)
        self.gl = ctypes.CDLL(gl_lib)
        self.dpy = self._display()
        self.cfg = None
        self.ctx = None
        self.fbo = 0
        self.rbo = 0
        self._shaders = []
        self._programs = []
        major, minor = ctypes.c_int(), ctypes.c_int()
        if not self.egl.eglInitialize(self.dpy, ctypes.byref(major), ctypes.byref(minor)):
            raise GlError("eglInitialize failed: %s" % self._eerr())
        self._initialized = True
        if not self.egl.eglBindAPI(EGL_OPENGL_ES_API):
            raise GlError("eglBindAPI(ES) failed: %s" % self._eerr())

        cfg = ctypes.c_void_p()
        n = ctypes.c_int()
        attrs = (ctypes.c_int * 5)(EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                  EGL_NONE)
        if not self.egl.eglChooseConfig(self.dpy, attrs,
                                        ctypes.byref(cfg), 1,
                                        ctypes.byref(n)) or not n.value:
            raise GlError("eglChooseConfig found no ES3 config: %s" % self._eerr())
        if not cfg:
            raise GlError("eglChooseConfig returned a NULL EGLConfig")
        self.cfg = cfg

        self.ctx = self.egl.eglCreateContext(
            self.dpy, self.cfg, None,
            (ctypes.c_int * 3)(EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE))
        if not self.ctx:
            raise GlError("eglCreateContext failed: %s" % self._eerr())
        # EGL_NO_SURFACE is not NULL on every driver; pass it explicitly.
        if not self.egl.eglMakeCurrent(self.dpy, EGL_NONE_T(), EGL_NONE_T(),
                                       self.ctx):
            raise GlError("eglMakeCurrent failed: %s" % self._eerr())

        # --- offscreen FBO -------------------------------------------------
        fbo = ctypes.c_uint()
        self.gl.glGenFramebuffers(1, ctypes.byref(fbo))
        self.check("glGenFramebuffers")
        if not fbo.value:
            raise GlError("glGenFramebuffers produced no framebuffer")
        self.fbo = fbo.value

        rbo = ctypes.c_uint()
        self.gl.glGenRenderbuffers(1, ctypes.byref(rbo))
        self.check("glGenRenderbuffers")
        if not rbo.value:
            raise GlError("glGenRenderbuffers produced no renderbuffer")
        self.rbo = rbo.value

        self.gl.glBindRenderbuffer(GL_RENDERBUFFER, self.rbo)
        self.check("glBindRenderbuffer")
        # NOTE: 4-arg form -- (target, internalformat, width, height).
        self.gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H)
        self.check("glRenderbufferStorage")

        self.gl.glBindFramebuffer(GL_FRAMEBUFFER, self.fbo)
        self.check("glBindFramebuffer")
        self.gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_RENDERBUFFER, self.rbo)
        self.check("glFramebufferRenderbuffer")
        status = self.gl.glCheckFramebufferStatus(GL_FRAMEBUFFER)
        if status != GL_FRAMEBUFFER_COMPLETE:
            raise GlError("offscreen framebuffer incomplete: 0x%04X" % status)
        self.gl.glViewport(0, 0, W, H)
        self.check("glViewport")

    # -- binding -----------------------------------------------------------
    def _bind(self, lib, table, name):
        """Return the prototype-bound function, or raise."""
        try:
            fn = getattr(lib, name)
        except AttributeError:
            raise GlError("%s does not export %s" % (lib._name, name))
        argtypes, restype = table[name]
        fn.argtypes = argtypes
        fn.restype = restype
        return fn

    def _egl(self, name):
        return self._bind(self.egl, EGL_PROTOTYPES, name)

    def _gl(self, name):
        return self._bind(self.gl, GL_PROTOTYPES, name)

    def bind_all(self):
        """Eagerly resolve every prototype. Raises on the first missing symbol.

        Called by the unit tests with mocked libraries, and useful as a
        fail-fast check that the host's libEGL/libGLESv2 match these
        signatures before any GPU work is attempted.
        """
        missing = []
        for name in EGL_PROTOTYPES:
            if name in ("eglGetPlatformDisplay", "eglGetPlatformDisplayEXT"):
                continue  # resolved dynamically; see _display()
            try:
                self._egl(name)
            except GlError as e:
                missing.append(str(e))
        for name in GL_PROTOTYPES:
            try:
                self._gl(name)
            except GlError as e:
                missing.append(str(e))
        if missing:
            raise GlError("missing symbols:\n  " + "\n  ".join(missing))

    # -- display -----------------------------------------------------------
    def _display(self):
        """Get a surfaceless EGLDisplay.

        eglGetPlatformDisplayEXT is NOT a mandatory export of libEGL.so.1 --
        on this host only the core eglGetPlatformDisplay(EGL 1.5) is exported,
        which is exactly why the previous run died with
        "undefined symbol: eglGetPlatformDisplayEXT".  Try, in order:
          1. exported core eglGetPlatformDisplay  (EGLAttrib* attrs, EGL 1.5)
          2. eglGetProcAddress("eglGetPlatformDisplayEXT")  (EGLint* attrs)
          3. eglGetProcAddress("eglGetPlatformDisplay")     (EGLint* attrs)
        """
        attribs_none = ctypes.POINTER(ctypes.c_longlong)()
        int_attrs = (ctypes.c_int * 1)(EGL_NONE)

        # 1. core EGL 1.5 export
        try:
            get = self._bind(self.egl, EGL_PROTOTYPES, "eglGetPlatformDisplay")
        except GlError:
            get = None
        if get is not None:
            d = get(EGL_PLATFORM_SURFACELESS_MESA, None, attribs_none)
            if d:
                return d

        # 2/3. dynamic lookup via eglGetProcAddress + CFUNCTYPE
        procaddr = self._bind(self.egl, EGL_PROTOTYPES, "eglGetProcAddress")
        for name, argtypes, restype in (
                ("eglGetPlatformDisplayEXT",
                 EGL_PROTOTYPES["eglGetPlatformDisplayEXT"], P),
                ("eglGetPlatformDisplay",
                 [I, P, B], P)):
            addr = procaddr(name.encode())
            if not addr:
                continue
            fn = ctypes.CFUNCTYPE(restype, *argtypes)(addr)
            d = fn(EGL_PLATFORM_SURFACELESS_MESA, None, int_attrs)
            if d:
                return d
        raise GlError(
            "no surfaceless EGLDisplay: libEGL.so.1 exports neither "
            "eglGetPlatformDisplay nor EGL_PLATFORM_SURFACELESS_MESA support "
            "(eglGetPlatformDisplayEXT is optional and absent on this host)")

    # -- errors ------------------------------------------------------------
    def _eerr(self):
        return _fmt_e(self.egl.eglGetError())

    def check(self, where):
        """Drain the GL error queue and raise on the first real error."""
        err = self.gl.glGetError()
        if err != GL_NO_ERROR:
            raise GlError("GL error %s after %s" % (_fmt_gl(err), where))

    def info(self):
        gstr = self.gl.glGetString

        def s(tok):
            p = gstr(tok)          # restype c_void_p -> full 64-bit pointer
            if not p:
                return None
            # restype is c_void_p, so ctypes hands back a bare int address.
            # Cast straight to c_char_p: POINTER(c_char) has no .value, so the
            # old cast raised AttributeError here.
            return ctypes.cast(p, ctypes.c_char_p).value.decode("utf-8", "replace")

        # eglQueryString returns const char*; restype is c_char_p so ctypes
        # converts it in one step instead of truncating a pointer to an int.
        q = self.egl.eglQueryString(self.dpy, EGL_VERSION)
        return {
            "gl_version": s(GL_VERSION),
            "gl_vendor": s(GL_VENDOR),
            "gl_renderer": s(GL_RENDERER),
            "glsl_version": s(GL_SHADING_LANGUAGE_VERSION),
            "egl_version": q.decode("utf-8", "replace") if q else None,
        }

    # -- shaders -----------------------------------------------------------
    def compile(self, src, kind, label):
        sh = self.gl.glCreateShader(kind)
        self.check("glCreateShader")
        if not sh:
            raise GlError("%s: glCreateShader returned 0" % label)
        data = src.encode()
        buf = ctypes.create_string_buffer(data)
        p = ctypes.pointer(ctypes.cast(buf, ctypes.c_char_p))
        length = ctypes.c_int(len(data))
        self.gl.glShaderSource(sh, 1, p, ctypes.byref(length))
        self.check("%s glShaderSource" % label)
        self.gl.glCompileShader(sh)
        self.check("%s glCompileShader" % label)
        ok = ctypes.c_int()
        self.gl.glGetShaderiv(sh, GL_COMPILE_STATUS, ctypes.byref(ok))
        if not ok.value:
            log = ctypes.create_string_buffer(1 << 16)
            self.gl.glGetShaderInfoLog(sh, 1 << 16, None, log)
            self.gl.glDeleteShader(sh)
            raise GlError("%s compile failed:\n%s" % (label, log.value.decode()))
        self._shaders.append(sh)
        return sh

    def link(self, vert, frag):
        prog = self.gl.glCreateProgram()
        self.check("glCreateProgram")
        if not prog:
            raise GlError("glCreateProgram returned 0")
        self.gl.glAttachShader(prog, vert)
        self.gl.glAttachShader(prog, frag)
        self.check("glAttachShader")
        self.gl.glLinkProgram(prog)
        self.check("glLinkProgram")
        ok = ctypes.c_int()
        self.gl.glGetProgramiv(prog, GL_LINK_STATUS, ctypes.byref(ok))
        if not ok.value:
            log = ctypes.create_string_buffer(1 << 16)
            self.gl.glGetProgramInfoLog(prog, 1 << 16, None, log)
            raise GlError("program link failed:\n%s" % log.value.decode())
        self.gl.glUseProgram(prog)
        self.check("glUseProgram")
        self._programs.append(prog)
        return prog

    # -- teardown ----------------------------------------------------------
    def close(self):
        """Release every GL/EGL resource created by this object, in order.

        Safe to call twice and safe to call from __exit__ after a partial
        constructor failure (missing attributes make each step a no-op).
        """
        if not hasattr(self, "gl"):
            return  # constructor failed before the GL library was opened
        for prog in getattr(self, "_programs", []):
            try:
                self.gl.glDeleteProgram(prog)
            except Exception:
                pass
        self._programs = []
        for sh in getattr(self, "_shaders", []):
            try:
                self.gl.glDeleteShader(sh)
            except Exception:
                pass
        self._shaders = []
        for name, attr in (("glDeleteFramebuffers", "fbo"),
                           ("glDeleteRenderbuffers", "rbo")):
            val = getattr(self, attr, 0)
            if val:
                try:
                    buf = ctypes.c_uint(val)
                    getattr(self.gl, name)(1, ctypes.byref(buf))
                except Exception:
                    pass
                setattr(self, attr, 0)
        if getattr(self, "ctx", None):
            try:
                self.egl.eglMakeCurrent(self.dpy, EGL_NONE_T(), EGL_NONE_T(),
                                        EGL_NONE_T())
            except Exception:
                pass
            try:
                self.egl.eglDestroyContext(self.dpy, self.ctx)
            except Exception:
                pass
            self.ctx = None
        if getattr(self, "_initialized", False):
            try:
                self.egl.eglTerminate(self.dpy)
            except Exception:
                pass
            self._initialized = False

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


def build_shader_source(frag_path):
    """Rename the real main() and append a diagnostic main() that calls the
    untouched jet_glow() from the same file."""
    src = frag_path.read_text()
    if src.count("void main(") != 1:
        raise SystemExit("expected exactly one 'void main(' in %s" % frag_path)
    if "vec3 jet_glow(" not in src:
        raise SystemExit("jet_glow() not found in %s" % frag_path)
    renamed = src.replace("void main(", "void bh_scene_main(", 1)
    diagnostic = """
uniform vec3 u_diag_view;    // camera position (world units)
uniform vec3 u_diag_right;   // screen right
uniform vec3 u_diag_up;      // screen up (the jet axis projects onto this)
uniform vec3 u_diag_axis;    // jet axis, frozen (u_enh == 0)
uniform float u_diag_scale;  // world units per pixel
uniform float u_diag_expo;   // display gain so faint knots are measurable
void main(){
  vec2 uv = (gl_FragCoord.xy - u_resolution * .5) * u_diag_scale;
  vec3 ray = normalize(u_diag_right * uv.x + u_diag_up * uv.y - u_diag_view);
  vec3 c = max(jet_glow(u_diag_view, ray, u_diag_axis), 0.0) * u_diag_expo;
  fragColor = vec4(pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
"""
    return renamed + diagnostic


# Every uniform jet_glow() and its helpers can touch. Anything not listed here
# is simply never set; the diagnostic path never reads it.
UNIFORMS = {
    "u_time": 0.0, "u_seed": SEED, "u_jet": 1.0, "u_jet_len": JET_LEN,
    "u_enh": ENH, "u_exposure": 1.0, "u_tonemap": 0.0,
    "u_palette_contrast": 1.0, "u_dither": 0.0, "u_fade": 1.0,
    "u_dmin": 3.0, "u_dmax": 26.0, "u_radius": 1.0, "u_temperature": 1.0,
    "u_density": 1.0, "u_rotation": 0.0, "u_inclination": 0.0,
    "u_orbit_rate": 0.0, "u_star_density": 0.0, "u_camera_mode": 0.0,
    "u_palette": 0.0, "u_approach": 0.0, "u_periapsis": 0.0,
    "u_palette_phase": 0.0, "u_palette_rate": 0.0, "u_spin": 0.0,
    "u_isco": 1.0, "u_beaming": 0.0, "u_bloom": 0.0, "u_fringe": 0.0,
    "u_lensing": 1.0, "u_nebula_amt": 0.0, "u_hot_sector": 0.0,
    "u_structure": 0.0, "u_jet_duty": 1.0, "u_photon_ring": 0.0,
    "u_bright_stars": 0.0, "u_kerr": 0.0, "u_disk_out": 12.0,
    "u_torus": 0.0, "u_torus_r": 6.0, "u_comp": 0.0, "u_max_steps": 48,
    "u_flyby": 0.0, "u_fly_t": 0.0, "u_fly_var": 0.0, "u_incl": 0.0,
    "u_nebula_scheme": 0.0, "u_camera_family": 0.0, "u_orbit_q": 1.0,
    "u_orbit_e": 0.0, "u_orbit_omega": 0.0,
}
VEC2 = {"u_resolution": (float(W), float(H)), "u_jitter": (0.0, 0.0),
        "u_drift": (0.0, 0.0), "u_nebula_axis": (0.0, 1.0)}
VEC4 = {"u_camo": (0.0,) * 4, "u_nebula": (0.0,) * 4, "u_disk_axis": (0.0,) * 4,
        "u_disk_precess": (0.0,) * 4, "u_scn": (0.0,) * 4, "u_gw": (0.0,) * 4,
        "u_gw2": (0.0,) * 4, "u_fx": (0.0,) * 4, "u_tde": (0.0,) * 4,
        "u_tde2": (0.0,) * 4, "u_tde3": (0.0,) * 4, "u_gal": (0.0,) * 4,
        "u_gal2": (0.0,) * 4, "u_zoom": (0.0,) * 4}


def camera_for_axis(axis):
    """Side-on view: camera perpendicular to the axis, so the full jet length
    projects across the screen and the axis lands on screen-up."""
    a = _norm(axis)
    view = _norm(_cross(a, (0.0, 0.0, 1.0)))
    cam = tuple(24.0 * c for c in view)
    up = a
    right = _cross(up, view)
    return a, cam, right, up


def render(gl, prog, cam, right, up, axis, t):
    # Uniform locations are cached per program; glGetUniformLocation returns a
    # signed GLint (-1 when absent), which is why locations are compared < 0.
    cache = getattr(gl, "_loc_cache", None)
    if cache is None:
        cache = gl._loc_cache = {}

    def loc(name):
        key = (prog, name)
        if key not in cache:
            cache[key] = gl.gl.glGetUniformLocation(
                prog, ctypes.c_char_p(name.encode()))
        return cache[key]

    vals = dict(UNIFORMS)
    vals["u_time"] = t
    for name, v in vals.items():
        l = loc(name)
        if l >= 0:
            gl.gl.glUniform1f(l, float(v))
    for name, v in VEC2.items():
        l = loc(name)
        if l >= 0:
            gl.gl.glUniform2f(l, float(v[0]), float(v[1]))
    for name, v in VEC4.items():
        l = loc(name)
        if l >= 0:
            gl.gl.glUniform4f(l, *(float(c) for c in v))
    setv = {"u_diag_view": cam, "u_diag_right": right,
            "u_diag_up": up, "u_diag_axis": axis}
    for name, v in setv.items():
        l = loc(name)
        if l < 0:
            raise RuntimeError("diagnostic uniform %s not found (optimised out?)" % name)
        gl.gl.glUniform3f(l, *(float(c) for c in v))
    gl.gl.glUniform1f(loc("u_diag_scale"), float(SCALE))
    gl.gl.glUniform1f(loc("u_diag_expo"), float(EXPOSURE))
    gl.gl.glClearColor(0.0, 0.0, 0.0, 1.0)
    gl.gl.glClear(GL_COLOR_BUFFER_BIT)
    gl.gl.glDrawArrays(GL_TRIANGLES, 0, 3)  # one fullscreen triangle
    gl.check("draw")
    buf = ctypes.create_string_buffer(W * H * 4)
    gl.gl.glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf)
    gl.check("readPixels")
    return bytes(buf)


def write_ppm(path, rgba):
    rows = []
    for y in range(H - 1, -1, -1):  # GL origin is bottom-left, PPM is top-left
        row = rgba[y * W * 4 : (y + 1) * W * 4]
        rows.append(b"".join(bytes(row[i : i + 3]) for i in range(0, len(row), 4)))
    with open(path, "wb") as fh:
        fh.write(b"P6\n%d %d\n255\n" % (W, H))
        fh.write(b"".join(rows))


def profile(rgba, h):
    """Max luminance across the beam width at unsigned height h.

    gl_FragCoord.y counts up from the bottom, so h maps to +h/SCALE there.
    """
    py = int(round(H / 2 + h / SCALE))
    best = 0.0
    for x in range(-3, 4):
        xi = W // 2 + x
        if not (0 <= xi < W and 0 <= py < H):
            continue
        o = (py * W + xi) * 4
        r, g, b = rgba[o] / 255.0, rgba[o + 1] / 255.0, rgba[o + 2] / 255.0
        best = max(best, 0.2126 * r + 0.7152 * g + 0.0722 * b)
    return best


def _stats(a):
    n = len(a)
    m = sum(a) / n
    var = sum((x - m) ** 2 for x in a) / n
    return m, var


def measure_shift(p0, p1, min_overlap=None):
    """Zero-mean normalised (Pearson) cross-correlation of the t0 profile against
    the t1 profile over the ACTUAL overlap of each candidate shift.

    Returns (shift_samples, best_r, overlap, ok, reason).  A positive shift means
    the structure moved toward larger sample index, i.e. outward.  A raw
    uncentred dot product is biased by the DC level and happily "passes" on a
    flat/black profile, so the score is r, not a sum, and an unusable profile
    reports ok=False instead of a random positive winner.

    One sample step == SCALE world units (see H_SAMPLES), so
    shift_world = shift_samples * SCALE.
    """
    if min_overlap is None:
        min_overlap = max(3, int(math.ceil(MIN_OVERLAP_FRAC * len(p0))))
    n0, n1 = len(p0), len(p1)
    best = (-2.0, None, 0)
    for s in range(-SEARCH_SAMPLES, SEARCH_SAMPLES + 1):
        lo0 = max(0, -s)
        lo1 = max(0, s)
        n = min(n0 - lo0, n1 - lo1)
        if n < min_overlap:
            continue
        a = p0[lo0:lo0 + n]
        b = p1[lo1:lo1 + n]
        ma, va = _stats(a)
        mb, vb = _stats(b)
        if math.sqrt(va) < MIN_CONTRAST or math.sqrt(vb) < MIN_CONTRAST:
            continue
        cov = sum((x - ma) * (y - mb) for x, y in zip(a, b)) / n
        r = cov / (math.sqrt(va) * math.sqrt(vb))
        if r > best[0]:
            best = (r, s, n)
    r, s, n = best
    if s is None:
        return None, r, n, False, "no shift in +/-%g world had enough overlap and contrast" % SEARCH_WORLD
    ok = abs(r) >= 0.05
    reason = "ok" if ok else "best |r|=%.4f below 0.05: profile is noise, not a measurable knot" % abs(r)
    return s, r, n, ok, reason


def shift_px(p0, p1):
    """Backwards-compatible wrapper: (shift_samples, r) or (None, r)."""
    s, r, _n, _ok, _why = measure_shift(p0, p1)
    return s, r


def run(frag_path, out_dir):
    frag_path = pathlib.Path(frag_path)
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    source = build_shader_source(frag_path)
    sha = hashlib.sha256(source.encode()).hexdigest()

    results = {}
    # The context manager guarantees the FBO, renderbuffer, shaders, program,
    # EGL context and display are released even if a compile/render raises.
    with Gl() as gl:
        gl.bind_all()
        info = gl.info()
        vert = gl.compile(
            # Fullscreen triangle from gl_VertexID; no vertex buffers are needed
            # because the diagnostic fragment main() uses gl_FragCoord.
            "#version 300 es\n"
            "void main(){\n"
            "  vec2 p = vec2(gl_VertexID == 1 ? 3. : -1., gl_VertexID == 2 ? 3. : -1.);\n"
            "  gl_Position = vec4(p, 0., 1.);\n"
            "}\n",
            GL_VERTEX_SHADER, "vertex")
        frag = gl.compile(source, GL_FRAGMENT_SHADER, "blackhole.frag+diagnostic")
        prog = gl.link(vert, frag)

        for name, axis in sorted(AXES.items()):
            a, cam, right, up = camera_for_axis(axis)
            frames = {}
            profiles = {}
            for tag, t in (("t0", T0), ("t1", T1)):
                rgba = render(gl, prog, cam, right, up, a, t)
                ppm = out_dir / ("%s_%s.ppm" % (name, tag))
                write_ppm(ppm, rgba)
                frames[tag] = {"time": t, "ppm": str(ppm)}
                for pole, sgn in (("positive", +1.0), ("negative", -1.0)):
                    profiles.setdefault(pole, {})[tag] = [
                        profile(rgba, sgn * h) for h in H_SAMPLES]
            poles = {}
            for pole, pr in profiles.items():
                s, r, novl, ok, why = measure_shift(pr["t0"], pr["t1"])
                sw = None if s is None else s * SCALE
                good = bool(ok and s is not None and s > 0)
                poles[pole] = {
                    "shift_samples": s,
                    "shift_world_units": sw,
                    "outward": good,
                    "measured": bool(ok),
                    "reason": why,
                    "overlap_samples": novl,
                    "pearson_r": r,
                    "max_lum_t0": max(pr["t0"]),
                    "max_lum_t1": max(pr["t1"]),
                    "profile_h": H_SAMPLES,
                    "profile_lum_t0": [round(x, 5) for x in pr["t0"]],
                    "profile_lum_t1": [round(x, 5) for x in pr["t1"]],
                }
            results[name] = {
                "axis_world": list(axis),
                "camera_pos": list(cam),
                "frames": frames,
                "poles": poles,
                "both_poles_outward": all(p["outward"] for p in poles.values()),
            }

    return {
        "tool": "tools/jet-validation/jet_outward_proof.py",
        "renderer": "EGL surfaceless + OpenGL ES 3.0 (ctypes, real libEGL/libGLESv2)",
        "gl_info": info,
        "shader_path": str(frag_path),
        "shader_sha256": sha,
        "shader_source_bytes": len(source),
        "shader_source_note": (
            "complete real blackhole.frag with 'void main(' renamed to "
            "bh_scene_main() plus an appended diagnostic main(); jet_glow() "
            "and its noise()/jet_tint() helpers are byte-identical to the file"),
        "resolution": [W, H],
        "world_units_per_pixel": SCALE,
        "h_samples": H_SAMPLES,
        "h_sample_spacing_world": SCALE,
        "search_window_world": [-SEARCH_WORLD, SEARCH_WORLD],
        "min_contrast_std": MIN_CONTRAST,
        "min_overlap_fraction": MIN_OVERLAP_FRAC,
        "correlation": "zero-mean normalised Pearson r over the actual overlap",
        "times": [T0, T1],
        "seed": SEED,
        "u_enh": ENH,
        "u_jet_len": JET_LEN,
        "uniforms_set": {
            "float": {k: UNIFORMS[k] for k in sorted(UNIFORMS)},
            "vec2": {k: list(v) for k, v in sorted(VEC2.items())},
            "vec4": {k: list(v) for k, v in sorted(VEC4.items())},
            "diagnostic": {
                "u_diag_scale": SCALE, "u_diag_expo": EXPOSURE,
                "u_diag_axis": "per-axis, see results",
            },
        },
        "expected_knot_speed_world_per_s": 1.6 / 0.55,
        "expected_shift_world_units": EXPECTED_WORLD,
        "shift_tolerance_world_units": SHIFT_TOL_WORLD,
        "results": results,
        "all_poles_outward": all(r["both_poles_outward"] for r in results.values()),
        "all_poles_measured": all(p["measured"] for r in results.values()
                                 for p in r["poles"].values()),
        "all_poles_near_expected": all(
            p["shift_world_units"] is not None
            and abs(p["shift_world_units"] - EXPECTED_WORLD) <= SHIFT_TOL_WORLD
            for r in results.values() for p in r["poles"].values()),
        "pass_expect_outward": bool(
            all(r["both_poles_outward"] for r in results.values())
            and all(p["measured"] for r in results.values()
                    for p in r["poles"].values())
            and all(p["shift_world_units"] is not None
                    and abs(p["shift_world_units"] - EXPECTED_WORLD) <= SHIFT_TOL_WORLD
                    for r in results.values() for p in r["poles"].values())),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shader", default=str(ROOT / "vendor" / "blackhole" / "blackhole.frag"))
    ap.add_argument("--out-dir", required=True,
                    help="required output dir (Projects-disk scratch; never /tmp or stage/tmp)")
    ap.add_argument("--expect-outward", action="store_true",
                    help="exit non-zero unless every pole drifted outward")
    args = ap.parse_args()
    report = run(args.shader, args.out_dir)
    path = pathlib.Path(args.out_dir) / "report.json"
    path.write_text(json.dumps(report, indent=2))
    print(json.dumps({k: report[k] for k in
                      ("shader_sha256", "all_poles_measured", "all_poles_outward",
                       "all_poles_near_expected", "expected_shift_world_units",
                       "shift_tolerance_world_units")}))
    for name, r in report["results"].items():
        for pole, p in r["poles"].items():
            sw = "n/a" if p["shift_world_units"] is None else "%+.3f" % p["shift_world_units"]
            print("  %-9s %-9s shift=%s world  r=%+.4f  overlap=%d  outward=%s  measured=%s"
                  % (name, pole, sw, p["pearson_r"], p["overlap_samples"],
                     p["outward"], p["measured"]))
            if not p["measured"]:
                print("      DIAGNOSTIC FAILURE: %s" % p["reason"])
    print("report: %s" % path)
    if args.expect_outward:
        if not report["pass_expect_outward"]:
            print("FAIL: --expect-outward requires every pole on both axes to be "
                  "measured (real contrast), outward (shift > 0) and within "
                  "%g world of the expected %+.3f" % (SHIFT_TOL_WORLD, EXPECTED_WORLD))
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())