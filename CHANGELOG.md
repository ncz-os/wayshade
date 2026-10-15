# Changelog

Release history of `ncz-screensavers` (engine, launcher and lock-hosted feed).
The Debian changelog in `debian/changelog` carries the same entries in package
form, plus every release before 0.8.10.

## 0.8.16 - 2026-10-14

Tag `v0.8.16`. Package `ncz-screensavers 0.8.16` (amd64, arm64); settings
plugin `ncz-screensavers-plugin 0.3.23`.

* The saver behind the lock runs at 30 fps and full resolution by default
  (display refresh cap; 15 fps on battery; 15 fps at half resolution on a weak
  GPU). Settings: Frame rate and Render resolution on the Screensaver and Lock
  page (keys `lock-fps`, `lock-render-divisor`).
* The CPU no longer touches pixels: GPU flip/scale/swizzle into reused shared
  buffers (3072x1920 on AMD: 39 ms CPU per frame at about 10 fps before, 2 ms
  at 30 fps now).
* The unlock prompt suspends the saver (no rendering) and it continues from the
  same frame when the prompt is dismissed.
* `NCZ_FEED_RENDER_DRIVER` / `NCZ_FEED_RENDER_NODE` pin the saver's GPU;
  `NCZ_SAVER_AUTO_FPS` lowers Auto on hot-running hardware.
* Thermal warm band 15 fps (was 10); the feed logs its frame cadence and the
  renderer its cost per frame.

## 0.8.15 - 2026-10-13

Tag `v0.8.15`. Package `ncz-screensavers 0.8.15` (amd64, arm64); the settings
plugin stays `ncz-screensavers-plugin 0.3.21`.

* `ncz-screensaver-idled` honours the Singularity "Lock on Suspend" setting
  and leaves the lock before sleep to a shell that does it itself, so resume
  shows one lock screen.

## 0.8.14 - 2026-10-13

Tag `v0.8.14`. Package `ncz-screensavers 0.8.14` (amd64, arm64); the settings
plugin shipped alongside stays `ncz-screensavers-plugin 0.3.21`.

* Savers that draw their final pass to the window (Cubestorm, Noof and the
  other classics with post-processing, Magmasimplex) render behind the lock
  instead of showing a black screen.
* Magmasimplex renders on Arm Mali (Sky1).

* Lock-hosted saver: no timer freeze by default. The new default freeze mode
  `prompt` keeps the saver animating for as long as nobody touches the
  machine; it freezes on a frame only when input raises the unlock prompt (the
  lock client freezes it and draws the prompt over that frame). Before this,
  the idle saver stopped on a frame 15 seconds after it started on every
  system, because on NCZ-OS the idle saver is hosted by the lock.
* `NCZ_SAVER_FREEZE=always` keeps the old intro-then-freeze behaviour as an
  opt-in; `auto` does that on Apple T2 Macs only; an unknown value means
  `prompt`. `--print-policy` now reports `freeze=prompt`.
* The thermal governor is unchanged and remains the protection for a saver
  that animates indefinitely.

## 0.8.12 - 2026-10-12 (shipped in NCZ-OS 26.10 Preview 1)

Tag `v0.8.12`. Package `ncz-screensavers 0.8.12` (amd64, arm64); the settings
plugin shipped alongside is `ncz-screensavers-plugin 0.3.20`.

* Lock-hosted saver (`ncz-screensaver-feed`) runs cool by default. While the
  session is locked the saver renders a short intro at a capped 8 fps and half
  render resolution, then freezes after 15 seconds and hands the lock its last
  frame to keep. The freeze applies on every system.
* The freeze is negotiated: the feed sends HOLD only to a lock that announces
  HOLD support. Against an older lock it keeps the low frame rate and never
  sends HOLD, so it degrades quietly instead of faulting.
* Thermal governor: rendering pauses when a thermal sensor approaches about
  85 C and resumes once it cools. Battery-powered systems use a lower frame
  rate.
* Off-screen renderer: a GBM EGL platform fallback lets the lock-hosted saver
  animate on GPUs whose drivers lack the surfaceless platform (Arm Mali
  proprietary driver). Mesa drivers keep the surfaceless path.
* Hardening of the off-screen renderer after review: explicit thermal failure
  paths, overflow-checked readback sizes, allocate-before-free on resize and a
  GBM teardown invariant.
* `ncz-screensaver-feed --print-policy` prints the compiled-in policy
  (`lock_fps=8 render_div=2 freeze=always freeze_after_s=15` plus the battery
  frame rate) so an installed system can be verified.
* Catalog: 74 screensavers (Black Hole, 30 hyprsaver shaders, 30 xshadertoy
  shaders, 11 classics, Fractasia, Magmasimplex). Each has a minimum GPU class
  (weak, mid, strong) in `assets/screensaver-chooser/tiers.tsv`, measured with
  `tools/measure-tiers.py`; the launcher uses it to pick savers a GPU can run.

## 0.8.11 - 2026-10-11

* `ncz-screensaver-feed`: the producer half of the lock-hosted screensaver.
  A lock client built with an out-of-process frame source presents saver frames
  instead of a static prompt; every hack gained an off-screen FBO mode.
* `ncz-screensaver-idled` delegated mode: with a lock renderer configured the
  daemon asks the shell to lock at the saver delay and the lock hosts the saver.

## 0.8.10 and earlier

See `debian/changelog`.

## Next release (not shipped)

Work on branches, not yet on `master`:

* `feat/saver-tier-manifest-20261012` - a generated saver manifest and a
  measured first-boot saver pool per GPU.
* `fix/saver-kickin-20261013` - in delegated mode the idle daemon arms no
  idle lock of its own, so the shell is the single idle-lock owner.
* `fix/plugin-settings-pane-20261012` - the settings plugin is declared
  app-shipped so its settings page is populated however late it is enabled.
* `docs/unshipped-savers-catalog-20261012` - catalog of buildable savers that
  are not in the shipped set.
