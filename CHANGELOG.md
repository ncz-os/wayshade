# Changelog

## 0.8.13 - 2026-10-13

Ships in NCZ-OS 26.10 (engine 0.8.13, Settings plugin 0.3.21).

* `ncz-screensaver-idled` delegated mode: the shell is the only idle-lock owner;
  the daemon keeps suspend locking and display power-off. Two lock clients no
  longer race for the lock's frame source, so the saver renders behind the lock
  at default timings.
* Settings plugin 0.3.21: app-shipped manifest, so the Settings page is filled
  as soon as the plugin is found; own Plugins category and icon; per-screensaver
  options expand in place.
* Launcher: the GPU class cache is keyed on the running kernel release as well;
  `ensure-plugin` says why when it does nothing.
* GTK4 reference front end: libadwaita optional, schema-derived choice lists,
  "Randomize all", build-time smoke test.
* Documentation: user guide, CLI reference, developer guide and a catalogue page
  per saver group with screenshots (`docs/index.md`).

## 0.8.12 - 2026-10-12

Ships in NCZ-OS 26.10 Preview 1.

* Lock-hosted saver (`ncz-screensaver-feed`): a short intro at a capped 8 fps and
  half render resolution, then a freeze after 15 seconds; the lock keeps the last
  frame. The freeze is negotiated (HOLD); against an older lock the feed keeps
  the low frame rate and never sends HOLD.
* Thermal governor: rendering pauses when a sensor approaches about 85 C and
  resumes when it cools; battery-powered systems use a lower frame rate.
* Off-screen renderer: GBM EGL platform fallback for drivers without the
  surfaceless platform (Arm Mali proprietary driver); review hardening of the
  failure paths, readback sizes and GBM teardown.
* `ncz-screensaver-feed --print-policy` prints the compiled-in policy.
* Catalog: 74 screensavers with measured minimum GPU classes
  (`assets/screensaver-chooser/tiers.tsv`).

## 0.8.11 - 2026-10-11

* `ncz-screensaver-feed`: the producer half of the lock-hosted screensaver; every
  screensaver gained an off-screen FBO mode (EGL surfaceless or device platform).
* `ncz-screensaver-idled` delegated mode: with a lock renderer configured the
  daemon asks the session to lock and the lock hosts the saver.

## 0.8.0 - 0.8.10

Magmasimplex (0.8.5) and Fractasia engines, Black Hole randomization (0.8.10),
and a license audit of every vendored shader with the Apache-2.0 license for the
original code (0.8.9).

## 0.7.3 - 2026-09-29

First public source release.
