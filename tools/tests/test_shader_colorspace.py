#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Colour-surface contract for the xshadertoy shaders.

The vendored Shadertoy-format shaders write *display-referred* colours:
they do their own gamma (pow(col, 1/2.2)) and expect to be shown as-is.
The harness must therefore request a LINEAR (non-sRGB) EGL window
surface -- an sRGB-tagged surface encodes their output a second time and
the monitor looks washed out while glReadPixels (the frame dump) returns
the un-encoded value.

This test:
  1. pins that the harness asks for EGL_GL_COLORSPACE_LINEAR, and
  2. if glslangValidator is on PATH, compiles the production wrapper for
     vendor/xshadertoy/glsl/neonhorizon.glsl under GLSL ES 3.00.
"""

import ast
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

SRC = pathlib.Path(
    os.environ.get("NCZ_SRC_DIR")
    or (pathlib.Path(__file__).resolve().parents[2] / "src")
)
ROOT = SRC.parent
GLSL = ROOT / "vendor" / "xshadertoy" / "glsl"
GLSLANG = shutil.which("glslangValidator")


def c_string_block(text, var):
    """Concatenate the C string literals assigned to `var`.

    Parses the same preamble/tail the production driver uses so the
    validation cannot silently drift from what actually ships.
    """
    # Collect the C string-literal lines after the assignment.  We scan
    # line-by-line rather than regex the whole assignment: a repeated
    # `(?:\s*"..."\s*)+` pattern backtracks catastrophically on the
    # ~100-line preamble.
    lines = text.splitlines()
    start = next(
        (
            i
            for i, l in enumerate(lines)
            if re.search(r"\b" + re.escape(var) + r"\s*=", l)
        ),
        None,
    )
    assert start is not None, f"could not find {var}"
    chunk = []
    for line in lines[start:]:
        chunk.append(line)
        # The assignment's final line ends the literal run with `";`.
        if line.rstrip().endswith(";") and '"' in line:
            break
    parts = re.findall(r'"(?:[^"\\]|\\.)*"', "\n".join(chunk))
    assert parts, f"no string literals for {var}"
    return "".join(ast.literal_eval(p) for p in parts)


def skip_leading_directives(src):
    """Mirror src/gles3_xshadertoy.c::skip_leading_directives()."""
    lines = src.splitlines(keepends=True)
    i = 0
    while i < len(lines):
        stripped = lines[i].lstrip(" \t")
        if stripped == "" or stripped.startswith(("\n", "//")):
            i += 1
            continue
        if stripped.startswith("#") and re.match(
            r"#(version|extension|pragma|precision)\b", stripped
        ):
            i += 1
            continue
        break
    return "".join(lines[i:])


class ShaderColorspace(unittest.TestCase):
    def test_harness_requests_linear_surface(self):
        gl = (SRC / "ncz_gl.c").read_text()
        self.assertIn("EGL_GL_COLORSPACE_LINEAR", gl)
        self.assertIn("EGL_GL_COLORSPACE", gl)
        # The chosen config must be able to fall back when the extension
        # is absent rather than failing to start.
        self.assertIn("EGL_KHR_gl_colorspace", gl)

        harness = (SRC / "gles3_harness.c").read_text()
        # The surface attribute is the request drivers honour most
        # reliably; both config and surface carry the linear tag.
        self.assertIn("EGL_GL_COLORSPACE_LINEAR", harness)
        self.assertIn("surf_attr", harness)
        # And sRGB writes are pinned off on the live surface.
        self.assertIn("glDisable(GL_FRAMEBUFFER_SRGB)", harness)

    def test_neonhorizon_outputs_display_referred_colour(self):
        shader = (GLSL / "neonhorizon.glsl").read_text()
        self.assertRegex(shader, r"pow\(\s*col\s*,\s*vec3\(\s*1\.0\s*/\s*2\.2\s*\)")
        # No large-argument sin() hashing (vendor-divergent).
        self.assertNotIn("sin(n * 12.9898)", shader)
        self.assertIn("hash21", shader)

    def test_wrapper_tail_forces_opaque_output(self):
        # The shared xshadertoy wrapper (src/xst_core.c, the assembler the
        # production driver uses) must force alpha 1.0: several vendored
        # ports leave .a at 0 and a layer-shell surface with alpha 0
        # composites to nothing (the blank-saver defect).
        core = (SRC / "xst_core.c").read_text()
        tail = c_string_block(core, "XST_TAIL")
        self.assertIn("vec4(col.rgb, 1.0)", tail)
        self.assertNotIn("frag_color = col;", tail)

    @unittest.skipUnless(GLSLANG, "glslangValidator not installed")
    def test_neonhorizon_validates_as_gles300(self):
        # The assembler lives in src/xst_core.c on the Cygnus integration
        # line (the 0.7.x line had it inline in gles3_xshadertoy.c).
        core = (SRC / "xst_core.c").read_text()
        preamble = c_string_block(core, "XST_PREAMBLE")
        tail = c_string_block(core, "XST_TAIL")
        body = skip_leading_directives((GLSL / "neonhorizon.glsl").read_text())
        full = "#version 300 es\n#define NCZ_QUALITY 2\n" + preamble + "\n" + body + tail
        self.assertIn("#version 300 es", full)
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / "neonhorizon.frag"
            path.write_text(full)
            proc = subprocess.run(
                [GLSLANG, "-S", "frag", str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(
            proc.returncode,
            0,
            f"glslangValidator failed:\n{proc.stdout}\n{proc.stderr}",
        )


if __name__ == "__main__":
    unittest.main()
