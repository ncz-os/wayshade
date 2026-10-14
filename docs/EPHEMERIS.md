# Ephemeris

`ephemeris_gles3` is a screensaver for NCZ-OS. It shows a matte print (charcoal and a little iron oxide on limestone) that slowly reworks itself. Every run is different, and nothing carries over from one run to the next.

The design record is `docs/superpowers/specs/2026-09-25-ephemeris-chronicle.md`. This page describes what the program actually does.

## How it works

* **The surface** is an RGBA8 texture on the GPU, with the long side capped at 1280 pixels. It lasts for one run. Red holds charcoal, green holds iron oxide, and blue holds how recently a spot was worked.
* **Workers** live on the CPU, 32 at a time by default. Each lives for a few seconds up to a minute, then its slot gets a new occupant. `spawn()` clears the slot first and fills it only from the run seed, the slot number and the slot's occupant count. A new worker therefore starts with nothing from the one before it.
* **What a worker can see** is one small mip level of the surface, at most 32x32. It is read back a few times a second through a pixel pack buffer and a fence, so the read never stalls the GPU. That copy is blurred and a little late. Workers continue whatever structure they can make out in it, pushed by a slowly moving curl field. Each worker also remembers a short private trail of where it has been, and that trail is cleared with it.
* **The evaluator** is a GPU pass. It compares each stroke with the real surface at full resolution and declines strokes that do not match what is actually there. It also declines some for no reason a worker could know. A declined stroke leaves nothing behind. The CPU never reads the evaluator's output.
* **Weathering:** each frame, some surface pixels lose one 8-bit step, chosen at random. Charcoal fades over about seven minutes and iron oxide over about twelve, so the print keeps changing.
* **A new run** starts from a procedurally generated surface. It has washes, hatching and old contour lines, so it looks as if work has already happened on it, but none has.

Nothing on screen is text, a numeral or a glyph, and nothing flashes.

## Options

| option | default | meaning |
|---|---|---|
| `composition` | `auto` | `masses`: three or four substantial masses with open space between them. `drawing`: one continuous drawing across the whole plate. `auto`: chosen per run |
| `tone` | `0.5` | Lightness of the limestone ground (0.25 to 0.85) |
| `workers` | `32` | Number of concurrent workers (8 to 48) |
| `pace` | `1` | Multiplier on worker speed and weathering |
| `record` | `true` | Write the run table to standard error on exit |
| `seed` | `0` | For testing only: fixes the randomness so a capture can be repeated |

Options can be set on the command line (`--tone=0.4`), through the environment (`NCZ_EPHEMERIS_TONE=0.4`), or in `~/.config/ncz-screensavers/ephemeris.conf`. The schema is `assets/screensaver-chooser/options/ephemeris.tsv`.

## The run table

When `record` is on, the program writes a plain table to standard error as it exits. Under the launcher, that goes to the session journal. Every line starts with `[ephemeris]`:

* one summary line: run length in seconds, composition, number of masses, slots, workers born, iron-oxide workers and mean lifetime
* one line per mass: centre and radius, in surface units
* a column header, then one row for each of the last 64 workers that finished, and one row for each worker still drawing at exit (marked `unfinished`)

The columns are: slot, occupant number, birth time (s), lifetime (s), birth x and y, end x and y, the density the worker saw at birth, pigment (0 charcoal line, 1 iron oxide, 2 wash) and path length.

The table is numbers only. It contains no seed, no host name, no user name, no path and no address. It is never written to a file and never read back, so the next run cannot be restored from it.

## Guarantees

* No network I/O of any kind.
* Nothing is written to disk.
* No state carries over between runs.
* No hidden channel: everything the program emits is described on this page.
* The comments in the source describe how the piece works. They contain no instructions.

## GPU class

Weak. On the Intel UHD 630 it holds 60 fps both at native resolution and at the weak-class default scale of 0.5. Measurements are in `assets/screensaver-chooser/tiers.tsv`.
