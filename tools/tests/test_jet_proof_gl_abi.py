# SPDX-License-Identifier: Apache-2.0
"""ABI tests for the jet-proof ctypes EGL/GLES3 bindings.

These exercise the REAL prototype tables in
``tools/jet-validation/jet_outward_proof.py`` against a genuinely compiled
fake ``libEGL``/``libGLESv2`` (built on the fly with the host cc).  That
matters: the bugs being pinned here -- ``glGenFramebuffers(1)`` with no out
pointer, ``glRenderbufferStorage`` missing its target argument, and 64-bit
pointer truncation -- are ctypes *marshalling* bugs.  Mocking them in Python
would only re-state the same function, so instead we call through the real
C ABI with real pointer widths.

No GPU and no EGL/GLESv2 library is required or loaded.
"""

import ctypes
import importlib.util
import pathlib
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "jet-validation" / "jet_outward_proof.py"

spec = importlib.util.spec_from_file_location("jet_outward_proof", TOOL)
assert spec and spec.loader
tool = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = tool
spec.loader.exec_module(tool)

# ---------------------------------------------------------------------------
# Fake GLESv2: implements exactly the signatures declared in GLES3/gl3.h, and
# validates them.  The accessors below read back the recorded arguments so the
# tests can assert on what actually crossed the ABI boundary.
# ---------------------------------------------------------------------------
FAKE_GL_C = r"""
#include <stdint.h>
typedef uint32_t GLenum; typedef uint32_t GLuint; typedef int32_t GLint;
typedef int32_t GLsizei; typedef uint32_t GLbitfield; typedef float GLfloat;
typedef unsigned char GLubyte; typedef char GLchar; typedef uint32_t GLboolean;

GLuint last_gen_fbo_n = 0, last_gen_rbo_n = 0;
GLuint last_gen_fbo_out = 0, last_gen_rbo_out = 0;
GLuint last_rb_target = 0, last_rb_fmt = 0, last_rb_attach_target = 0;
GLsizei last_rb_w = 0, last_rb_h = 0;
GLenum last_attrib = 0; GLint last_vp[4] = {-1, -1, -1, -1};
GLuint last_loc = 0; GLfloat last_uniform[4] = {0, 0, 0, 0};
int uniform_count = 0;
const char *last_string = 0;

GLenum glGetError(void) { return 0; }
const GLubyte *glGetStringi(GLenum name, GLuint index) {
  (void)name; (void)index; return (const GLubyte *)"fake-ext";
}
const GLubyte *glGetString(GLenum name) { last_string = "fake-gl"; (void)name; return (const GLubyte *)last_string; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { last_vp[0]=x; last_vp[1]=y; last_vp[2]=w; last_vp[3]=h; }
void glClear(GLbitfield m) { (void)m; }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r;(void)g;(void)b;(void)a; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count) { (void)mode;(void)first;(void)count; }
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p) {
  (void)x;(void)y;(void)w;(void)h;(void)f;(void)t;
  if (p) for (int i = 0; i < 4; i++) ((unsigned char *)p)[i] = 0xAB;
}
/* The real signature REQUIRES a GLuint* out parameter. */
void glGenFramebuffers(GLsizei n, GLuint *ids) {
  last_gen_fbo_n = n; last_gen_fbo_out = (ids && n > 0) ? (*ids = 7u) : 0u;
  if (!ids) { last_gen_fbo_out = 0; }
}
void glGenRenderbuffers(GLsizei n, GLuint *ids) {
  last_gen_rbo_n = n; last_gen_rbo_out = (ids && n > 0) ? (*ids = 9u) : 0u;
  if (!ids) { last_gen_rbo_out = 0; }
}
void glBindFramebuffer(GLenum t, GLuint fb) { last_attrib = t; (void)fb; }
void glBindRenderbuffer(GLenum t, GLuint rb) { last_rb_target = t; (void)rb; }
/* Four arguments, not three: (target, internalformat, width, height). */
void glRenderbufferStorage(GLenum target, GLenum fmt, GLsizei w, GLsizei h) {
  last_rb_target = target; last_rb_fmt = fmt; last_rb_w = w; last_rb_h = h;
}
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rt, GLuint rb) {
  last_attrib = t; last_rb_attach_target = rt; (void)a; (void)rb;
}
GLenum glCheckFramebufferStatus(GLenum t) { (void)t; return 0x8CD5; }
void glDeleteFramebuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
GLuint glCreateShader(GLenum t) { (void)t; return 3u; }
void glShaderSource(GLuint s, GLsizei c, const GLchar *const *str, const GLint *len) {
  (void)s; (void)str;
  /* count and length must survive the call, so a wrong argtype would show up. */
  if (c != 1) last_gen_fbo_n = 0xFFFFFFFFu;
  if (len && len[0] < 0) last_gen_rbo_n = 0xFFFFFFFFu;
}
void glCompileShader(GLuint s) { (void)s; }
void glGetShaderiv(GLuint s, GLenum p, GLint *params) { (void)s;(void)p; if (params) *params = 1; }
void glGetShaderInfoLog(GLuint s, GLsizei n, GLsizei *l, GLchar *log) {
  (void)s; if (log && n > 0) log[0] = 0; if (l) *l = 0;
}
void glDeleteShader(GLuint s) { (void)s; }
GLuint glCreateProgram(void) { return 5u; }
void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
void glLinkProgram(GLuint p) { (void)p; }
void glGetProgramiv(GLuint p, GLenum n, GLint *params) { (void)p;(void)n; if (params) *params = 1; }
void glGetProgramInfoLog(GLuint p, GLsizei n, GLsizei *l, GLchar *log) {
  (void)p; if (log && n > 0) log[0] = 0; if (l) *l = 0;
}
void glUseProgram(GLuint p) { (void)p; }
void glDeleteProgram(GLuint p) { (void)p; }
GLint glGetUniformLocation(GLuint p, const GLchar *n) { (void)p; (void)n; return 0; }
void glUniform1f(GLint l, GLfloat a) { last_loc = (GLuint)l; last_uniform[0] = a; uniform_count++; }
void glUniform2f(GLint l, GLfloat a, GLfloat b) {
  last_loc = (GLuint)l; last_uniform[0] = a; last_uniform[1] = b; uniform_count++;
}
void glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) {
  last_loc = (GLuint)l; last_uniform[0]=a; last_uniform[1]=b; last_uniform[2]=c; uniform_count++;
}
void glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
  last_loc = (GLuint)l; last_uniform[0]=a; last_uniform[1]=b;
  last_uniform[2]=c; last_uniform[3]=d; uniform_count++;
}
"""

