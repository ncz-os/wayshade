# Black Hole scenes: design and backlog

One concern: what to add beyond the current single-hole renderer, how it is packaged, what it costs on each GPU, and how accurate each scene is.

## Packaging model

1. **Presets** (done, `BLACKHOLE-PRESETS.md`): named option bundles for the existing renderer. Each is optionally its own catalog row (`presets.tsv`), launching `blackhole_gles3 --preset=<id>`. Cheap to add; no shader work when the look can be reached with existing options plus the small extras (`disk-outer`, `jet-length`, `torus`, `companion`).
2. **Scenes** (built 2026-09-29 for binary, merger, tidal disruption, galaxy zoom and three-body; see "Built scenes" below): heavier render modes selected by a `scene` option on the same binary, reusing the renderer, palettes, options plumbing and render-scale. Each scene ships a light quality mode for iGPUs. Both mechanisms get an accuracy label shown in the UI: `faithful` (geometry and numbers follow published physics or measurements) or `artistic` (motivated, scaled or stylized).

## Measured baseline (Schwarzschild ray-marcher, default options)

| GPU | Result |
|---|---|
| Radeon Pro 5500M (Navi14, radeonsi) | 60 fps at 1536x960, GPU 40 to 70 percent busy, p99 frame 17 ms |
| Intel UHD 630 (iris) | about 12 to 15 fps at 1080p at the lowest step budget; the adaptive controller holds 60 fps at 1536x960 only at render scale 0.5 with 80 steps |
| Mali-G720 (Sky1, mali_kbase), 1080p output | 59 fps steady, p95 17 ms |
| RTX 2060 (NVIDIA driver) | not measured in this work; expect at least Navi14 class |

Costs below are estimates relative to that baseline (x1.0) and are marked as such until built.

## Scene menu

| Scene | Feasibility | Est. cost | Accuracy | Notes |
|---|---|---|---|---|
| Kerr (spinning), proper | medium | x1.4 to x1.9 | faithful | Replace the Schwarzschild Binet integrator with Kerr geodesics (Carter constant, Boyer-Lindquist or Kerr-Schild). Gives the real frame-dragging shadow offset and asymmetry. Today `spin` only moves the disk edge (ISCO) and speeds the disk. |
| Quasar / AGN, proper | medium | x1.1 to x1.3 | artistic | Presets `quasar`, `quasar-edge-on`, `blazar`, `ton-618` exist. Missing: lensed torus, broad-line clouds, jet precession and knots moving at c. |
| Microquasar (Cygnus X-1), proper | medium | x1.1 | artistic | Preset exists; the companion is opt-in (`--companion=1`) and is lensed: the star and an analytic Roche-lobe stream are tested on every step of the same bent ray as the disk. Not done: stream integrated as geodesic particles. |
| Sagittarius A*, extras | medium | x1.1 | faithful | Preset exists. Missing: flares (hourly brightening hot spot on the ISCO orbit, period about 30 minutes at r = 6 GM/c^2), S-stars on Keplerian ellipses (S2 period 16 yr, pericenter 120 AU), scaled time. |
| M87* | easy | done | faithful | Preset exists; longer jet needs only larger `jet-length`. |
| TON 618 | easy | done | artistic | Preset exists. |
| Binary black holes | hard | x1.5 (two-hole deflection) or x0.4 (baked lensing map) | artistic (superposed) | Superposing two Schwarzschild deflection fields is not GR; a baked lookup of a numerical lensing map per orbital phase is cheap but needs an offline tool. |
| Inspiral, merger, ringdown (GW150914 style) | hard | x1.3 | artistic | Chirp waveform from the post-Newtonian phase plus a damped-sinusoid ringdown; ripples as a lensing perturbation on the sky. |
| 3 or more holes | hard | x2+ | artistic | Chaotic N-body integration on the CPU per frame; deflection from all holes; only for discrete GPUs. |
| Tidal disruption event | medium | x1.2 | artistic | Star shredded into a stream (particle system on the CPU) wrapping into a disk over minutes; fits the microquasar stream code. |
| Wormhole (Ellis, Interstellar style) | easy | x0.8 | faithful (closed form) | The Ellis wormhole has closed-form lensing; no integration needed, cheaper than the black hole. Two sky textures (both mouths). |
| Powers-of-ten zoom into a feeding hole | medium | x1.0 | artistic | Galaxy with dust lanes and gas streams as layered procedural noise, camera dives to the nucleus; implemented as a new flyby type `zoom` plus a galaxy pass at far distance. |
| Galactic center | medium | x1.2 | artistic | Stars on elliptical orbits (Sgr A* S-star extras), gas wind. |
| Neutron star, pulsar, magnetar | easy | x0.3 | artistic | Lighthouse beams and a rotating field-line glow; not a black hole; cheap extra. |
| Naked singularity, stronger white hole | easy | x0.9 | artistic (fantasy) | Change the boundary condition of the integrator or invert the disk emission; label clearly as fantasy. |

