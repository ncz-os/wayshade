# meson.build - native Wayland/EGL GLES3 screensavers.
#
# Every binary is a GLES3 program driven by src/gles3_harness.c (Wayland
# layer-shell + EGL).  The ship set
# below is what the ncz-screensavers package installs; the other targets are
# development helpers.

project(
    'ncz-screensavers',
    'c',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        # Default -O0/-g; downstream packagers usually override.
    ],
    version: '0.8.16',
    license: 'Apache-2.0',
)

# ---------------------------------------------------------------------------
# Ship set (operator-ratified 2026-09-29) — the ONLY executables that are
# installed into the ncz-screensavers package. Everything else in this file
# builds for development/validation only (install: false).
# ---------------------------------------------------------------------------
ncz_libdir = get_option('prefix') / 'lib' / 'ncz-screensavers'
ncz_datadir = get_option('prefix') / 'share' / 'ncz-screensavers'
ncz_docdir = get_option('prefix') / 'share' / 'doc' / 'ncz-screensavers'
ncz_ship_bins = [
    'blackhole_gles3',
    'hyprsaver_attitude_gles3',
    'hyprsaver_aurora_gles3',
    'hyprsaver_bezier_gles3',
    'hyprsaver_blob_gles3',
    'hyprsaver_caustics_gles3',
    'hyprsaver_circuit_gles3',
    'hyprsaver_clouds_gles3',
    'hyprsaver_donut_gles3',
    'hyprsaver_fireflies_gles3',
    'hyprsaver_flames_gles3',
    'hyprsaver_geometry_gles3',
    'hyprsaver_hypercube_gles3',
    'hyprsaver_kaleidoscope_gles3',
    'hyprsaver_lissajous_gles3',
    'hyprsaver_marble_gles3',
    'hyprsaver_digitalrain_gles3',
    'hyprsaver_oscilloscope_gles3',
    'hyprsaver_planet_gles3',
    'hyprsaver_plasma_gles3',
    'hyprsaver_snowfall_gles3',
    'hyprsaver_sonar_gles3',
    'hyprsaver_starfield_gles3',
    'hyprsaver_ticker_gles3',
    'hyprsaver_temple_gles3',
    'hyprsaver_terminal_gles3',
    'hyprsaver_arccoil_gles3',
    'hyprsaver_tunnel_gles3',
    'hyprsaver_voronoi_gles3',
    'hyprsaver_waterfall_gles3',
    'hyprsaver_wormhole_gles3',
    'xshadertoy_alienbeacon_gles3',
    'xshadertoy_batteredplanet_gles3',
    'xshadertoy_bestill0-0_gles3',
    'xshadertoy_bestill1-0_gles3',
    'xshadertoy_bestill2-0_gles3',
    'xshadertoy_bestill3-0_gles3',
    'xshadertoy_bestill4-0_gles3',
    'xshadertoy_bestill5-0_gles3',
    'xshadertoy_darktransit_gles3',
    'xshadertoy_downfall_gles3',
    'xshadertoy_elementalring_gles3',
    'xshadertoy_fluxcore_gles3',
    'xshadertoy_gimbalharmonics_gles3',
    'xshadertoy_goldenapollian_gles3',
    'xshadertoy_hexplasma_gles3',
    'xshadertoy_logarithmiccircles_gles3',
    'xshadertoy_neongravity_gles3',
    'xshadertoy_neontriangulator_gles3',
    'xshadertoy_noxfire_gles3',
    'xshadertoy_polarnight_gles3',
    'xshadertoy_prococean_gles3',
    'xshadertoy_protophore_gles3',
    'xshadertoy_rigrekt_gles3',
    'xshadertoy_skyline_gles3',
    'xshadertoy_starnest_gles3',
    'xshadertoy_neonhorizon_gles3',
    'xshadertoy_topologica_gles3',
    'xshadertoy_trainmandala_gles3',
    'xshadertoy_trizm_gles3',
    'xshadertoy_truchetzoom_gles3',
    'voronoi_gles3',
    'projectiveplane_gles3',
    'klein_gles3',
    'hypertorus_gles3',
    'cubestorm_gles3',
    'hexstrut_gles3',
    'crackberg_gles3',
    'cityflow_gles3',
    'geodesic_gles3',
    'noof_gles3',
    'datatowers_gles3',
    'fractasia_gles3',
    'magmasimplex_gles3',
]

# ---------------------------------------------------------------------------
# Build options — see `meson configure` for the full list.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Core dependencies — always required.
# ---------------------------------------------------------------------------

# Vala project arguments for the Singularity plugin. Must be set BEFORE
# any target is declared; doing it inside plugin/meson.build (which is
# processed after most targets are declared here) is too late on newer
# meson releases.
if get_option('singularity-plugin').enabled()
    add_languages('vala', required: true)
    add_project_arguments(
        '--pkg=posix',
        '--vapidir=' + get_option('singularity-vapidir'),
        language: 'vala',
    )
endif

dep_wayland_client = dependency('wayland-client', required: true)
dep_wayland_egl    = dependency('wayland-egl',    required: true)
dep_egl            = dependency('egl',            required: true)
dep_glesv2         = dependency('glesv2',         required: true)

# libm — vendored xscreensaver code uses cosf/sinf/sqrtf/etc., and so do
# our own GLES3 helper modules. Neither libGLESv2 nor libwayland-client
# drags libm in transitively, so we declare it explicitly.
cc = meson.get_compiler('c')
dep_m = cc.find_library('m', required: false)
add_project_arguments('-DNCZ_VERSION="@0@"'.format(meson.project_version()), language: 'c')

# libpng — used by the harness frame dump and the smoke testers.
dep_png = dependency('libpng', required: false)


# ---------------------------------------------------------------------------
# wayland-scanner — generates both headers (client-side) and the private
# bindings code (no dlopen, all symbols static). One custom_target per
# protocol so build graphs are clean and incremental.
# ---------------------------------------------------------------------------

wayland_scanner = find_program('wayland-scanner', required: true)

wayland_scanner_client_args  = ['client-header']
wayland_scanner_privcode_args = ['private-code']

protocols = {
    'wayland':           'protocols/wayland.xml',
    'xdg-shell':         'protocols/xdg-shell.xml',
    'wlr-layer-shell-unstable-v1': 'protocols/wlr-layer-shell-unstable-v1.xml',
    'viewporter':        'protocols/viewporter.xml',
}

gen_sources = []
foreach name, xml_path : protocols
    base = name + '-client-protocol'
    h_target = custom_target(
        name + '_header',
        input:  xml_path,
        output: base + '.h',
        command: [wayland_scanner, wayland_scanner_client_args,
                  '@INPUT@', '@OUTPUT@'],
        depend_files: files(xml_path),
    )
    c_target = custom_target(
        name + '_private_code',
        input:  xml_path,
        output: base + '.c',
        command: [wayland_scanner, wayland_scanner_privcode_args,
                  '@INPUT@', '@OUTPUT@'],
        depend_files: files(xml_path),
    )
    gen_sources += [h_target, c_target]
endforeach

# Add the build directory to the include path so generated headers are
# reachable via `#include "wayland-client-protocol.h"` etc. The source
# root is added so source files can find them via relative path.
inc = include_directories('.', 'src', 'include')

# Build-wide -Wall -Wextra. All our code (and the shim layer) must stay
# warning-clean under these. Vendored third-party code (xscreensaver
# hacks) is NOT held to this bar — it gets c_args below that suppress
# relevant warnings where unavoidable.
common_c_args = ['-Wall', '-Wextra']

# -------------------------------------------------------------------
# GLES3 harness binaries
# -------------------------------------------------------------------