# eglGetPlatformDisplayEXT is deliberately NOT exported: this reproduces the
# real libEGL.so.1 on this host, which only exports the core
# eglGetPlatformDisplay(EGL 1.5).  Getting a display must therefore fall
# through to the core entry point instead of raising on the missing symbol.
FAKE_EGL_C = r"""
#include <stdint.h>
typedef void *EGLDisplay; typedef void *EGLConfig; typedef void *EGLContext;
typedef void *EGLSurface; typedef unsigned int EGLBoolean;
typedef int EGLint; typedef unsigned int EGLenum; typedef int64_t EGLAttrib;

static EGLDisplay the_display = (EGLDisplay)0xE6D0000000001ULL;
EGLint last_err = 0;

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *nd, const EGLAttrib *a) {
  (void)nd; if (a) return the_display;
  return the_display;
}
EGLBoolean eglInitialize(EGLDisplay d, EGLint *maj, EGLint *min) {
  (void)d; if (maj) *maj = 1; if (min) *min = 5; return 1;
}
EGLBoolean eglTerminate(EGLDisplay d) { (void)d; return 1; }
EGLBoolean eglBindAPI(EGLenum api) { (void)api; return 1; }
EGLint eglGetError(void) { return last_err; }
EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *a, EGLConfig *c, EGLint n, EGLint *nc) {
  (void)d; if (c) *c = (EGLConfig)0xC0FFEE; if (nc) *nc = 1; (void)a; (void)n; return 1;
}
EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext s, const EGLint *a) {
  (void)d; (void)c; (void)s; (void)a;
  /* A high-bit address: any 32-bit truncation shows up as corruption. */
  return (EGLContext)0xC0FFEE0000001234ULL;
}
EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c) { (void)d; (void)c; return 1; }
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface dr, EGLSurface re, EGLContext c) {
  (void)d; (void)dr; (void)re; (void)c; return 1;
}
const char *eglQueryString(EGLDisplay d, EGLint name) {
  (void)d; (void)name; return "fake-egl-1.5";
}
void (*eglGetProcAddress(const char *n))(void) { (void)n; return 0; }
"""