## Backlog, priority order, effort

1. Sgr A* flares and S-stars (2 days): particles on analytic Kepler orbits, additive glow, no integration.
2. Wormhole (2 days): closed form, lowest risk, visually strong.
3. Pulsar and magnetar (1 day).
4. Proper Kerr geodesics (5 days plus validation against published shadow shapes): the only scene that changes the core loop; keep the Schwarzschild loop as the fast path for iGPUs.
5. Tidal disruption event (3 days).
6. Powers-of-ten galaxy zoom (4 days).
7. Binary holes with baked lensing maps (6 days including the offline tool); inspiral and merger (3 more days).
8. Three or more holes, naked singularity, white-hole variants (later; low value for the cost).

Steps 5, 6 and 7 and the three-hole part of step 8 were built on 2026-09-29 (next section). The others are not started.

## Physical numbers (sources)

Horizon radius r_s = 2GM/c^2 = 2.95 km per solar mass. Sgr A*: 4.3e6 solar masses, 8.28 kpc (GRAVITY 2019; EHT 2022 shadow about 52 microarcseconds). M87*: 6.5e9 solar masses, 16.8 Mpc, shadow about 42 microarcseconds (EHT 2019). TON 618: about 4e10 solar masses (estimates up to 6.6e10). Cygnus X-1: 21.2 solar mass hole, 40.6 solar mass companion, 5.6 day period, inclination 27 degrees, 2.2 kpc (Miller-Jones 2021). ISCO for prograde Kerr orbits: 3 r_s at a = 0 to about 0.6 r_s at a = 0.998 (Bardeen, Press, Teukolsky 1972).

## Built scenes (2026-09-29)

Selected with `--scene` (`NCZ_BLACKHOLE_SCENE`, `scene=` in `blackhole.conf`, or a preset row). All five are labeled **artistic**. The laws are right, but time and length are compressed for viewing, and the lensing by several holes is a superposition of single-hole fields, which is not a solution of general relativity.

| Scene | What happens | Physics used | Repeats |
|---|---|---|---|
| `binary` | Two holes (mass ratio 0.45 to 1, or `--mass-ratio`) on a circular orbit. Each has its own minidisk, and a circumbinary ring surrounds them. Each hole lenses the other and its disk. Faint two-armed gravitational-wave ripples spread outward | Kepler period, about 30 s at a separation of 15 r_s. Minidisks are truncated at 0.4 of the separation and the ring spans 1.6 to 2.2 separations | no |
| `merger` | The binary spirals in and chirps. Over 0.8 s the two shadows slide into one. The remnant's shadow rings down as a decaying, rotating quadrupole. After a delay the circumbinary disk falls in and brightens as it feeds the remnant. There is no flash, because a vacuum merger emits no light | Quadrupole inspiral a ~ (t_c - t)^(1/4) with the chirping phase (Peters 1964). About 5 percent of the mass is radiated (GW150914). The ripple pattern is cos 2(theta - phi(t - R/c)) | every 75 s (`--scene-cycle`) |
| `tidal-disruption` | A star on a parabolic orbit is stretched and torn apart at pericenter. Its bound debris wraps into a new disk with a diffuse edge and a t^(-5/3) flare. The unbound half escapes. A weak jet appears late | Barker's equation for the parabola, fallback ~ t^(-5/3) (Rees 1988, Phinney 1989). The star (a sphere tested against every ray segment) and the debris stream (shaded at the disk-plane crossings) are both lensed | every 110 s |
| `galaxy-zoom` | Holds on a whole spiral galaxy, then makes a logarithmic dive through the bulge to the nucleus and lingers at the hole. The galactic band is lensed around the hole | Exponential disk, logarithmic spiral arms, dust lanes, star-forming knots, and a Gaussian bulge integrated along the ray in closed form. The galaxy is scaled down by many orders of magnitude | every 80 s |
| `three-body` | Three equal holes chase each other along one figure eight, and the figure slowly turns | Figure-eight choreography (Moore 1993, Chenciner and Montgomery 2000). The curve x = S sin tau, y = 0.315 S sin 2 tau is used as a close fit | no |

