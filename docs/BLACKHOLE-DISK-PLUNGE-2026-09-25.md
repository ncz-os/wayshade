# Black-hole disk-plunge verification — 2026-09-25

This change adds camera mode 3, a launch-relative disk plunge.  It begins at
radius 18, dives to a randomized radius 5.4–6.35 while elevation reaches
-0.045 radians, banks through the disk plane, and returns to the wide view.
The synchronized close radius and near-zero elevation put the camera inside
the disk's outer radius rather than merely orbiting a distant tilted ellipse.

All four modes now receive a randomized signed approach factor (prograde or
retrograde, magnitude 0.82–1.22).  The factor changes azimuthal direction and
trajectory shape, while mode 3 also uses the randomized periapsis.  Host-side
camera selection changed from `% 3` to `% 4`; launch diagnostics record both
new values.

## Real NVIDIA verification

The exact production source was built and run on build-host using NVIDIA PRIME
offload.  The logs identify an NVIDIA GeForce RTX 2060 with driver 615.71.09
and OpenGL ES 3.2.  Two independent randomized launches were captured at 2,
10, 20, and 30 seconds at 3840x2160.  They independently selected opposite
approach directions (+1.171 and -1.116), demonstrating trajectory-level
variation.  Both completed all captures with `gl_error=0x0`; inspection found
no black flicker, NaN pattern, or missing geometry.

A preceding unforced two-launch production run selected camera mode 3 on its
second launch (`approach=0.892`, `periapsis=5.696`), proving that normal `% 4`
selection reaches the plunge.  Its 2-second frame establishes the complete
small tilted disk; its 10- and 20-second frames show the disk extending beyond
the frame as the camera passes inside its outer radius.

For exact periapsis framing, a dedicated mode-3 run fixed only mode and rate
in a temporary build.  At 14.3 seconds, radius was approximately 5.5 and
elevation approximately -0.045.  The retained screenshot visibly shows the
near disk plane crossing and leaving the frame while the lensed far side
wraps over the black hole and across the top of the view as a bright overhead
halo.  This is the intended under-the-disk tunnel shot, not a camera wobble.
The dedicated run also reported `gl_error=0x0` and showed no close-pass
flicker or invalid pixels.

Full-resolution PNGs, logs, and checksums are retained outside the repository
on build-host under `~/build-tmp/blackhole-cinematic-disk-plunge-final-build-host-nvidia/`
and on the development host under `~/build-tmp/blackhole-disk-plunge/`.  The
temporary forced build was replaced by, and recompiled from, the unforced
production source after capture.