@pytest.fixture(scope="module")
def fakes(tmp_path_factory):
    """Compile the fake libEGL/libGLESv2 and return (gl_path, egl_path)."""
    d = tmp_path_factory.mktemp("fakegl")
    paths = {}
    for name, src in (("libGLESv2.so.2", FAKE_GL_C), ("libEGL.so.1", FAKE_EGL_C)):
        c = d / (name + ".c")
        so = d / name
        c.write_text(src)
        r = subprocess.run(
            ["cc", "-shared", "-fPIC", "-o", str(so), str(c)],
            capture_output=True, text=True)
        if r.returncode != 0:
            pytest.skip("no working host cc for the fake GL libraries: " + r.stderr)
        paths[name] = str(so)
    return paths["libGLESv2.so.2"], paths["libEGL.so.1"]


@pytest.fixture
def gl(fakes):
    gl_path, egl_path = fakes
    obj = tool.Gl.__new__(tool.Gl)      # build without touching a real device
    obj.egl = ctypes.CDLL(egl_path)
    obj.gl = ctypes.CDLL(gl_path)
    obj.cfg = obj.ctx = None
    obj.fbo = obj.rbo = 0
    obj._shaders = []
    obj._programs = []
    obj._initialized = False
    obj.dpy = obj._display()
    obj.bind_all()
    yield obj
    obj.close()


def _c(lib, name):
    return ctypes.c_uint.in_dll(lib, name).value


def test_fake_egl_really_lacks_the_EXT_symbol(fakes):
    """Guard the premise: the fake mimics this host's real libEGL.so.1."""
    _, egl_path = fakes
    lib = ctypes.CDLL(egl_path)
    with pytest.raises(AttributeError):
        lib.eglGetPlatformDisplayEXT
    assert lib.eglGetPlatformDisplay


def test_display_falls_back_to_core_eglgetplatformdisplay(gl):
    """The original crash: eglGetPlatformDisplayEXT is undefined.

    _display() must still return a usable display via the exported core
    eglGetPlatformDisplay instead of raising AttributeError.
    """
    dpy = gl._display()
    assert dpy, "no EGLDisplay obtained"
    # restype is c_void_p, so ctypes hands back a plain int on this ABI.
    assert int(dpy) == 0xE6D0000000001


def test_display_failure_is_a_clear_glerror(gl):
    """If no platform path works the error names the cause, not AttributeError.

    lib really does omit eglGetPlatformDisplayEXT here, and eglGetProcAddress
    hands back nothing, so _display() must convert that into a GlError that
    names the missing entry point.
    """
    class _NoProcLib:
        """libEGL without either eglGetPlatformDisplay entry point, but with a
        real eglGetProcAddress that (like the host's) resolves nothing.  Raises
        exactly what CDLL raises for an undefined symbol: AttributeError."""

        _name = "libEGL.so.1"

        def __init__(self, lib):
            self._lib = lib

        def __getattr__(self, name):
            if name.startswith("eglGetPlatformDisplay"):
                raise AttributeError("%s: undefined symbol: %s" % (self._name, name))
            return getattr(self._lib, name)

    gl.egl = _NoProcLib(gl.egl)
    with pytest.raises(tool.GlError) as e:
        gl._display()
    assert "eglGetPlatformDisplayEXT" in str(e.value)


