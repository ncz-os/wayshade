# Screensaver options schema

Every screensaver is a simulation with settings, scenes (presets) and a seed. The engine
describes them in data files; front ends (the Singularity plugin, the reference GTK4 app,
your own) draw the controls from that description and write values back through the
launcher. No screensaver needs UI code.

## Files a screensaver ships

| File | Purpose |
|---|---|
| `/usr/share/ncz-screensavers/options/<id>.json` | option schema (this document). `<id>` is the binary name (`magmasimplex_gles3`) or the name without `_gles3` |
| `/usr/share/ncz-screensavers/options/presets/<short>/<name>.json` | one scene (preset) per file |
| `/usr/share/ncz-screensavers/options/_render.tsv` | common render options added to every shader hack (do not repeat them) |

A hack that describes its own options needs no file: when there is no `options/<id>.json` or
`.tsv`, the launcher runs `<hack> --dump-schema` (five seconds at most, no GPU needed) and reads
its tab-separated output, ten columns per option: name, type, default, min, max, choices
(comma separated), label, description, group, env variable. The xshadertoy family works this
way (options declared in each `.glsl` header, common options `preset`, `seed`, `randomize`,
`palette`, `speed`, `evolution`, `quality`). `--dump-schema` must exit 0 and print nothing else.

The older tab-separated form (`options/<id>.tsv`, ten columns, produced by
`<hack>_gles3 --dump-schema`) is still read; `.json` wins when both exist. Black Hole
also lists its scenes in `presets.tsv` (hack `--preset=NAME`), which the engine merges with the
JSON presets.

## options/<id>.json

```json
{
  "options": [
    { "id": "seed", "type": "int", "label": "Seed", "default": 0, "range": [0, 2147483647],
      "group": "Look", "description": "Fixes the randomness so a scene can be reproduced; 0 = random each run." },
    { "id": "palette", "type": "enum", "label": "Palette", "default": "stylized",
      "choices": ["stylized", "kipthorne", "faithful", "singularity", "slingshot", "whitehole", "eht"],
      "group": "Look" },
    { "id": "exposure", "type": "float", "label": "Exposure", "default": 1.0, "range": [0.2, 4.0],
      "group": "Look", "level": "advanced", "description": "Overall brightness multiplier." },
    { "id": "gravitational-lensing", "type": "bool", "label": "Gravitational lensing", "default": true,
      "group": "Physics", "level": "advanced" },
    { "id": "preset", "type": "string", "label": "Scene" }
  ]
}
```

| Field | Required | Meaning |
|---|---|---|
| `id` (or `name`) | yes | option name: lower case, digits and `-`; unique in the file |
| `type` | yes | `bool`, `int`, `float`, `enum`, `string` |
| `default` | yes | value as the hack starts without any setting |
| `range` | int, float | `[min, max]`; the launcher rejects values outside it |
| `choices` | enum | allowed values, in display order |
| `label` | yes | short title (a few words; the front end translates it) |
| `description` | no | one or two sentences |
| `group` | no | section title, default `General` |
| `level` | no | `basic` or `advanced`; default `basic` for `preset`, `seed`, `palette`, `style`, `speed`, else `advanced` |
| `env` | no | environment variable the hack reads; default `NCZ_<SHORTNAME>_<ID>` upper case, `-` becomes `_` |

Long enums (a palette with about a hundred entries) may add two optional fields; `choices`
stays the flat, validated list and unknown fields are ignored by older front ends:

- `choice_groups`: `[{"label": "1960s", "choices": ["1960s-yellow-orange", ...]}, ...]`, shown
  as sections or a two-level picker;
- `choice_labels`: `{"lavalite-06": "Clear / Coral red"}`, readable names for ids.

Put the default (for example `auto`, meaning drawn from the seed) first in `choices`.

Conventions a front end relies on:

- `preset` (string) names the scene. Its choices are the presets listed for the hack.
- `seed` (int, `0` = random) is the reproducibility handle. A front end shows a Seed field with
  Randomize and Copy buttons. The hack prints its effective seed in its `[diag]` line.
- The values are stored as strings in the GSettings key `dev.ncz.screensaver hack-options`
  (`a{sa{ss}}`, hack id to option to value) and handed to the hack as environment variables.
  Nothing else is stored.

## options/presets/&lt;short&gt;/&lt;name&gt;.json

```json
{ "title": "Calm", "description": "Slow and gentle.", "accuracy": "artistic",
  "min_class": "weak", "values": { "speed": "0.3", "chaos": "0.1" } }
```

`values` lists option values the scene sets. Precedence when a hack starts, lowest first:
scene values, the user's stored options, per-run overrides (`preview --option k=v`). A
scene therefore changes what the user has not touched; a front end that wants a scene to
replace earlier tweaks calls `reset-options` first. `min_class` (`weak`, `mid`, `strong`) flags
a scene that needs a faster GPU than the hack itself, like `tiers.tsv` does for hacks.
`accuracy` is free text shown next to the scene (Black Hole uses `faithful` and `artistic`).

## The command a front end calls

    ncz-screensaver options <hack> --json

```json
{ "schema_version": 1, "hack": "blackhole_gles3",
  "options": [ { "name": "seed", "type": "int", "default": "0", "min": "0", "max": "2147483647",
                 "choices": [], "label": "Seed", "description": "", "group": "Look",
                 "env": "NCZ_BLACKHOLE_SEED", "level": "basic", "value": "0" } ],
  "presets": [ { "id": "m87-star", "row_id": "blackhole_gles3--m87-star", "title": "Black Hole: M87*",
                 "accuracy": "faithful", "description": "...", "min_class": "weak" } ],
  "preset_option": "preset", "seed_option": "seed" }
```

`value` is the stored value or the default. `preset_option` and `seed_option` are `null` when
the hack has no such option. Write values with `ncz-screensaver set-option <hack> <name> <value>`
(validated against the schema), clear a hack with `reset-options <hack>`, try values without
storing them with `preview <hack> --option name=value`.

## What a front end draws

1. **Scene**: a list of `presets` (title, accuracy, description) plus "No scene".
2. **Basic** options in their groups, one row per option by `type`: switch (`bool`),
   number row with the range (`int`, `float`), selection (`enum`), text (`string`).
3. **Seed** (if `seed_option`): number field, Randomize (writes a new random seed), Copy seed.
4. **Advanced**: a collapsed section with every `level: advanced` option, grouped.
5. **Reset to defaults** (`reset-options`) and **Preview**.

The Singularity plugin implements exactly this; `contrib/gtk4-reference-app` implements it for
GTK4. A screensaver adds nothing to either: ship the JSON files.

## Checklist for a new screensaver

- `options/<id>.json` with `seed`, `preset` and the parameters worth changing; ranges and defaults
  taken from what the hack actually accepts.
- At least three scenes under `options/presets/<short>/`.
- The hack reads each option from its `env` variable (or `--<id>=` flag) and prints the effective
  seed. `<hack> --dump-schema` prints the schema (tab-separated today; JSON is read as well).
- Not enforced yet: a build check that validates the schema files (ids, types, ranges, presets
  naming known options) is still to be written.
