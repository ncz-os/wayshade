# Ported classics: what each port kept and replaced

The per-saver record for the eleven ported xscreensaver screensavers. Each
entry is the header comment of the port itself. The page that describes the
group is [ported-classics.md](ported-classics.md).

The per-saver detail below is taken from the header comment of each
`src/classics_shader/cs_*.c`.

### Cityflow

Kept from the original: a field of skewed square-ish towers whose heights
follow moving interference waves (itself adapted from
xscreensaver's `hacks/interference.c` by Hannu Mallat), coloured by height through a smooth random colour map,
viewed from above at a slant, lit by one light fixed to the model, the
background being the first colour map entry.

Why the port exists: the original set one `glMaterialfv` per tower inside
a single `glBegin(GL_QUADS)`, so a compatibility layer that applies a
material as a per-batch uniform made every tower take the last tower's
colour and merged the flat tops into one polygon. Here each tower is an
instance with its own colour, drawn in a single instanced call at the
original 800 towers.

Notice: xscreensaver, © 2014-2017 Jamie Zawinski.

### Crackberg

Kept line for line from the original: the triangular-tile terrain
generator with midpoint displacement and shared edges, the grow / fall /
yeast morphs that build and dismantle tiles at the edge of the view, the
visibility walk, the drunken camera, the four colour schemes (plain, ice,
magma, vomit) with their water and lava, and flat per-triangle lighting.

Replaced: each tile used to be compiled into a display list drawn with
`glBegin`. A tile is now generated once into a static vertex buffer
(position, colour, normal, water flag) and drawn with one call and a model
matrix uniform; the two-sided light of the original runs in the fragment
shader. `enhanced` style adds distance fog into a sky gradient, a fill
light so the back-lit slopes keep their shape, glossy water and glowing
lava, and FXAA.

Licence notice: xscreensaver, `crackberg` © 2005 Matus Telgarsky.

### Cubestorm

Kept from the original: a string of hollow, bevelled cube frames trailing
behind wandering, tumbling leaders (each following the first with its own
spin), a history of up to `length` frames redrawn every frame, colours
walking through a smooth random colour map, the periodic short "no trails"
resets with a fresh colour map, the light from (1,1,1) with a sharp white
specular, and the 30 degree view from 45 units.

Changed: the original issued one display-list call per history cube (about
800 per frame) through immediate mode. The cube frame is now one static
vertex buffer drawn with a single instanced call.

Notice: xscreensaver, © 2003-2018 Jamie Zawinski (the xscreensaver source
carries the full contributor list).

### Data Towers

This is the port of upstream's `gibson`, renamed at the catalogue level:
the launcher keeps `gibson_gles3` as a deprecated id and maps it to
`datatowers_gles3` (`RENAMED_IDS` in `launcher/ncz-screensaver`), and the
source file is still `src/classics_shader/cs_gibson.c`.

Kept from the original: the grid of glowing translucent towers that rise
and the motion of the grid.

Changed: the original compiled 360 display lists with per-quad texture
coordinates; the towers are now drawn from static buffers in one
instanced call.

Notice: xscreensaver, © 2020-2025 Jamie Zawinski.

### Geodesic

Kept from the original: the geodesic sphere built by recursively halving
the faces of an icosahedron, its frequency creeping up and down between 0
and count-1 with the new faces fading in and then morphing out onto the
sphere, the four modes (`mesh` — every face a triangular frame with a
triangular hole, `solid`, `stellated`, `stellated2`), the smooth random
colour map for the two frame colours, the spin and wander of the rotator,
and the light from (1,1,1) with a sharp white specular.

Changed: the original rebuilt up to 1280 faces (each 9 quads) with
`glBegin` every frame. The faces of each frequency are now generated once
into a static instance buffer.

Notice: xscreensaver, © 2013-2014 Jamie Zawinski.

### Hexstrut

Kept from the original: the triangular lattice whose triangles each carry
three struts (a "Y") that rotate by a third of a turn, the propagation of a
spin from a random triangle to its neighbours after a short delay, the
smooth random colour map advancing while a triangle rotates, and the slowly
spinning and wandering view (30 degree perspective, 30 units back).

Changed: the struts are instanced (one instance per triangle, 18 vertices
generated in the vertex shader) instead of `glBegin(GL_QUADS)` per frame.
`enhanced` style adds anti-aliased strut edges, a glow that brightens the
struts while they turn, and a soft vignette.

Notice: xscreensaver, © 2016-2017 Jamie Zawinski.

### Noof

Kept from the original: seven wandering "flowers" of 2 to 19 petals and the
motion that carries them.

Provenance of the original: it was a demo shipped with GLUT (apparently
called "diatoms" on Irix), ported to raw GL and then to xscreensaver by
Jamie Zawinski on 12 February 2004.

Notice: xscreensaver, © 2004-2018 Bill Torzewski.

### Voronoi

Kept: the simulation (sites appear near the middle in bursts, drift with
slowly growing random acceleration, the whole field zooms outward every
zoom-delay seconds so old sites leave and new ones are added), the random
smooth colour map, the flat coloured cells (each pixel takes the colour of
its nearest site, measured in the same normalised 0..1 by 0..1 space, so
cell shapes are stretched with the screen exactly as before) and the small
rotating five-spike marker on each site.

Changed: no immediate mode. `classic` style draws each site as an
instanced cone into the depth buffer, one draw call. `enhanced` style
computes the cells in a fragment shader from a uniform buffer of sites,
which also gives the distance to the nearest cell border, so borders are
anti-aliased and glow, and new sites fade in from the colour of the cell
they were born in.

Notice: xscreensaver, © 2007-2018 Jamie Zawinski.

## What the three upstream-GLSL ports are

These three were already fragment shaders upstream, so the port is build
glue only: the harness compiles them, feeds them the standard uniforms and
manages the surface.

### Hypertorus

A hyperboloid-of-one-sheet wireframe swept through time. © 2003-2026
Carsten Steger. Notice: xscreensaver.

### Klein

The Klein bottle immersion drawn as a wireframe. © 2005-2026 Carsten
Steger. Notice: xscreensaver. This saver is listed in
`assets/screensaver-chooser/broken.tsv` with status `suspect` and the
reason "large single-color areas, low color count"; the launcher excludes
it from the random and playlist pools but you can still run it on request.

### Projectiveplane

The projective plane drawn as a tiling. © 2013-2026 Carsten Steger.
Notice: xscreensaver.