def test_pointer_returns_are_not_truncated_to_32_bits(gl):
    """Every pointer-returning binding must use a 64-bit restype.

    On LP64 ctypes defaults restype to c_int, so an unset restype silently
    truncates eglCreateContext/glGetString/eglQueryString.
    """
    assert ctypes.sizeof(ctypes.c_void_p) == 8, "test assumes LP64"
    for name in ("eglCreateContext", "eglGetPlatformDisplay",
                 "eglGetPlatformDisplayEXT", "eglGetProcAddress"):
        assert tool.EGL_PROTOTYPES[name][1] is ctypes.c_void_p, name
    for name in ("glGetString", "glGetStringi"):
        assert tool.GL_PROTOTYPES[name][1] is ctypes.c_void_p, name
    # ... and it must actually survive the round trip through the real ABI.
    ctx = gl.egl.eglCreateContext(
        None, None, None, (ctypes.c_int * 3)(tool.EGL_CONTEXT_CLIENT_VERSION, 3,
                                              tool.EGL_NONE))
    # The fake returns a handle with high bits set (0xC0FFEE00_00001234); any
    # 32-bit truncation would drop the 0xC0FFEE00 half.
    assert int(ctx) == 0xC0FFEE0000001234, "eglCreateContext truncated to 32 bits"


def test_eglquerystring_decodes_without_truncation(gl):
    dpy = gl._display()
    major, minor = ctypes.c_int(), ctypes.c_int()
    assert gl.egl.eglInitialize(dpy, ctypes.byref(major), ctypes.byref(minor))
    ver = gl.egl.eglQueryString(dpy, tool.EGL_VERSION)
    assert ver == b"fake-egl-1.5"
    assert gl.info()["egl_version"] == "fake-egl-1.5"
    assert gl.info()["gl_version"] == "fake-gl"


def test_genframebuffers_and_genrenderbuffers_pass_the_out_pointer(gl):
    """Regression: the old code called glGenFramebuffers(1) with no out arg.

    The fake returns 0 when the destination pointer is absent, so a lost
    out-pointer shows up as a 0 framebuffer id.
    """
    fbo = ctypes.c_uint()
    gl.gl.glGenFramebuffers(1, ctypes.byref(fbo))
    gl.check("glGenFramebuffers")
    assert fbo.value == 7
    assert _c(gl.gl, "last_gen_fbo_n") == 1
    assert _c(gl.gl, "last_gen_fbo_out") == 7

    rbo = ctypes.c_uint()
    gl.gl.glGenRenderbuffers(1, ctypes.byref(rbo))
    gl.check("glGenRenderbuffers")
    assert rbo.value == 9
    assert _c(gl.gl, "last_gen_rbo_n") == 1
    assert _c(gl.gl, "last_gen_rbo_out") == 9


def test_genfunctions_reject_the_old_arity(gl):
    """The 2-arg out-pointer form must be what the table declares."""
    at = tool.GL_PROTOTYPES["glGenFramebuffers"][0]
    assert len(at) == 2
    assert at[0] is ctypes.c_int                 # GLsizei n
    assert at[1] == ctypes.POINTER(ctypes.c_uint)  # GLuint * (matches gl3.h)


def test_renderbufferstorage_passes_target_internalformat_and_size(gl):
    """Regression: the old call omitted the leading `target` argument.

    Passing (GL_RGBA8, W, H) shifted every argument by one, so the driver saw
    target=GL_RGBA8 and a width of 256 from H.  The target is a separate
    first argument.
    """
    gl.gl.glBindRenderbuffer(tool.GL_RENDERBUFFER, 9)
    gl.gl.glRenderbufferStorage(tool.GL_RENDERBUFFER, tool.GL_RGBA8, tool.W, tool.H)
    gl.check("glRenderbufferStorage")
    assert len(tool.GL_PROTOTYPES["glRenderbufferStorage"][0]) == 4
    assert _c(gl.gl, "last_rb_target") == tool.GL_RENDERBUFFER
    assert _c(gl.gl, "last_rb_fmt") == tool.GL_RGBA8
    assert _c(gl.gl, "last_rb_w") == tool.W
    assert _c(gl.gl, "last_rb_h") == tool.H