common_gles3_sources = [
    'src/ncz_hack_shim.c',
    'src/ncz_gl.c',
    'src/ncz_platform.c',
    'src/ncz_gpu_tier.c',
    'src/ncz_options.c',
    'src/ncz_render.c',
    'src/ncz_gpu_guard.c',
    'src/ncz_stats.c',
    'src/ncz_harness_cfg.c',
    # Off-screen frame-source mode (ncz-screensaver-feed). Every hack binary
    # gains it; it is inert unless NCZ_FEED_SOCKET is set. See
    # src/ncz_feed_render.c and include/lock_framesrc_proto.h.
    'src/ncz_feed_render.c',
    # The cadence helper is split out so the unit test can link it without
    # dragging the entire EGL/GLES/GBM stack in.
    'src/ncz_feed_render_fps.c',
]
# GLES3-native pilots need -DNCZ_GLES3_BUILD=1 so that the glu*
# helpers in xscreensaver_compat.c compile to empty stubs (they
# rely on glMultMatrixf/glTranslated which don't exist in GLES3).
# -DUSE_GL is required by the vendored xscreensaver hack sources — most
# of them are wrapped in '#ifdef USE_GL /* whole file */' (see tangram.c,
# glcells.c, etc.). Without it the file compiles to an empty translation
# unit (no errors, no symbols), which surfaces as an "undefined reference
# to <hack>_xscreensaver_function_table" link failure — and crucially
# there are no prior compiler complaints to point at it.
gles3_native_c_args = common_c_args + [
        '-DUSE_GL',
        '-D_GNU_SOURCE',
        '-DGETTIMEOFDAY_TWO_ARGS=1',
        '-DHAVE_UNISTD_H=1',
        '-Wno-unused-parameter',
        '-Wno-unused-but-set-variable',
        '-Wno-unused-variable',
        '-Wno-sign-compare',
        '-Wno-missing-field-initializers',
        '-DNCZ_GLES3_BUILD=1',
    ]

# Shared GLES3-native dependency list: Wayland + EGL + GLESv2 + libpng
# (for image_data_to_ximage on hacks that need texture decoding)
# + libm (vendored xscreensaver code and our GLES3 helpers use
# cosf/sinf/sqrtf etc — neither libGLESv2 nor libwayland-client
# drags libm in transitively on Debian aarch64).
# Off-screen frame-source mode helpers linked into every hack binary:
#   * src/ncz_fbo_redirect.c (--wrap=glBindFramebuffer): there is no window and
#     so no framebuffer 0; a bind of 0 lands on the frame-source FBO;
#   * src/ncz_time_offset.c (--wrap=clock_gettime): time spent suspended behind
#     the unlock prompt is taken out of the monotonic clocks, so a resumed saver
#     continues where it stopped.
# Both pass through unchanged on screen.
dep_fbo_redirect = declare_dependency(
    sources: files('src/ncz_fbo_redirect.c', 'src/ncz_time_offset.c'),
    link_args: ['-Wl,--wrap=glBindFramebuffer', '-Wl,--wrap=clock_gettime'],
)

common_gles3_deps = [
    dep_wayland_client,
    dep_wayland_egl,
    dep_egl,
    dep_glesv2,
    dep_png,
    dep_m,
    dep_fbo_redirect,
]

# Shader-engine ports of the xscreensaver classics (src/classics_shader/), built
# by the block at the end of this file.  The GL1 immediate-mode builds of these
# nine hacks (and the -Dlegacy-classics option) were retired; see
# docs/CLASSICS-SHADER-PORT.md.
classics_shader_ported = ['voronoi', 'gravitywell', 'hexstrut', 'cubestorm', 'crackberg', 'cityflow', 'noof', 'geodesic', 'gibson']
# extra vendored xscreensaver helper sources a port still needs (rotators, RNG)
classics_shader_extras = {
    'hexstrut': ['src/rotator.c', 'src/yarandom.c'],
    'cubestorm': ['src/rotator.c', 'src/yarandom.c'],
    'geodesic': ['src/rotator.c', 'src/yarandom.c'],
    'gibson': ['src/rotator.c', 'src/yarandom.c'],
}


# -----------------------------------------------------------------------
# Round 13 — Phase 3 — hyprsaver shader port (vendor/hyprsaver/, MIT, Mara Vexa).
# 35 GLSL fragment shaders (all #version 320 es, GLES 3.2-native).
# One .c file (src/gles3_hyprsaver.c) is reused for all 35 targets —
# the foreach picks which .frag to compile and which symbol stem to
# use. License preserved at vendor/hyprsaver/LICENSE; attribution
# in PORTED.md.
# -----------------------------------------------------------------------

hyprsaver_shaders = [
    'attitude', 'aurora', 'bezier', 'blob', 'caustics', 'circuit',
    'clouds', 'donut', 'fireflies', 'flames', 'geometry',
    'gridwave', 'hypercube', 'kaleidoscope', 'lissajous',
    'marble', 'matrix', 'oscilloscope', 'planet', 'plasma',
    'snowfall', 'sonar', 'starfield', 'stonks', 'temple',
    'terminal', 'tesla', 'tunnel', 'voronoi', 'waterfall', 'wormhole',
]