**Quality modes.** `--scene-quality=auto|light|rich`. auto means light on the weak GPU class and rich elsewhere.
- **light** is a thin-lens approximation. Each hole bends the straight ray once, at its closest approach (nearest hole first), by 2 r_s / b plus a steep term near the critical impact parameter. That term draws the photon ring and the lensed far side of each disk, and it holds 60 fps on the Intel UHD 630 at the weak render scale.
- **rich** integrates every ray through the summed field of all holes. For each hole the acceleration is a = -1.5 r_s |x cross v|^2 x / r^5, with an adaptive step and the adaptive step budget.

The tidal disruption and galaxy scenes use the single-hole Binet integrator in both modes. The shader is specialized at compile time (`BH_SCENE`, `BH_LIGHT`), so the default single-hole scene compiles exactly as before.

**Camera.** With `--flyby=auto` each scene uses the director (below) within its own distance range: binary uses orbit at 26 to 34 r_s, merger uses slow-orbit at 28 to 36, TDE uses slow-orbit at 20 to 27, and three-body uses drift at 24 to 32. The multi-hole scenes also use a narrower field of view. The galaxy zoom owns its camera: it starts high above the disk and sinks toward the plane as it dives. Every flyby type and palette works with every scene.

**Defaults are the original look.** Everything below is opt-in, as a whole with `--style=enhanced` or one option at a time (an option set explicitly always wins). The operator reviews each against the original in an A/B sheet before any becomes a default.
- `--tonemap=luminance` compresses luminance and keeps the hue, so saturated hot regions stay colored with a pale core. `classic` (default) is the original per-channel curve.
- `--disk-structure`: hot spots riding Keplerian orbits and a two-armed spiral density wave.
- `--photon-ring`: a thin, pixel-filtered ring just outside the shadow.
- `--bright-stars`: a sparse bright star layer that makes the lensing legible.
- `--jet-duty`: jets come and go on a slow cycle.
- `--drift`: slow composition drift and occasional dim passages against burn-in.
- Enhanced style also adds jet precession, occlusion of a jet behind the shadow, and a jet sheath, and makes `--flyby=auto` the director.

Director camera (`--flyby=director`, or auto in the scenes and the enhanced style): seeded chapters of establish, approach, reveal (the camera rises out of the disk plane so the far side of the disk arches over the shadow), dwell and withdraw. It never cuts, uses the disk normal as up (no roll), and takes its distances from the disk size.

Two new palettes, `psychedelic` and `synthwave`, are available everywhere the others are (launcher, settings, plugin, GSettings enum).

Changes that do apply to the default: stars no longer grow into large discs where the lens magnifies the sky (see below), the output carries an invisible +-0.5/255 dither against banding, and the adaptive step controller may now climb above the GPU tier's static prior when frames have headroom (on Mali-G720 it previously stayed at level 0.33 for a whole run).

**Test hook.** `NCZ_BLACKHOLE_T0=<seconds>` starts the animation clock later. It exists for contact sheets at fixed scene times and is not a user option.

### The pole "large dots"

The star field has not been a longitude/latitude grid since 2026-09-28: stars are hashed in an equal-angle cube map, 208 cells per face. The remaining cause of large dots was magnification. Each star had a fixed radius of 0.34 cells, so where the lens stretches the sky (near the photon ring, and in pole-on views where much of the frame looks at lensed sky) a star grew with the magnification into a disc many pixels wide. The radius now follows the pixel footprint in the lensed sky: `fwidth` of the escaped ray direction, taken outside all branches, where derivatives are defined. A star stays about 1.3 pixels wide at any magnification and still brightens where it is magnified. The old 720x360 lat/long stretch no longer exists in the code; `docs/blackhole-scenes/stars-*.jpg` compares the two.