def test_framebufferattachment_uses_the_color_attachment_enum(gl):
    fbo = ctypes.c_uint()
    gl.gl.glGenFramebuffers(1, ctypes.byref(fbo))
    rbo = ctypes.c_uint()
    gl.gl.glGenRenderbuffers(1, ctypes.byref(rbo))
    gl.gl.glBindFramebuffer(tool.GL_FRAMEBUFFER, fbo.value)
    gl.gl.glFramebufferRenderbuffer(tool.GL_FRAMEBUFFER, tool.GL_COLOR_ATTACHMENT0,
                                    tool.GL_RENDERBUFFER, rbo.value)
    assert tool.GL_COLOR_ATTACHMENT0 == 0x8CE0
    assert tool.GL_RENDERBUFFER == 0x8D41
    assert _c(gl.gl, "last_rb_attach_target") == tool.GL_RENDERBUFFER
    assert gl.gl.glCheckFramebufferStatus(tool.GL_FRAMEBUFFER) == tool.GL_FRAMEBUFFER_COMPLETE


def test_every_table_entry_is_bound_with_argtypes(gl):
    """bind_all() must leave a full signature on each resolved symbol."""
    for name in tool.GL_PROTOTYPES:
        fn = getattr(gl.gl, name)
        assert fn.argtypes is not None, name
        # void entry points legitimately declare restype None; what matters is
        # that the bound restype is exactly the declared one.
        assert fn.restype == tool.GL_PROTOTYPES[name][1], name
        assert len(fn.argtypes) == len(tool.GL_PROTOTYPES[name][0]), name


def test_missing_symbol_raises_a_named_glerror(gl):
    """An unexported symbol must surface as a named GlError, not AttributeError."""
    hidden = {"glDrawArrays"}

    class _HiddenLib:
        _name = "libGLESv2.so.2"

        def __init__(self, lib):
            self._lib = lib

        def __getattr__(self, name):
            if name in hidden:
                raise AttributeError("libGLESv2.so.2: undefined symbol: " + name)
            return getattr(self._lib, name)

    obj = tool.Gl.__new__(tool.Gl)
    obj.gl = _HiddenLib(gl.gl)
    with pytest.raises(tool.GlError) as e:
        obj._gl("glDrawArrays")
    assert "glDrawArrays" in str(e.value)


def test_shader_and_link_flow_uses_the_declared_prototypes(gl):
    v = gl.compile("#version 300 es\nvoid main(){gl_Position=vec4(0.);}\n",
                   tool.GL_VERTEX_SHADER, "vertex")
    f = gl.compile("#version 300 es\nprecision highp float;\n"
                   "out vec4 o; void main(){o=vec4(1.);}\n",
                   tool.GL_FRAGMENT_SHADER, "fragment")
    prog = gl.link(v, f)
    assert prog == 5 and gl._programs == [5] and gl._shaders == [3, 3]
    # glShaderSource received count=1 and a valid length (both checked in C).
    assert _c(gl.gl, "last_gen_fbo_n") != 0xFFFFFFFF, "glShaderSource count mangled"
    assert _c(gl.gl, "last_gen_rbo_n") != 0xFFFFFFFF, "glShaderSource length mangled"
    gl.close()
    assert gl._programs == [] and gl._shaders == []
    assert gl.ctx is None and gl.fbo == 0 and gl.rbo == 0


def test_close_is_idempotent_and_tolerates_a_failed_constructor():
    obj = tool.Gl.__new__(tool.Gl)   # nothing initialised at all
    obj.close()
    obj.close()                      # must not raise