# RFC 0001: Sensors configuration layer

Status: draft for discussion, not implemented. Date: 2026-10-05.
Scope: the `sensors` plugin in `singularityos-lab/singularity-plugins` and the
schema keys it reads. No shell or core change is required.

## Problem

`Singularity.SensorMonitor` (libsingularity) is already the single backend:
temperatures, fans, clocks, power rails, VRAM and utilization, each temperature
classified into `SensorKind` (CPU, GPU, SYSTEM, NPU, VPU, MEMORY, STORAGE,
NETWORK, BOARD). The `sensors` plugin renders all of it in a fixed order behind
one chip, with a single grouped/ungrouped toggle, so it is configurable only in
the ways its author guessed: no choice of categories, visualisation, or chip
contents. The six keys it reads are not declared in any shipped schema yet
(desktop PR #267 declares them), so they fall through to defaults. Fans show the
gap: Performance and the plugin each read fans and share no display config.

## Goals

1. One backend, one configuration model, consumed in the plugin settings page
   and in the panel chip/popover.
2. A predictable flow: enumerate categories the backend reports, choose
   per-category visibility and groups, choose a visualisation, choose what the
   top-of-panel summary strip shows.
3. Categories the hardware does not report are never offered.
4. Every control has a real consumer in the same change (no write-only keys).
5. Distro-neutral, plugin-scoped keys and no new backend code.

## Non-goals

Fan control stays in Performance (`FanControlManager`); this is display config.
No new sysfs readers; the only backend-facing change is passing the existing
`start(interval_sec)` argument. No popover layout or contrast changes (plugins
PR #2, #5); those land first. No new top-level bar: the strip is the panel
widget.

## Ground truth (upstream HEAD, 2026-09-25)

- `SensorMonitor` (`libsingularity/src/system/sensor.vala`) exposes readings by
  `SensorKind`, `start(interval_sec)`, `fans()`/`fan_channels()`,
  `power_rails()`, `clocks()`, `available`, `gpu_utilization`,
  `vram_used_bytes`, `cpu_hint`/`gpu_hint`, an `updated` signal and a test-only
  `sysfs_root`. `available` is the contract.
- The plugin adds its indicator with `add_panel_widget(indicator, Align.END)`,
  returns `null` from `get_settings_widget()`, and renders
  `add_group(kind, title)` per kind with readings; absent kinds are skipped.
- `Plugin` (`plugin_contract.vala`) is `activate`/`deactivate`/
  `get_settings_widget`; the shell shows it under Plugins > (name) > Settings.
- `dev.sinty.desktop.gschema.xml` declares no `sensors-*` key today; desktop
  PR #267 declares the six above.

## Proposed data model

All `sensors-*` keys are plugin-scoped: the sensors plugin is their only reader
and writer, so they are that plugin's preferences, not desktop policy. The
recommended home is a plugin-owned `dev.sinty.sensors` schema installed by the
plugins package, so `dev.sinty.desktop` stops growing; until it exists the
plugin reads the keys from whichever schema provides them, behind `has_key()`.
The first six rows below are the keys desktop PR #267 declares; the rest are new.
The `sensors-` prefix is kept across the move so no key is renamed twice.

| Key | Type | Default | Consumer |
|---|---|---|---|
| `sensors-interval-seconds` | int 1-60 | 2 | `monitor.start()` |
| `sensors-show-frequency` | bool | true | chip + Clocks group |
| `sensors-show-utilization` | bool | true | chip + utilization rows |
| `sensors-grouped` | bool | true | headings / clock collapsing |
| `sensors-cpu-zone` | string | "" | `monitor.cpu_hint` |
| `sensors-gpu-zone` | string | "" | `monitor.gpu_hint` |
| `sensors-categories` | string array | [] | ordered category allow-list |
| `sensors-summary` | string array | [] | ordered chip items |
| `sensors-summary-style` | enum compact/labelled | compact | chip rendering |
| `sensors-visualisation` | enum chip/bars/sparkline | chip | popover rows |
| `sensors-alerts` | bool | true | severity colouring |

Category tokens (stable, untranslated): `cpu`, `gpu`, `npu`, `vpu`, `memory`,
`storage`, `network`, `board`, `system`, `fans`, `clocks`, `power`. Chip item
tokens: `temp`, `cpu_khz`, `cpu_usage`, `memory_usage`, `gpu_temp`, `gpu_usage`,
`vram`, `fan`. `temp` is the chip's primary reading: CPU when one is identified,
else SYSTEM, else the hottest reading.

An absent key or empty array means "the shipped default", never "hide
everything". The default for `sensors-categories` is every category the backend
reports, in current order; the default for `sensors-summary` is the current chip
(hottest temperature, CPU clock, CPU %, MEM %). `[]` is the documented sentinel
for "all reported" and is deliberately also the schema default. An absent key
must never produce an empty chip or a hidden control.

## UI flow

1. Category enumeration: read the live snapshot once when the widget is built;
   present only categories with a reading, fan or clock.
2. Visibility and grouping: one switch per present category, in backend order;
   drag to reorder into the two popover columns. Writes `sensors-categories`.
3. Visualisation: a global choice (`chip`, `bars`, `sparkline`), optional
   per-category override under Advanced; `bars` reuses
   `SensorReading.heat_fraction` (0..1 against the sensor's limit).
4. Summary strip: a checkable, orderable chip-item list with a live preview.
   Writes `sensors-summary` and `sensors-summary-style`. The strip is the
   existing panel widget (`add_panel_widget(indicator, Align.END)`), not a new
   bar; this RFC only decides its contents and colour.

The settings widget is a plain `Gtk.Box` of libsingularity rows from
`get_settings_widget()`; no libadwaita.

## Conditional categories

`add_group()` already renders only kinds with readings. Extend that upward:

- NPU and VPU appear only when `classify()` identified them.
- Fans appear only when `fan_channels()` is non-empty (`fans()` omits zero-RPM
  headers), and power rails only when `power_rails()` is non-empty.
- The config UI never builds a control for an absent category, and
  `sensors-categories` is filtered against the present set on read.

## Migration from the current sensors keys (desktop PR #267)

Phase 1 (with #267): the six keys keep their names and meanings in
`dev.sinty.desktop`; the config layer adds its keys there too, every read behind
`has_key()`.

Phase 2 (after #267): move all `sensors-*` keys to `dev.sinty.sensors`; the
plugin resolves the schema once, plugin first, else desktop. Both paths stay
guarded, so a mixed package set never aborts the panel (an unguarded missing key
is a fatal abort, which #267 fixes). No key changes type or meaning, so
migration is a copy and the defaults above preserve today's behaviour.

## Collisions with open work

desktop #267 declares the six keys: extend that block, do not redeclare it.
plugins #2 and #5 edit `sensors.vala`: rebase on their tips, not `main`.
desktop #262 and #264-#266 add wallpaper keys in the same schema file.

## Test plan

- Schema: `glib-compile-schemas --strict`; set/read-back on a memory backend;
  assert `[]` resolves to the dynamic default.
- Model: a pure `SensorsLayout` unit test over a fixed snapshot, asserting which
  categories and chip items render for: no hwmon, CPU only, CPU+GPU, the Sky1
  label set, and fans only.
- Backend: reuse `libsingularity/tests/sensor_test.vala` with `sysfs_root`
  fixtures; no new reader code.
- Plugin build: build `libsensors.so` with Meson/Ninja in a Debian container
  (as on plugins PR #2). Manual: a VM with no thermal zones, a laptop with fans,
  and a Sky1 board.

## First PR (small)

One concern: add `SensorsLayout`, a plain model turning one `SensorMonitor`
snapshot into the ordered present-category and chip-item lists, plus the
`sensors-categories` and `sensors-summary` keys and the wiring of the existing
chip to the model. No new widgets, no layout or contrast change; every key has
its consumer, and an absent config reproduces today's chip exactly.

## Open questions

- Plugin-owned `dev.sinty.sensors`, or keep `sensors-*` in `dev.sinty.desktop`?
- One global visualisation choice, or per-category overrides?
- Does the Performance fan section share the same model?
