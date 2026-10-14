# Black-hole cinematic camera verification — 2026-09-25

This change replaces the fixed-radius slow orbit with three randomized fly-by
paths. Every path changes camera radius, azimuth, and elevation; the launch
rate now produces an obvious change in composition within the first ten
seconds. Five discrete accretion-disk palettes (solar gold, blue-hot, ember,
ultraviolet, and exotic mint) are selected independently of temperature.

## Real-hardware long captures

Two randomized launches were captured at 2, 10, 20, and 30 seconds on both
real test GPUs. Full-resolution PNGs, logs, and SHA-256 manifests are retained
on the hosts and copied to the development host outside the repository:

| Host | Renderer | Native resolution | Evidence |
|---|---|---:|---|
| build-host | NVIDIA GeForce RTX 2060, NVIDIA 615.71.09, PRIME offload | 3840x2160 | `~/build-tmp/blackhole-cinematic-build-host-nvidia/` |
| build-host | AMD Navi14/radeonsi, Mesa 26.1.6-1 | 3072x1920 | `~/build-tmp/blackhole-cinematic-build-host-amd/` |

The development-host review copies are in
`~/build-tmp/blackhole-cinematic/{build-host,build-host}/`.

The launch diagnostics exercised all three fly-by paths and four of the five
palettes across the four launches: build-host selected fly-by 0/mint and fly-by
1/ultraviolet; build-host selected fly-by 2/solar and fly-by 1/blue-hot. Every
launch compiled and linked the shader, reported `gl_error=0x0`, and remained
live for the full capture.

## Visual result

- In the first build-host launch, the view changes from a close oblique overhead
  disk at 2 seconds, to an edge-on silhouette and tall lensed rear arc at 10
  seconds, through a near-polar circular photon ring at 20 seconds, then
  recedes to show the full disk at 30 seconds.
- In the second build-host launch, the blue-hot disk progresses from a low,
  strongly compressed edge-on view, to a wider oblique view with the rear
  image wrapped beneath the hole, to a close overhead pass, then returns to a
  dramatic edge-on arch. This is visibly changing by the 10-second capture.
- build-host independently shows the same large changes in scale and inclination:
  the mint launch moves from oblique to near face-on, while the ultraviolet
  launch moves from an extremely low disk crossing to a broad lensed arch.
  This also demonstrates that the new palette selection is materially more
  varied than a warmer/cooler version of one hue family.

As supporting evidence, full-frame RGB comparisons counted pixels whose value
changed by more than 8 in any channel. Consecutive capture intervals changed
67.91–93.31% of build-host's 8,294,400 pixels and 53.47–95.04% of build-host's
5,898,240 pixels. The visual geometry changes above, rather than this numeric
threshold alone, are the acceptance evidence for the cinematic result.