# Absolute runtime asset directory matches both loaders, including builds
# configured with the default /usr/local prefix. DESTDIR staging still works.
shader_install_dir = ncz_datadir / 'shaders'
install_data('vendor/blackhole/blackhole.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
# Shaders of non-ship hacks are installed only with -Dinstall-extras=true
# (development installs); the ncz-screensavers package ships the ratified set.
# magmasimplex is in ncz_ship_bins as of 0.8.5, so its shaders must be installed
# with the package too: the 0.8.5 build shipped the binary without goo.frag and
# every goo-engine run failed with "magmasimplex: cannot locate goo.frag".
install_extras = get_option('install-extras')
ms_shaders = install_extras or ncz_ship_bins.contains('magmasimplex_gles3')
if ms_shaders
install_data('vendor/magmasimplex/magmasimplex.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
endif
foreach ms_shader : ms_shaders ? [
  'goo', 'render', 'sim_zero', 'sim_advect_v', 'sim_advect_t', 'sim_advect_p',
  'sim_advect_h', 'sim_buoyancy', 'sim_project', 'sim_heat',
  'sim_phase', 'sim_init_seed',
  'sim3d_advect_v', 'sim3d_advect_t', 'sim3d_advect_p', 'sim3d_advect_h',
  'sim3d_buoyancy', 'sim3d_project', 'sim3d_heat', 'sim3d_phase',
  'sim3d_init_seed',
] : []
    install_data('vendor/magmasimplex/' + ms_shader + '.frag',
                 install_dir: shader_install_dir + '/magmasimplex',
                 install_tag: 'runtime-shaders')
endforeach
# Catalog ids differ from the vendored shader file names for these three.
hyprsaver_ids = {'matrix': 'digitalrain', 'stonks': 'ticker', 'tesla': 'arccoil'}
foreach s : hyprsaver_shaders
    hid = hyprsaver_ids.get(s, s)
    if ncz_ship_bins.contains('hyprsaver_' + hid + '_gles3')
        install_data('vendor/hyprsaver/shaders/' + s + '.frag',
                     install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    endif
    executable(
        'hyprsaver_' + hid + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_hyprsaver.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            # Quoted because meson/gcc splits 'tunnel.frag' on the dot.
            '-DSHADER_FILE="@0@.frag"'.format(s),
            '-DHACK_PREFIX=hyprsaver_@0@'.format(hid),
            '-DHACK_TABLE=hyprsaver_@0@_xscreensaver_function_table'.format(hid),
        ],
        install: ncz_ship_bins.contains('hyprsaver_' + hid + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# -----------------------------------------------------------------------
# Round 16 — xshadertoy shader port (vendor/xshadertoy/glsl/, MIT /
# CC0 / CC BY 3.0 / public domain; per-shader headers preserved; see
# vendor/xshadertoy/PORTED.md).
#
# 38 single-pass Shadertoy-API GLSL fragment shaders vendored from
# upstream xscreensaver 6.16 (commit b99f621, 2026-09-03). One
# generic driver (`src/gles3_xshadertoy.c`) is reused for all 38 via
# -D flags, matching the upstream pattern (one ~20-line bash
# wrapper per shader) and the pattern of this repo's hyprsaver
# shaders (PORTED.md §13.3).
# -----------------------------------------------------------------------

xshadertoy_shaders = [
    'alienbeacon', 'batteredplanet',
    'bestill0-0', 'bestill1-0', 'bestill2-0', 'bestill3-0',
    'bestill4-0', 'bestill5-0',
    'caustictidepool', 'darktransit', 'downfall',
    'elementalring', 'fluxcore', 'gimbalharmonics', 'goldenapollian',
    'hexlattice', 'hexplasma', 'iridescence', 'logarithmiccircles',
    'neongravity', 'neongravity-0', 'neongravity-1', 'neonhorizon', 'neontriangulator',
    'noxfire', 'polarnight', 'prococean', 'protophore',
    'rigrekt', 'selfreflect', 'skyline', 'stardome', 'starnest',
    'stratus', 'stripeytorus', 'topologica', 'trainmandala',
    'trizm', 'truchetzoom', 'universeball',
    'physarum',
    'attractor',
    'caustics',
    'aurora',
    'marble',
    'wellcurve',
    'chitin',
]

# ridgeline.glsl stays in vendor/xshadertoy/glsl/ but is deliberately NOT
# installed: it is a calibration/stress shader, not catalogue content.
# Measured on an Intel UHD CML GT2 iGPU at 1920x1080 it managed 4 frames
# in 10s where cellmosaic and hexlattice both hit the 600-frame vsync cap,
# so it is useful for GPU-tier benchmarking and unusable as a screensaver.

# Shared include of the xshadertoy_opts savers (#include "xst_common.glsl").
# It must be installed whenever a saver that includes it is in the ship set,
# not only for -Dinstall-extras: the in-house neongravity is the first shipped
# saver that includes it, and without this the installed binary cannot resolve
# the include at runtime.
if install_extras or ncz_ship_bins.contains('xshadertoy_neongravity_gles3')
    install_data('vendor/xshadertoy/glsl/xst_common.glsl',
                 install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif

foreach s : xshadertoy_shaders
    xshadertoy_extra_c_args = []
    if s == 'neongravity-0'
        xshadertoy_extra_c_args += ['-DNCZ_NEONGRAVITY_FXAA']
    endif
    if ncz_ship_bins.contains('xshadertoy_' + s + '_gles3')
        install_data('vendor/xshadertoy/glsl/' + s + '.glsl',
                     install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    endif
    # Hyphen-to-underscore substitution for the C identifier stem.
    # The shader basenames `bestill0-0`..`bestill5-0`, `neongravity-0`
    # and `neongravity-1` carry hyphens which are illegal in C
    # identifiers; the driver .c has separate HACK_PREFIX (display)
    # and HACK_PREFIX_ID (C-symbol stem) macros for that reason.
    s_id = s.replace('-', '_')
    executable(
        'xshadertoy_' + s + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_xshadertoy.c',
            'src/xst_core.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DSHADER_FILE="@0@.glsl"'.format(s),
            '-DHACK_PREFIX=xshadertoy_@0@'.format(s),
            '-DHACK_PREFIX_ID=xshadertoy_@0@'.format(s_id),
            '-DHACK_TABLE=xshadertoy_@0@_xscreensaver_function_table'.format(s_id),
        ] + xshadertoy_extra_c_args,
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# -----------------------------------------------------------------------
# Round 20 — wave 2 quiet-materiality shaders. Six original .glsl files
# under vendor/wave2-materia/glsl/. The driver
# (src/gles3_xshadertoy.c) is the same one as above; its shader-search
# prefixes were extended with vendor/wave2-materia/glsl/ entries so the
# binary resolves its .glsl from build tree or repo root. Each shader is
# shipped in two places: the build tree (so dev iteration works) and
# /usr/share/ncz-screensavers/shaders/ on install.
#
# The six are independent GLES3-native fragment shaders, all written
# from scratch (no Shadertoy ports — its default CC BY-NC-SA licence
# is non-commercial and unshippable). Each follows the wave-2 brief:
# light RECEIVED rather than emitted. See
# docs/superpowers/specs/wave2-materia.md for the perceptual brief
# and the per-shader design intent.
# -----------------------------------------------------------------------

wave2_materia_shaders = [
    'caustics-on-stone',
    'satin-wind',
    'foliage-plaster',
    'frosted-glass',
    'thin-film',
]

# -----------------------------------------------------------------------
# JurassicAquarium — procedural prehistoric sea at the waterline.
# Same generic driver as xshadertoy/wave2-materia; only the shader search
# prefix differs (src/gles3_xshadertoy.c was extended for
# vendor/jurassicaquarium/glsl/). Design doc:
# docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md
#
# NOT installed: this is the skeleton build (current field + seaweed +
# waterline), measured on a dGPU only. It stays out of the catalogue until
# it has a real iGPU measurement. Building it is what makes that
# measurement possible at all.
# -----------------------------------------------------------------------

jurassicaquarium_shaders = [
    'jurassicaquarium_skeleton',
]

foreach s : jurassicaquarium_shaders
    s_id = s.replace('-', '_')
    executable(
        'xshadertoy_' + s + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_xshadertoy.c',
            'src/xst_core.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DSHADER_FILE="@0@.glsl"'.format(s),
            '-DHACK_PREFIX=xshadertoy_@0@'.format(s),
            '-DHACK_PREFIX_ID=xshadertoy_@0@'.format(s_id),
            '-DHACK_TABLE=xshadertoy_@0@_xscreensaver_function_table'.format(s_id),
        ],
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

foreach s : wave2_materia_shaders
    if install_extras
        install_data('vendor/wave2-materia/glsl/' + s + '.glsl',
                     install_dir: shader_install_dir,
                     install_tag: 'runtime-shaders')
    endif
    s_id = s.replace('-', '_')
    executable(
        'xshadertoy_' + s + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_xshadertoy.c',
            'src/xst_core.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DSHADER_FILE="@0@.glsl"'.format(s),
            '-DHACK_PREFIX=xshadertoy_@0@'.format(s),
            '-DHACK_PREFIX_ID=xshadertoy_@0@'.format(s_id),
            '-DHACK_TABLE=xshadertoy_@0@_xscreensaver_function_table'.format(s_id),
        ],
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# -----------------------------------------------------------------------
# Round 19 — gl-transitions shader port (vendor/gl-transitions/,
# 123 MIT / 1 BSD-3-Clause / 1 BSD-2-Clause; per-file Author +
# License headers preserved; see vendor/gl-transitions-PORTED.md).
#
# 125 transition shaders vendored from gl-transitions/gl-transitions
# upstream commit 902218a (2026-09-25). One generic driver
# (`src/gles3_transitions.c`) is reused for all 125 via -D flags —
# the same shape as the xshadertoy driver above. Each transition
# compiles to one executable `gles3_transition_<name>_gles3`.
# Shaders install to /usr/share/ncz-screensavers/shaders/ alongside
# the other shader resources; the driver resolves them by searching
# build tree, then that absolute path. The "from /" launch path is
# exercised in evidence capture.
#
# Note: a transition is *not* a screensaver — see
# docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md
# for why. The driver binary is built and shipped as a transition
# infrastructure layer; it is exercised via NCZ_FRAME_DUMP-driven
# evidence captures and integrated with the chooser rotation /
# neonspacewar mode-switch path (separate commits).
# -----------------------------------------------------------------------

gles3_transitions_list = [
    'AdvancedMosaic', 'angular', 'BlockDissolve', 'BookFlip', 'Bounce',
    'BowTieHorizontal', 'BowTieVertical', 'BowTieWithParameter', 'Box',
    'burn', 'burn0', 'ButterflyWaveScrawler', 'cannabisleaf',
    'chessboard', 'circle', 'CircleCrop', 'circleopen', 'colorphase',
    'ColourDistance', 'coord-from-in', 'CrazyParametricFun',
    'crosshatch', 'crosswarp', 'CrossZoom', 'cube', 'DefocusBlur',
    'Directional', 'directional-easing', 'DirectionalScaled',
    'directionalwarp', 'directionalwipe', 'displacement', 'dissolve',
    'DoomScreenTransition', 'doorway', 'Dreamy', 'DreamyZoom',
    'Drop_Zone_Flicker', 'EdgeTransition', 'fade', 'fadecolor',
    'fadegrayscale', 'FilmBurn', 'flyeye', 'Fold', 'fragment',
    'GlitchDisplace', 'GlitchMemories', 'GridFlip', 'heart',
    'hexagonalize', 'HorizontalClose', 'HorizontalOpen', 'HSVfade',
    'InvertedPageCurl', 'kaleidoscope', 'LeftRight', 'LinearBlur',
    'luma', 'luminance_melt', 'morph', 'Mosaic', 'mosaic_transition',
    'multiply_blend', 'old_tv_lost_signal', 'Overexposure',
    'parametric_glitch', 'perlin', 'pinwheel', 'pixelize',
    'polar_function', 'PolkaDotsCurtain', 'powerKaleido', 'PuzzleRight',
    'Radial', 'randomNoisex', 'randomsquares', 'Rectangle',
    'RectangleCrop', 'Revolve_Left', 'ripple', 'Rolls',
    'rotate_scale_fade', 'RotateScaleVanish', 'rotateTransition',
    'scale-in', 'SimpleFlip', 'SimpleZoom', 'SimpleZoomOut', 'Slides',
    'splitSlideInHorizontal', 'splitSlideInOutHorizontal',
    'splitSlideInOutVertical', 'splitSlideInVertical',
    'splitSlideOutHorizontal', 'splitSlideOutVertical', 'squareswire',
    'squeeze', 'StarWipe', 'StaticFade', 'static_wipe', 'StereoViewer',
    'StripDatamoshGlitch', 'swap', 'Swirl', 'tangentMotionBlur',
    'TilesWave', 'TopBottom', 'TVStatic', 'undulatingBurnOut',
    'VerticalClose', 'VerticalOpen', 'WaterDrop', 'wind',
    'windowblinds', 'windowslice', 'wipeDown', 'wipeLeft', 'wipeRight',
    'wipeUp', 'x_axis_translation', 'ZoomInCircles', 'zoomInOut',
    'ZoomLeftWipe', 'ZoomRigthWipe',
]
assert(gles3_transitions_list.length() == 125,
       'gles3_transitions_list must contain exactly 125 transitions')

foreach t : gles3_transitions_list
    if install_extras
        install_data('vendor/gl-transitions/transitions/' + t + '.glsl',
                     install_dir: shader_install_dir,
                     install_tag: 'runtime-shaders')
    endif
    # Hyphens in upstream basenames (`coord-from-in`,
    # `directional-easing`, `scale-in`) break C identifiers — the
    # driver .c uses HACK_PREFIX for the display name and
    # HACK_PREFIX_ID for the C-symbol stem. Same substitution pattern
    # as the xshadertoy foreach above for `bestill0-0` etc.
    t_id = t.replace('-', '_')
    executable(
        'gles3_transition_' + t + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_transitions.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DTRANSITION_FILE="@0@.glsl"'.format(t),
            '-DHACK_PREFIX=gles3_transition_@0@'.format(t),
            '-DHACK_PREFIX_ID=gles3_transition_@0@'.format(t_id),
            '-DHACK_TABLE=gles3_transition_@0@_xscreensaver_function_table'.format(t_id),
        ],
        install: ncz_ship_bins.contains('gles3_transition_' + t + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# Headless smoke tester for the 125 transitions. Reuses the same
# preamble/body-rewrite as the production driver but is a
# self-contained EGL+GLES3 program with no harness dependency.
# See docs/audit/gl-transitions-evidence/ROUNDUP-2026-09-26.md for
# what this is for and how to drive it.
executable(
    'gles3_transitions_smoke',
    files(['src/gles3_transitions_smoke.c']),
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DSTANDALONE',
    ],
    install: ncz_ship_bins.contains('gles3_transitions_smoke'), install_dir: ncz_libdir,
)

# Headless renderer and GPU bench for the xshadertoy family: same options,
# seed, palette and director code as the driver (src/xst_core.c). Frames at
# exact times for contact sheets; --bench N for sustained GPU-time gates
# (tools/xshadertoy-perf-gate.py). Development only, never installed.
xshadertoy_render = executable(
    'xshadertoy_render',
    files(['src/xshadertoy_render.c', 'src/xst_core.c', 'src/ncz_options.c',
           'src/ncz_harness_cfg.c', 'src/ncz_render.c', 'src/ncz_gpu_guard.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_egl, dep_glesv2, dep_png, dep_m],
    install: false,
)

# Headless smoke tester for xshadertoy-format fragment shaders under
# vendor/xshadertoy/glsl/. Same pattern as gles3_transitions_smoke
# (EGL_KHR_surfaceless_context, PBuffer destination, real PNG dumps
# via libpng). Validates compile/link/render for individual shaders
# without needing a Wayland compositor or the full gles3_harness.
# Usage: gles3_xshadertoy_smoke <shader.glsl> <out-dir> [t1 t2 ...]
gles3_xshadertoy_smoke = executable(
    'gles3_xshadertoy_smoke',
    files(['src/gles3_xshadertoy_smoke.c']),
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args,
    install: ncz_ship_bins.contains('gles3_xshadertoy_smoke'), install_dir: ncz_libdir,
)

# xshadertoy-gl-clean: render a representative xshadertoy shader through the
# headless smoke tester for several frames and fail if the render path leaves
# glGetError != 0.  A persistent GL error (e.g. the 0x502 from a glUniform
# issued with no program bound, or a 0x500 from a GL1-only glEnable cap) is a
# latent bug that a correct PNG hides.  The smoke tester needs a live GLES3
# driver, so it is skipped in cross builds like megacity-coverage.
if not meson.is_cross_build()
    test('xshadertoy-gl-clean',
         gles3_xshadertoy_smoke,
         args: [
             meson.project_source_root() / 'vendor' / 'xshadertoy' / 'glsl' / 'neonhorizon.glsl',
             meson.current_build_dir() / 'xshadertoy-gl-clean',
             '5', '20',
         ],
         timeout: 300,
         is_parallel: false)
endif

# MIT Schwarzschild raytracer port; see vendor/blackhole-PORTED.md.
blackhole_exe = executable(
    'blackhole_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_blackhole.c',
        'src/blackhole_randomize.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=blackhole_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('blackhole_gles3'), install_dir: ncz_libdir,
)

# Round 18 — magmasimplex: rename + full rewrite of the lavafield
# prototype. Same line of work as lavafield (rising buoyant blobs in a
# coloured field, palette + blob count + viscosity + scale randomised
# per-launch) but:
#
#   - The "field" is now a real coloured transmissive liquid that fills
#     the frame, not blobs on black. Coverage target: >90% non-black.
#   - The blobs are translucent wax viewed THROUGH the liquid, so the
#     perceived hue is a product of the wax colour and the liquid hue
#     (Beer-Lambert-style transmission), not the wax composited over a
#     background. Yellow wax in blue liquid reads green on screen as
#     an emergent result of the optics, not by hardcoding green.
#   - Different blobs carry different hues simultaneously — the field
#     is multicoloured in a single frame, not monochrome.
#   - Lighting is a hot emitter low in frame with vertical luminance
#     falloff, not a uniform backlight. Wax pooled near the base is
#     the brightest thing on screen, frequently blown out.
#   - ~30 historical colourways (liquid / wax pairings) are randomised
#     per-launch from /dev/urandom. Internal names: ember, orchid,
#     ultraviolet, sunflower, opal. No manufacturer / brand / product
#     references in code, comments, docs, or commit messages.
#
# Replaces lavafield_gles3 (which measured 5% non-black and rendered
# monochrome on a mostly black field). Old src/lavalite.c and the
# lavalite_gles3 binary are dropped from the catalogue in this change —
# the legacy 1990s framing (small object on black) is now fully
# superseded. See docs/superpowers/rounds/2026-09-25-magmasimplex.md.
magmasimplex_exe = executable(
    'magmasimplex_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_magmasimplex.c',
        'src/magmasimplex_goo.c',
        'src/magmasimplex_goo_cfg.c',
        'src/magmasimplex_goo_sim.c',
        'src/magmasimplex_pattern.c',
        'src/magmasimplex_palettes.c',
        'src/magmasimplex_beat.c',
        'src/ncz_audio_bus.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=magmasimplex_xscreensaver_function_table',
    ] + (get_option('magma-audio') ? ['-DNCZ_AUDIO_CAPTURE'] : []),
    install: ncz_ship_bins.contains('magmasimplex_gles3'), install_dir: ncz_libdir,
)

# Round 19 — genxvectorcade: psychedelic vector-arcade journey. The
# second piece of the GenX vision: a non-interactive, ever-evolving
# visual that blends vector tunnel / grid horizon / starfield warp /
# kaleidoscope bloom / lattice canyon into one continuous trip with
# persistent feedback trails as the through-line. Differs from the
# neonspacewar piece (Round 18) in that there is no game and no
# geometry to model — the journey IS the artwork. See
# docs/superpowers/specs/2026-09-25-genxvectorcade-vision.md for the
# shared creative spine.
#
# Architecture: one driver .c (src/gles3_genxvectorcade.c) +
# one fragment shader (vendor/genxvectorcade/genxvectorcade.frag)
# loaded at runtime from the source tree or from
# /usr/share/ncz-screensavers/shaders/. The host owns a half-
# resolution RGBA8 ping-pong FBO for the feedback trail buffer (the
# feature that separates this from a generic tunnel shader) plus a
# tiny "fade + rotate" pass that spirals the wakes each frame. Trail
# buffer is intentionally half-resolution: full-resolution on Intel
# UHD nearly doubled per-frame cost (measured on build-host 2026-09-25)
# and trails are intentionally soft.
if install_extras
install_data('vendor/genxvectorcade/genxvectorcade.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
executable(
    'genxvectorcade_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_genxvectorcade.c',
        'src/ncz_platform.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=genxvectorcade_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('genxvectorcade_gles3'), install_dir: ncz_libdir,
)

# Ephemeris: the piece Claude designed (docs/superpowers/specs/
# 2026-09-25-ephemeris-chronicle.md, docs/EPHEMERIS.md). Matte surface,
# short-lived CPU workers that see only an asynchronously read-back 32x32
# mip of it, and a GPU evaluator they cannot see. One driver plus one GLSL
# file holding every pass. Not in ncz_ship_bins until the operator decides.
if install_extras
install_data('vendor/ephemeris/ephemeris.glsl',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
ephemeris_exe = executable(
    'ephemeris_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_ephemeris.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=ephemeris_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('ephemeris_gles3'), install_dir: ncz_libdir,
)


# Megacity: a flight through an endless future city. oscura (dark, neon, rain) and
# luminox (bright, high-key) lightings joined by a stateless sunrise and sunset,
# seeded weather regimes, lightning within the photosensitivity caps, and paperback
# cover-art ships. One driver, one GLSL file, one stateless logic header
# (src/megacity_logic.h, unit-tested). Not in ncz_ship_bins until the operator decides.
if install_extras
install_data('vendor/megacity/megacity.glsl',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
megacity_exe = executable(
    'megacity_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_megacity.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=megacity_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('megacity_gles3'), install_dir: ncz_libdir,
)

# Headless smoke tester for the megacity screensaver. The harness driver
# requires a Wayland compositor with layer-shell/xdg_shell; this smoke tester
# runs the same scene logic (megacity_logic.h + megacity_opts.h) against a
# surfaceless EGL PBuffer context and dumps real PNGs. Used to capture
# evidence on hosts that have an AMD GPU but no live Wayland session.
# See src/gles3_megacity_smoke.c for usage.
gles3_megacity_smoke = executable(
    'gles3_megacity_smoke',
    files([
        'src/gles3_megacity_smoke.c',
        'src/ncz_options.c',
    ]),
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + ['-DSTANDALONE'],
    install: false,
)
# megacity-coverage: the GPU-rendered visual gate. Renders every brief-mandated
# preset at small resolution, reads back the framebuffer, asserts per-preset
# dark/bright/neon coverage floors and ceilings. Skipped in cross builds
# (the smoke tester needs a live GLES3 driver on the build host). Runs the
# gles3_megacity_smoke binary with --coverage-audit so a regression in the
# paperback look or the rain-soaked neon palette trips the gate in CI rather
# than slipping through to the contact sheet review.
if not meson.is_cross_build()
    test('megacity-coverage',
         gles3_megacity_smoke,
         args: ['night', 'audit', '/tmp/megacity-coverage-audit', '--coverage-audit'],
         timeout: 600,
         is_parallel: false)
endif


# Leviathan: a chaptered flight to, across and into a vast vessel, through a
# gate, to a quiet room (docs/LEVIATHAN.md). Its own driver so the arc has an
# option table (src/leviathan_opts.h); the shader stays in vendor/xshadertoy/glsl
# and still renders in the headless smoke tester with default options. The
# binary keeps its xshadertoy_ name so the launcher and measure-tiers.py treat
# it as a shader hack (weak-class render scale 0.5, common render options); its
# schema is options/xshadertoy_leviathan.tsv (the launcher's short name). Not
# in ncz_ship_bins until the operator decides.
if install_extras and ncz_ship_bins.contains('xshadertoy_leviathan_gles3')
install_data('vendor/xshadertoy/glsl/leviathan.glsl',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
if install_extras and ncz_ship_bins.contains('xshadertoy_leviathan_gles3')
install_subdir('assets/screensaver-chooser/options/presets/xshadertoy_leviathan',
               install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'presets')
endif
leviathan_exe = executable(
    'xshadertoy_leviathan_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_leviathan.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=leviathan_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('xshadertoy_leviathan_gles3'), install_dir: ncz_libdir,
)
# Round 18 — neonspacewar (working name: GenXRockCade, placeholder
# rename at the top of src/gles3_neonspacewar.c).
#
# Self-playing neon vector rock-shooter: ship in a wrapping playfield,
# drifting rocks that split when shot, all rendered as glowing neon
# vector lines with bloom + chromatic aberration + palette cycling +
# shockwave warps. AI plays the game; nobody touches the keyboard.
#
# Pattern matches the blackhole / lavafield GLES3-native targets:
# one driver .c, three vendor shaders (lines / trail-fade / composite),
# loaded at runtime from the source tree or from
# /usr/share/ncz-screensavers/shaders/neonspacewar/ (the
# install_data rule below ensures the binary works when launched
# from / — the cwd-relative-only path was a real ship-blocker on
# the flagship blackhole hack).
if install_extras
install_data('vendor/neonspacewar/composite.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
install_data('vendor/neonspacewar/lines.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
install_data('vendor/neonspacewar/melee.glsl', rename: 'neonspacewar-melee.glsl',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
neonspacewar_exe = executable(
    'neonspacewar_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_neonspacewar.c',
        'src/nsw_melee_sim.c',
        'src/nsw_melee_render.c',
        'src/nsw_melee_camera.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=neonspacewar_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('neonspacewar_gles3'), install_dir: ncz_libdir,
)


# rss-sdl2 family REMOVED 2026-09-26 -- all 13 targets cut after operator
# visual judgement on real hardware: 1 of 5 reviewed was worth keeping, and
# that one (fireworks) is better rebuilt as a shader. See
# docs/superpowers/specs/2026-09-26-afterimage-design.md.


# -----------------------------------------------------------------------
# Phase 1+ — upstream xscreensaver 6.00+ GLES3 rewrites (5 Carsten Steger
# Klein-bottle-cluster hacks + sphereeversion). These already use real
 # GLSL/GLES3 shaders upstream — they call glCreateShader /
 # glCompileShader / glLinkProgram directly via the shared
 # hacks/glx/glsl-utils.{c,h} utility. Vendor the hack source + the
 # shared utility + any per-hack model data, and build against system
 # libGLESv2 / libEGL with -DHAVE_GLSL so the GLSL path compiles in.
 #
 # See UPSTREAM-GLES3-HACKS-2026-08-20.md for the per-hack verification
 # and which hacks needed foundation extensions.
 # -----------------------------------------------------------------------

# Shared utility: glsl-utils.{c,h} + the GLSL helper functions used by
# every upstream GLES3 hack (glsl_CompileAndLinkShaders,
# glsl_Perspective, glsl_LookAt, glsl_GetGLSLVersionString, ...).
upstream_gles3_common_sources = [
    'src/glsl-utils.c',
]

# Helper macro: every upstream GLES3 hack needs -DHAVE_GLSL (so the
# shader paths compile) plus -DSTANDALONE (so #include "xlockmore.h"
# fires inside the file's #ifdef STANDALONE block). The HACK_TABLE
# override routes gles3_harness.c at the right
# xscreensaver_function_table symbol.
upstream_gles3_c_args = gles3_native_c_args + [
    '-DHAVE_JWZGLES',   # ES-only build: no desktop profile queries
    '-DHAVE_GLSL',
    '-DSTANDALONE',
]

# The 6 per-hack executable() targets are added individually by the
# per-hack commits that vendor each hack's source. See
# UPSTREAM-GLES3-HACKS-2026-08-20.md.

# Hypertorus — thickened torus surface with Phong shading
executable(
    'hypertorus_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/hypertorus.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=hypertorus_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('hypertorus_gles3'), install_dir: ncz_libdir,
)

# Klein — Klein bottle immersion
executable(
    'klein_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/klein.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=klein_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('klein_gles3'), install_dir: ncz_libdir,
)

# Projectiveplane — real projective plane immersion
executable(
    'projectiveplane_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/projectiveplane.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=projectiveplane_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('projectiveplane_gles3'), install_dir: ncz_libdir,
)

# ---------------------------------------------------------------------------
# Chooser catalogue + GSettings schema (dev.ncz.screensaver).
# hacks.tsv is generated from ncz_ship_bins by tools/generate-catalog.py.
# ---------------------------------------------------------------------------
install_data('assets/screensaver-chooser/hacks.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
# Shipped to both directories, like hacks.tsv: the launcher reads share/ncz-screensavers,
# the chooser plugin reads share/ncz-screensaver-chooser. Do not de-duplicate.
install_subdir('assets/screensaver-chooser/options/presets/blackhole',
               install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'presets')
# Candidate hacks install their schema and scenes only once they ship.
foreach h : ['neonspacewar', 'ephemeris', 'magmasimplex']
    if ncz_ship_bins.contains(h + '_gles3')
        install_subdir('assets/screensaver-chooser/options/presets/' + h,
                       install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'presets')
        install_data(['assets/screensaver-chooser/options/' + h + '.tsv', 'assets/screensaver-chooser/options/' + h + '.json'],
                     install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'options')
    endif
endforeach
install_data('assets/screensaver-chooser/render-hints.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/render-hints.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/presets.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/presets.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data(['assets/screensaver-chooser/options/blackhole.tsv', 'assets/screensaver-chooser/options/_render.tsv'],
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'options')
install_data('assets/screensaver-chooser/sparse.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/ground.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/sparse.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
# GPU tiers (see docs/LAUNCHER-DESIGN.md): the launcher and both settings UIs read them.
install_data('assets/screensaver-chooser/broken.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/broken.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/tiers.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/tiers.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/render-defaults.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
# Project licence, NOTICE and third-party licence notices ship with the docs.
install_data('LICENSE', 'NOTICE', 'NOTICES.md',
             install_dir: ncz_docdir, install_tag: 'docs')
install_data('docs/PROVENANCE.tsv', 'docs/SHADER-LEDGER.md',
             install_dir: ncz_docdir, install_tag: 'docs')
# Chooser grouping (style categories, validated by tools/generate-catalog.py) and the simple
# option views (Black Hole sliders, per-hack simple sets); see docs/CHOOSER-TAXONOMY.md.
install_data(['assets/screensaver-chooser/category-list.tsv', 'assets/screensaver-chooser/categories.tsv'],
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data(['assets/screensaver-chooser/options/blackhole-simple.tsv', 'assets/screensaver-chooser/options/simple.tsv'],
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'options')
install_data('config/dev.ncz.screensaver.gschema.xml',
             install_dir: get_option('prefix') / 'share' / 'glib-2.0' / 'schemas')

# Launcher, idle timer and settings glue live in launcher/ (own meson.build,
# maintained separately); pull them into the same install when present.
fs = import('fs')
if fs.is_dir('launcher') and fs.exists('launcher/meson.build')
    subdir('launcher')
endif

if get_option('singularity-plugin').enabled()
    subdir('plugin')
endif

if get_option('gtk4-reference-app')
    subdir('contrib/gtk4-reference-app')
endif

# ---------------------------------------------------------------------------
# Option parser unit test and blackhole schema sync test.
# ---------------------------------------------------------------------------
test_ncz_options = executable(
    'test_ncz_options',
    files(['tests/test_ncz_options.c', 'src/ncz_options.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('ncz_options', test_ncz_options)
# Regression guard for the cadence helper in ncz_feed_render_main(): the
# per-frame budget divides by the effective frame rate, so fps_eff must
# always be >= 1. The test pins the clamp and the per-level selector.
test_feed_render_fps = executable(
    'test_feed_render_fps',
    files(['tests/test_feed_render_fps.c', 'src/ncz_feed_render_fps.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('feed-render-fps-safety', test_feed_render_fps)
test_harness_cfg = executable(
    'test_harness_cfg',
    files(['tests/test_harness_cfg.c', 'src/ncz_render.c', 'src/ncz_gpu_guard.c', 'src/ncz_options.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('harness_cfg', test_harness_cfg)
# Round-21 (BLACKHOLE-FP32): deterministic CPU-side precision gate for the
# fp32 linear sample accumulator that bh_capture_still picks when the FBO
# accepts GL_RGBA32F. The on-device fp32 selection itself is asserted at
# runtime via glCheckFramebufferStatus in gles3_blackhole.c.
test_blackhole_fp32_accum = executable(
    'test_blackhole_fp32_accum',
    files(['tests/test_blackhole_fp32_accum.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('blackhole-fp32-accum-precision', test_blackhole_fp32_accum)
# blackhole randomize: seeded, in-envelope, class-gated rolls (10 000 each).
test_blackhole_randomize = executable(
    'test_blackhole_randomize',
    files(['tests/test_blackhole_randomize.c', 'src/blackhole_randomize.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('blackhole-randomize-roll', test_blackhole_randomize)
# blackhole cadence predicate / transition: the adaptive-quality controller
# must classify a frame's inter-draw dt against the ACTIVE feed budget
# (warm-15, capped-30, capped-60), not just the panel refresh. This test
# exercises the production predicate ncz_feed_adapt_classify() (in
# ncz_feed_render_fps.c) and the budget accessor / change flag in
# ncz_time_offset.c, plus the warm-transition rebase the BH saver applies in
# bh_pace(). The test stub for __real_clock_gettime is local to the test
# source (the host-side --wrap is in scope for the on-host harness, not here).
test_blackhole_cadence = executable(
    'test_blackhole_cadence',
    files(['tests/test_blackhole_cadence.c',
           'src/ncz_feed_render_fps.c',
           'src/ncz_time_offset.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('blackhole-cadence', test_blackhole_cadence)
# magmasimplex goo: several distinct wax masses at 20/60/120/300 s and never
# fewer than two, over 240 seeded runs (the operator's blob gate).
test_magmasimplex_goo = executable(
    'test_magmasimplex_goo',
    files(['tests/test_magmasimplex_goo.c', 'src/magmasimplex_goo_sim.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('magmasimplex-goo-blob-gate', test_magmasimplex_goo, timeout: 120)
# magmasimplex randomize: 1000 seeded draws are reproducible, in range, use
# readable palettes and keep the blob gate at the drawn physics and speed.
test_magmasimplex_randomize = executable(
    'test_magmasimplex_randomize',
    files(['tests/test_magmasimplex_randomize.c', 'src/magmasimplex_goo_cfg.c', 'src/magmasimplex_goo_sim.c',
           'src/magmasimplex_palettes.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('magmasimplex-randomize', test_magmasimplex_randomize, timeout: 300)
# ncz_audio_bus: deterministic virtual beat, BPM range/drift, onset detector on
# synthetic kicks (within 40 ms, few false positives on noise), AGC, and the
# photosensitive limiter at 180 BPM full dynamics. Capture is a no-op here
# (built without NCZ_AUDIO_CAPTURE).
test_ncz_audio_bus = executable(
    'test_ncz_audio_bus',
    files(['tests/test_ncz_audio_bus.c', 'src/ncz_audio_bus.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
# magmasimplex variety: the light-show pattern parameters and the initial
# wax-body layout are reproducible for one seed and clearly different across
# seeds (the operator's repeat-layout complaint, 2026-10-05).
test_magmasimplex_variety = executable(
    'test_magmasimplex_variety',
    files(['tests/test_magmasimplex_variety.c', 'src/magmasimplex_goo_sim.c', 'src/magmasimplex_pattern.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
# magmasimplex dendrites: the dendrite option, the projector-mask-size mapping
# (0 = the new large default, clamping, the legacy small dish exact) and the
# resolve defaults, so no existing lamp preset changes.
test_magmasimplex_dendrites = executable(
    'test_magmasimplex_dendrites',
    files(['tests/test_magmasimplex_dendrites.c', 'src/magmasimplex_goo_cfg.c', 'src/magmasimplex_palettes.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('ncz-audio-bus', test_ncz_audio_bus, timeout: 180)
# magmasimplex_beat: the per-mode adapter mapping is bounded (property test),
# dynamics=0 gives exactly zero drives, and the adapter is deterministic.
test_magmasimplex_beat = executable(
    'test_magmasimplex_beat',
    files(['tests/test_magmasimplex_beat.c', 'src/magmasimplex_beat.c', 'src/ncz_audio_bus.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('magmasimplex-beat-adapter', test_magmasimplex_beat, timeout: 180)
test('magmasimplex-dendrites', test_magmasimplex_dendrites, timeout: 60)
test('magmasimplex-variety', test_magmasimplex_variety, timeout: 120)
# Not a test: prints the simulation motion statistics for the real-lamp
# calibration table in docs/MAGMASIMPLEX-60S.md.
magmasimplex_calib = executable(
    'magmasimplex_calib',
    files(['tests/magmasimplex_calib.c', 'src/magmasimplex_goo_sim.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
# neonspacewar Melee: every ordered race pair resolves without NaN, each
# race wins a sane share, battles last 30-80 s on average, and 1000 director
# battles run with no back-to-back repeat.
test_nsw_melee = executable(
    'test_nsw_melee',
    files(['tests/test_nsw_melee.c', 'src/nsw_melee_sim.c', 'src/nsw_melee_camera.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('neonspacewar-melee-battles', test_nsw_melee, timeout: 300)
# megacity: the stateless logic (sun, weather, lightning caps, ships) over hours of scene time.
test_megacity = executable(
    'test_megacity',
    files(['tests/test_megacity.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('megacity-logic', test_megacity, timeout: 300)

# xshadertoy_opts: option parsing, randomize ranges, reproducibility,
# director continuity and phase integration; palette contrast constraints.
test_xst_core = executable(
    'test_xst_core',
    files(['tests/test_xst_core.c', 'src/xst_core.c', 'src/ncz_options.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('xst-core', test_xst_core, timeout: 120)
test_xst_palettes = executable(
    'test_xst_palettes',
    files(['tests/test_xst_palettes.c']),
    include_directories: inc,
    dependencies: [dep_m],
    install: false,
)
test('xst-palettes', test_xst_palettes)
if not meson.is_cross_build()
    # options/xshadertoy_<id>.{tsv,json} and presets/*.json match the //@
    # directives and .conf scenes (tools/xshadertoy-gen-options.py writes them).
    test('xshadertoy-options-in-sync',
         find_program('python3'),
         args: [files('tools/xshadertoy-gen-options.py'), '--check',
                '--render', xshadertoy_render.full_path(), '--root', meson.project_source_root()],
         depends: xshadertoy_render, timeout: 120)
endif

# Leviathan randomize gate: many random (seed, option) draws render non-flat
# and keep the gate inside its luminance caps. Needs a GPU: exits 77 (skip)
# without one and on a software renderer.
test_leviathan_randomize = executable(
    'test_leviathan_randomize',
    files(['tests/test_leviathan_randomize.c']),
    dependencies: [dep_egl, dep_glesv2, dep_m],
    install: false,
)
test('leviathan-randomize', test_leviathan_randomize,
     args: [files('vendor/xshadertoy/glsl/leviathan.glsl'), '48', '0'], timeout: 120)
test('leviathan-randomize-rich', test_leviathan_randomize,
            args: [files('vendor/xshadertoy/glsl/leviathan.glsl'), '24', '2'], timeout: 120)

# Colour-surface contract: the harness must request a LINEAR (non-sRGB)
# window surface because the vendored shaders emit display-referred sRGB
# themselves.  See src/ncz_gl.h and tests/test_egl_colorspace.c.
test_egl_colorspace = executable(
    'test_egl_colorspace',
    files(['tests/test_egl_colorspace.c', 'src/ncz_gl.c']),
    include_directories: inc,
    dependencies: [dep_egl],
    install: false,
)
test('egl-colorspace', test_egl_colorspace)

# Blank-saver gate: the nine xshadertoy ports whose shaders leave .a at 0
# (bestill0-0..5-0, darktransit, noxfire, rigrekt) looked blank on a real
# layer-shell surface even though their RGB was correct, and noxfire also
# collapsed to near-black when its tunnel pinched shut.  Render them through
# the production wrapper (xshadertoy_render uses the same src/xst_core.c
# assembly as the screensaver) and assert opaque output (mean alpha 1.0) and
# a non-black frame (mean luma > 0.05) at t = 2/12/25 s.  The tool exits 77
# on a software renderer, so this is skipped where there is no GPU.
blank_savers = [
    'bestill0-0', 'bestill1-0', 'bestill2-0',
    'bestill3-0', 'bestill4-0', 'bestill5-0',
    'darktransit', 'noxfire', 'rigrekt',
]
foreach b : blank_savers
    test('blank-saver-' + b,
         xshadertoy_render,
         args: [
             meson.project_source_root() / 'vendor' / 'xshadertoy' / 'glsl' / (b + '.glsl'),
             '--out', meson.current_build_dir() / ('blank-' + b),
             '--size', '320x180',
             '--times', '2,12,25',
             '--gate',
         ],
         timeout: 300,
         is_parallel: false)
endforeach

if not meson.is_cross_build()
    # Smoke the in-house neongravity scene through the production render wrapper
    # (same src/xst_core.c assembly as the driver). It must compile, link and
    # render non-black with full alpha at early and steady-state times, without
    # a GL error -- the engine-level smoke for the neongravity supersession.
    # The tool exits 77 on a software renderer, so it is skipped where there is
    # no GPU; the cross-build guard keeps it off non-native build machines.
    test('neongravity-inhouse-smoke',
         xshadertoy_render,
         args: [
             meson.project_source_root() / 'vendor' / 'xshadertoy' / 'glsl' / 'neongravity.glsl',
             '--out', meson.current_build_dir() / 'neongravity-inhouse-smoke',
             '--size', '320x180',
             '--times', '2,20,60',
             '--gate',
         ],
         timeout: 300,
         is_parallel: false)
    test('blackhole-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                blackhole_exe, files('assets/screensaver-chooser/options/blackhole.tsv')])
    test('blackhole-preset-precedence',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=kerr --print-config | grep ^spin=)" = "spin=0.998" && test "$(NCZ_BLACKHOLE_SPIN=0.5 $0 --preset=kerr --print-config | grep ^spin=)" = "spin=0.5" && test "$(NCZ_BLACKHOLE_SPIN=0.5 $0 --preset=kerr --spin=0.7 --print-config | grep ^spin=)" = "spin=0.7" && test "$($0 --preset=nope --print-config 2>/dev/null | grep ^spin=)" = "spin=0"',
                blackhole_exe.full_path(), meson.project_source_root()])
    test('magmasimplex-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                magmasimplex_exe, files('assets/screensaver-chooser/options/magmasimplex.tsv')])
    # The JSON schema and the scenes (.json for the launcher, .conf for
    # --preset) are generated from the binary by one tool; they must match.
    test('magmasimplex-json-in-sync',
         find_program('python3'),
         args: [files('tools/magmasimplex-schema.py'), magmasimplex_exe.full_path(), '--check'],
         depends: magmasimplex_exe)
    test('magmasimplex-preset-precedence',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=classic-lamp --print-config | grep ^view=)" = "view=field" && test "$(NCZ_MAGMASIMPLEX_VIEW=show $0 --preset=classic-lamp --print-config | grep ^view=)" = "view=show" && test "$(NCZ_MAGMASIMPLEX_VIEW=show $0 --preset=classic-lamp --view=glass --print-config | grep ^view=)" = "view=glass"',
                magmasimplex_exe.full_path(), meson.project_source_root()])
    # The Flowerchild scenes were renamed on 2026-10-05; the old butterfly-* ids
    # are .conf aliases and must keep resolving (saved settings).
    test('magmasimplex-preset-aliases',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=butterfly-sunburst --print-config | grep ^palette=)" = "palette=show-yellow-red" && test "$($0 --preset=butterfly-sunburst --print-config | grep ^projector-mask=)" = "projector-mask=soft" && test "$($0 --preset=haight --print-config | grep ^palette=)" = "palette=show-yellow-red" && test "$($0 --preset=butterfly-fillmore --print-config | grep ^projector-mask=)" = "projector-mask=dish" && test "$($0 --preset=elevator --print-config | grep ^overlay=)" = "overlay=both"',
                magmasimplex_exe.full_path(), meson.project_source_root()])
    # Dendrites and projector-mask-size: parsing, precedence (preset < env <
    # CLI), a bad enum ignored, and the sentinel 0 kept raw for resolve.
    test('magmasimplex-dendrite-options',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=inkwell --print-config | grep ^dendrites=)" = "dendrites=ink" && test "$($0 --preset=veins --print-config | grep ^dendrites=)" = "dendrites=veins" && test "$($0 --preset=fillmore-small --print-config | grep ^projector-mask-size=)" = "projector-mask-size=0.47" && test "$($0 --preset=roundhouse --print-config | grep ^projector-mask-size=)" = "projector-mask-size=0.8" && test "$(NCZ_MAGMASIMPLEX_DENDRITES=veins $0 --preset=inkwell --print-config | grep ^dendrites=)" = "dendrites=veins" && test "$(NCZ_MAGMASIMPLEX_DENDRITES=veins $0 --preset=inkwell --dendrites=off --print-config | grep ^dendrites=)" = "dendrites=off" && test "$($0 --dendrites=bogus --print-config | grep ^dendrites=)" = "dendrites=off" && test "$($0 --projector-mask-size=1.5 --print-config | grep ^projector-mask-size=)" = "projector-mask-size=0"',
                magmasimplex_exe.full_path(), meson.project_source_root()])
    test('ephemeris-json-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema-json | diff -u "$1" -',
                ephemeris_exe, files('assets/screensaver-chooser/options/ephemeris.json')])
    test('ephemeris-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                ephemeris_exe, files('assets/screensaver-chooser/options/ephemeris.tsv')])
    test('megacity-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                megacity_exe, files('assets/screensaver-chooser/options/megacity.tsv')])
    test('neonspacewar-json-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema-json | diff -u "$1" -',
                neonspacewar_exe, files('assets/screensaver-chooser/options/neonspacewar.json')])
    test('neonspacewar-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                neonspacewar_exe, files('assets/screensaver-chooser/options/neonspacewar.tsv')])
    test('leviathan-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                leviathan_exe, files('assets/screensaver-chooser/options/xshadertoy_leviathan.tsv')])
    test('leviathan-schema-json-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | python3 "$1" xshadertoy_leviathan | diff -u "$2" -',
                leviathan_exe, files('tools/schema-tsv-to-json.py', 'assets/screensaver-chooser/options/xshadertoy_leviathan.json')])
    test('leviathan-preset-precedence',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=night-liner --print-config | grep ^palette=)" = "palette=sodium" && test "$(NCZ_LEVIATHAN_PALETTE=cool $0 --preset=night-liner --print-config | grep ^palette=)" = "palette=cool" && test "$(NCZ_LEVIATHAN_PALETTE=cool $0 --preset=night-liner --palette=white --print-config | grep ^palette=)" = "palette=white"',
                leviathan_exe.full_path(), meson.project_source_root()])
    test('common-render-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema-common | diff -u "$1" -',
                blackhole_exe, files('assets/screensaver-chooser/options/_render.tsv')])
    # cross-platform build-artifact gate. Validates the linkable shape (the
    # hack binaries, calibrator, idled, and libscreensaver.so when the plugin
    # is enabled) and the installable shape (the meson install tree).
    # Run-time is a couple of seconds; no live Wayland session required.
    # The gate takes the machine it should expect and whether the plugin was
    # asked for, so the same test is meaningful on the x86 tree and on the
    # arm64 packaging run.
    _bag_plugin = get_option('singularity-plugin').enabled() ? 'required' : 'skipped'
    test('build-artifact-gate',
         find_program('sh'),
         args: [files('tools/build-artifact-gate.sh'),
                meson.project_build_root(),
                '',
                _bag_plugin])
endif

# -----------------------------------------------------------------------
# Round 21 — fractasia: GPU fragment-shader screensaver implementing
# the Fractint catalogue coverage described in docs/fractasia/.
#
# One binary (`fractasia_gles3`), seven algorithm families
# (escape-time polynomial, Newton-Raphson, quaternion escape-time,
# strange-attractor basin, IFS/plasma/L-system, Lyapunov, Wolfram CA).
# Thirteen `--family` labels map onto the seven families. Driver
# compiles every family at init, picks one per cycle deterministically
# from --seed, and pushes the per-shader uniforms every frame.
#
# No Fractint source code, formulae, or assets are used. All algorithm
# implementations are textbook; see docs/fractasia/LICENSE-ANALYSIS.md.
# The .frag files are original to ncz-screensavers (Apache-2.0)
# and live under vendor/fractasia/glsl/.
# -----------------------------------------------------------------------

fractasia_exe = executable(
    'fractasia_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/fractasia/gles3_fractasia.c',
        'src/fractasia/fractasia_data.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=fractasia_xscreensaver_function_table',
        '-DFRACTASIA_VERSION="@0@"'.format(meson.project_version()),
    ],
    install: ncz_ship_bins.contains('fractasia_gles3'),
    install_dir: ncz_libdir,
)

# Install the .frag files alongside the other shader resources when
# the binary is shipped (the shader loader resolves from
# /usr/share/ncz-screensavers/shaders/fractasia/ on install).
foreach fs_shader : [
    'escape_time', 'multibrot', 'burning_ship', 'tricorn', 'julia',
    'quaternion', 'newton', 'attractor', 'ifs', 'plasma', 'lsystem',
    'lyapunov', 'cellular', 'julia_glow', 'orbit_trap', 'mobius', 'ship_glow',
]
    if ncz_ship_bins.contains('fractasia_gles3')
        install_data(
            'vendor/fractasia/glsl/@0@.frag'.format(fs_shader),
            install_dir: shader_install_dir / 'fractasia',
            install_tag: 'runtime-shaders',
        )
    endif
endforeach

# Unit tests for the deterministic picker + RNG reproducibility are
# built against the data table (no GL needed).
test_fractasia_picker = executable(
    'test_fractasia_picker',
    files([
        'tests/fractasia/test_fractasia_picker.c',
        'src/fractasia/fractasia_data.c',
    ]),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('fractasia-picker-determinism', test_fractasia_picker)

# Palette generator: quality gate, determinism and diversity (no GL needed).
test_fractasia_palette = executable(
    'test_fractasia_palette',
    files([
        'tests/fractasia/test_fractasia_palette.c',
        'src/fractasia/fractasia_data.c',
    ]),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('fractasia-palette-generator', test_fractasia_palette)

# Cellular automaton stepping (no GL needed).
test_fractasia_ca = executable(
    'test_fractasia_ca',
    files([
        'tests/fractasia/test_fractasia_ca.c',
        'src/fractasia/fractasia_data.c',
    ]),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('fractasia-cellular-automaton', test_fractasia_ca)

# Smoke tester for the GPU path: opens surfaceless EGL, compiles each
# shader at a deterministic time, renders one frame, and dumps it as
# a PNG. The test asserts non-black coverage and non-NaN colours.
test_fractasia_smoke = executable(
    'test_fractasia_smoke',
    files([
        'tests/fractasia/test_fractasia_smoke.c',
        'src/fractasia/fractasia_data.c',
    ]),
    include_directories: inc,
    dependencies: common_gles3_deps + [dep_png],
    c_args: gles3_native_c_args + ['-D_GNU_SOURCE'],
    install: false,
)
test('fractasia-gpu-smoke',
     test_fractasia_smoke,
     timeout: 90,
     is_parallel: false,
     env: {
         'NCZ_FRACTASIA_GL': meson.project_source_root() /
                                   'vendor' / 'fractasia' / 'glsl',
         'NCZ_FRACTASIA_OUT': meson.project_build_root() /
                                   'fractasia-smoke',
     })
if not meson.is_cross_build()
    test('fractasia-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                fractasia_exe, files('assets/screensaver-chooser/options/fractasia.tsv')])
endif

# -----------------------------------------------------------------------
# Shader-engine ports of the xscreensaver classics.  One source file per
# hack in src/classics_shader/, same xscreensaver_function_table symbol as
# the GL1 version exported.
# -----------------------------------------------------------------------
# gibson ships as datatowers (neutral catalog id); source and symbols keep the upstream name.
classics_ids = {'gibson': 'datatowers'}
foreach cname : classics_shader_ported
    executable(
        classics_ids.get(cname, cname) + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/classics_shader/cs_' + cname + '.c',
        ] + classics_shader_extras.get(cname, [])) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(cname),
        ],
        install: ncz_ship_bins.contains(classics_ids.get(cname, cname) + '_gles3'), install_dir: ncz_libdir,
    )
endforeach
