# Ported work and attribution

* **Black Hole** - Schwarzschild geodesic raytracer after Adriwin06/black-hole (MIT),
  `vendor/blackhole-PORTED.md`.
* **hyprsaver** - 35 fragment shaders by Mara Vexa (MIT), `vendor/hyprsaver/LICENSE`.
* **xshadertoy** - Shadertoy-API shaders from xscreensaver 6.16 and originals; the
  license identifier of each shader is in its header, `vendor/xshadertoy/PORTED.md`.
* **Classics** - xscreensaver hacks. voronoi, gravitywell, hexstrut, cubestorm, cityflow,
  geodesic, gibson (c) Jamie Zawinski; crackberg (c) Matus Telgarsky; noof (c) Bill
  Torzewski; hypertorus, klein, projectiveplane (c) Carsten Steger. The nine shader-engine
  ports in `src/classics_shader/` keep the original permission notice in each file header;
  hypertorus, Klein and projectiveplane are the upstream GLSL sources with the
  fixed-function fallback removed. Details: `docs/CLASSICS-SHADER-PORT.md`.
* **fractasia** - 17 GPU shader kernels (13 original plus 4 adapted from hyprsaver, MIT) in `vendor/fractasia/glsl/`
  implementing the Fractint catalogue coverage documented in
  `docs/fractasia/`. Original to ncz-screensavers (Apache-2.0);
  no Fractint source code, formulae, or restricted assets were copied
  (license analysis: `docs/fractasia/LICENSE-ANALYSIS.md`).

The earlier OpenGL 1 route (a GL1 emulation layer and gl4es) was retired; its history is
in `docs/archive/gl1-era/`.
