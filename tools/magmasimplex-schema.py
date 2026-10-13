#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-schema.py - generate magmasimplex's option schema and scenes.

One source for three outputs (docs/OPTIONS-SCHEMA.md):
  assets/screensaver-chooser/options/magmasimplex.json
      the option schema the launcher and the settings UIs render: every option
      from `magmasimplex_gles3 --dump-schema`, plus `preset` (the Scene list),
      a level (basic/advanced) per option, and for the palette the optional
      `choice_groups` (by era and source) and `choice_labels` (liquid/wax).
  assets/screensaver-chooser/options/presets/magmasimplex/<name>.json
      one scene per file (title, description, accuracy, min_class, values),
      what the launcher applies;
  assets/screensaver-chooser/options/presets/magmasimplex/<name>.conf
      the same scene as a key file, for the binary's own --preset=NAME.

usage: magmasimplex-schema.py BINARY [--check]
  --check: exit 1 if any generated file differs from what is on disk.
"""

import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OPT_DIR = ROOT / "assets/screensaver-chooser/options"
PRESET_DIR = OPT_DIR / "presets/magmasimplex"
PAL_SRC = ROOT / "src/magmasimplex_palettes.c"

BASIC = {
    "preset",
    "seed",
    "view",
    "world",
    "palette-era",
    "material",
    "speed",
    "dynamics",
    "randomize",
}

# Preset provenance and measurement, required by tools/generate-catalog.py in the
# [preset] section. magmasimplex is original in-house work, so every scene shares
# the same artistic sources; no performance numbers are claimed here until the
# merged beat/dendrite/variety build is calibrated on the reference hosts.
SOURCES = (
    "Original NCZ work; the molten-wax lava lamp and the 1960s-70s Fillmore-era "
    "liquid light show, with palettes from documented lamp colourways and footage."
)
MEASURED = "declared 2026-10-05; not yet measured for this build"

# name: (title, description, values)
SCENES = {
    "real-lamp-pace": (
        "Real-lamp pace",
        "Wax moves as fast as in a real lamp (calibrated on real-time footage); views change slowly.",
        {"speed": "1", "chapter": "2"},
    ),
    "screensaver-pace": (
        "Screensaver pace",
        "Time compressed three times: more rising, merging and dripping in a short idle.",
        {"speed": "3", "chapter": "0.6"},
    ),
    "classic-lamp": (
        "Classic lamp",
        (
            "The whole screen is the inside of a warm lamp: orange wax in yellow liquid, the pool below, the cap above, "
            "moving at the pace of a real lamp."
        ),
        {
            "view": "field",
            "world": "tank",
            "palette": "era-60s-yellow-orange",
            "material": "milky",
            "cap": "on",
        },
    ),
    "inside-the-wax": (
        "Inside the wax",
        "Suspended in the liquid: huge wax masses drift past and over you, backlit from far below.",
        {"view": "inside", "world": "tank", "depth": "1.3", "drift": "1.2"},
    ),
    "glass-macro": (
        "Eye against the glass",
        "Extreme macro: wax pressed flat on the inside of the glass, the rest of the lamp out of focus behind it.",
        {"view": "glass"},
    ),
    "liquid-light-show": (
        "Liquid light show",
        "Inks and oil between glass dishes, projected on a screen the way the 1960s concert light shows did.",
        {"view": "show"},
    ),
    "red-in-amber": (
        "Red in amber",
        "Red-orange wax in amber liquid, the colours of a real lamp measured from footage.",
        {"palette": "footage-red-in-amber"},
    ),
    "cyan-in-blue": (
        "Cyan in blue",
        "Pale cyan-white wax in deep blue liquid, measured from footage of a real lamp.",
        {"palette": "footage-cyan-in-blue"},
    ),
    "pink-in-teal": (
        "Violet in teal",
        "Violet-pink wax in teal liquid, measured from footage of a real lamp.",
        {"palette": "footage-violet-in-teal"},
    ),
    "startup-warmup": (
        "Warm-up",
        (
            "A lamp just switched on: a cold slab that slowly softens, the first column rising, then the cap forming."
        ),
        {
            "startup": "true",
            "view": "field",
            "world": "tank",
            "palette": "footage-blue-in-clear",
        },
    ),
    "lime-on-green": (
        "Lime in green",
        "Pale, milky lime wax in glowing green liquid.",
        {"palette": "lime-on-green"},
    ),
    "yellow-on-pink": (
        "Yellow in pink",
        "Yellow-orange wax in intensely saturated pink liquid.",
        {"palette": "yellow-on-pink"},
    ),
    "sixties": (
        "The sixties",
        "Only pairings documented for 1960s lamps, and warm generated ones in their spirit.",
        {"palette-era": "1960s"},
    ),
    "glitter-emerald": (
        "Glitter",
        "Gold flecks suspended in emerald liquid, catching the light.",
        {"palette": "era-60s-glitter-emerald", "glitter-amount": "1.5"},
    ),
    "candy-jelly": (
        "Candy jelly",
        "The earlier glossy, clear jelly look of this saver, kept as a scene (opt-in: never drawn).",
        {"material": "jelly", "view": "field"},
    ),
    # The two looks the operator photographed and liked on 2026-10-05 (IMG_8246,
    # IMG_8248): kept reachable, and the regression baseline for the wax shading.
    "amber-glow": (
        "Amber glow",
        "Suspended over the hot centre: pale blue and milky-white wax merging and stretching in a warm amber-to-rose glow.",
        {
            "view": "inside",
            "world": "radial",
            "palette": "code-13",
            "material": "translucent",
        },
    ),
    "teal-suspended": (
        "Violet in teal, suspended",
        "In the liquid: big soft lavender and peach wax over a saturated blue-to-cyan glow rising from below.",
        {
            "view": "inside",
            "world": "tank",
            "palette": "footage-violet-in-teal",
            "material": "translucent",
        },
    ),
    # Flowerchild (view=butterfly): the overhead-projector liquid light show
    # (operator, 2026-10-05). Names approved by the operator on 2026-10-05.
    # "Haight-Ashbury" marks the authentic light-show set in the description
    # (the scene JSON has no group field).
    "conspiracy": (
        "Conspiracy",
        "Haight-Ashbury: overhead-projector liquid light show, red and orange dye fingering into cyan, oil droplets with dark rims.",
        {
            "view": "butterfly",
            "palette": "show-cyan-red",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "grape": (
        "Grape",
        "Royal blue dye with white marbling and cyan inks, ringed oil discs drifting through.",
        {
            "view": "butterfly",
            "palette": "show-royal-white",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "haight": (
        "Haight",
        "Haight-Ashbury: bright yellow dye with big dark-red circles and swarms of small red bubbles, in the projector's round throw.",
        {
            "view": "butterfly",
            "palette": "show-yellow-red",
            "kaleidoscope": "0",
            "projector-mask": "soft",
        },
    ),
    "airplane": (
        "Airplane",
        "Pink, purple and teal dyes blooming into each other, slow breathing.",
        {
            "view": "butterfly",
            "palette": "show-holo-pink-teal",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "prune": (
        "Prune",
        "Dark-toned: slow gold and orange waves over near-black amber.",
        {
            "view": "butterfly",
            "palette": "show-ember-gold",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "butterfly": (
        "Butterfly",
        "Electric cyan, magenta and yellow in a six-fold mirror, pulsing slowly.",
        {
            "view": "butterfly",
            "palette": "show-neon-pulse",
            "kaleidoscope": "6",
            "projector-mask": "off",
        },
        "mid",
    ),
    "fillmore": (
        "Fillmore",
        "Haight-Ashbury: a white-hot dish filling most of the screen, pulsing, red and magenta dye splats with satellite droplets, as in 1969 concert footage. fillmore-small keeps the original small dish.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-splat",
            "kaleidoscope": "0",
            "projector-mask": "dish",
            "dynamics": "0.7",
        },
    ),
    # The live / calm variants (2026-10-05). No band names anywhere.
    "fillmore-live": (
        "Fillmore live",
        "Haight-Ashbury: the Fillmore dish driven by what is playing on the default output: a rhythmic squeeze, colour breathing and droplet bursts on the beat. Play music to see it move.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-splat",
            "kaleidoscope": "0",
            "projector-mask": "dish",
            "dynamics": "0.85",
            "audio": "system",
        },
    ),
    "fillmore-calm": (
        "Fillmore calm",
        "Haight-Ashbury: the Fillmore dish at a gentle dynamics 0.25; a slow breath rather than a dance.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-splat",
            "kaleidoscope": "0",
            "projector-mask": "dish",
            "dynamics": "0.25",
        },
    ),
    "lava-live": (
        "Lava live",
        "A classic warm lamp driven by what is playing: bass lifts the wax, onsets pulse the emitter, mids bloom the light. Play music to see it move.",
        {
            "view": "inside",
            "world": "tank",
            "palette": "era-60s-yellow-orange",
            "material": "milky",
            "cap": "on",
            "dynamics": "0.6",
            "audio": "system",
        },
    ),
    "show-live": (
        "Light show live",
        "The liquid light show driven by what is playing: the projector pulses and the inks move on the beat. Play music to see it move.",
        {
            "view": "show",
            "kaleidoscope": "0",
            "projector-mask": "soft",
            "dynamics": "0.7",
            "audio": "system",
        },
    ),
    "strawberry": (
        "Strawberry",
        "Haight-Ashbury: full-frame deep red and pink dye, glowing bubbles and hot highlights, no mask.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-red",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    # Dorsal branch, 2026-10-05: a branching dendrite layer (option `dendrites`)
    # as the boundary between two dyes and as an ink tree. All three use the
    # layer, so min-gpu-class is mid; the weak tier turns the layer off rather
    # than breaking. The dish/disc masks now default to a large size that fills
    # most of the screen (operator addendum, 2026-10-05).
    "inkwell": (
        "Inkwell",
        "Haight-Ashbury: a black ink tree branching over a white-hot dish, red droplets along the fingers, projector-mask dish, dendrites=ink.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-splat",
            "kaleidoscope": "0",
            "projector-mask": "dish",
            "dendrites": "ink",
        },
        "mid",
    ),
    "veins": (
        "Veins",
        "Haight-Ashbury: a radial vein network in teal, yellow and pink where two dyes interdigitate, macro, dendrites=veins.",
        {
            "view": "butterfly",
            "palette": "show-veins",
            "kaleidoscope": "0",
            "projector-mask": "off",
            "dendrites": "veins",
        },
        "mid",
    ),
    "avalon": (
        "Avalon",
        "Haight-Ashbury: ink fingers branching into a saturated two-dye field, dendrites=ink.",
        {
            "view": "butterfly",
            "palette": "show-cyan-red",
            "kaleidoscope": "0",
            "projector-mask": "off",
            "dendrites": "ink",
        },
        "mid",
    ),
    # The 1969 Fillmore dish at its original small size (the operator's 0.47
    # reading): fillmore's dish default is now the large size, so this keeps
    # the old look reachable.
    "fillmore-small": (
        "Fillmore small",
        "Haight-Ashbury: the 1969 Fillmore dish at its original small size, red and magenta dye splats with satellite droplets.",
        {
            "view": "butterfly",
            "palette": "show-fillmore-splat",
            "kaleidoscope": "0",
            "projector-mask": "dish",
            "projector-mask-size": "0.47",
        },
    ),
    "elevator": (
        "Elevator",
        "Tall drifting colour streaks and neon-sign tubes projected over a slow green, yellow and blue dye field, as other projectors did in 1969.",
        {
            "view": "butterfly",
            "palette": "show-green-blue-yellow",
            "kaleidoscope": "0",
            "projector-mask": "off",
            "overlay": "both",
        },
        "mid",
    ),
    "silver-apple": (
        "Silver Apple",
        "Silver and teal inks in an electric-blue field: a cooler, electronic light show.",
        {
            "view": "butterfly",
            "palette": "show-silver-apple",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    # "Swinging London" set (moods and names approved by the operator, 2026-10-05;
    # names only, nothing implies an endorsement). A 4th element is the
    # min-gpu-class; scenes without one are "weak".
    "technicolor": (
        "Technicolor",
        "Swinging London: wide fields of green, magenta, orange, cream and deep dye, many hues at once.",
        {
            "view": "butterfly",
            "palette": "show-technicolor",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "ufo": (
        "UFO",
        "Swinging London: white droplets drifting in a dark violet field, spacey and slow.",
        {
            "view": "butterfly",
            "palette": "show-ufo",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "middle-earth": (
        "Middle Earth",
        "Swinging London: deep green and teal dye, forest-dark.",
        {
            "view": "butterfly",
            "palette": "show-middle-earth",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "roundhouse": (
        "Roundhouse",
        "Swinging London: a large bright round dish with a cool interior, ice blue, white and violet dye.",
        {
            "view": "butterfly",
            "palette": "show-roundhouse",
            "kaleidoscope": "0",
            "projector-mask": "disc",
            "projector-mask-size": "0.80",
        },
    ),
    "hapshash": (
        "Hapshash",
        "Swinging London: flat bands of violet, orange and magenta with hard edges, poster-art style, almost no gradients.",
        {
            "view": "butterfly",
            "palette": "show-hapshash",
            "kaleidoscope": "0",
            "projector-mask": "off",
            "poster": "true",
        },
    ),
    "kaleidoscope": (
        "Kaleidoscope",
        "Swinging London: deep blue, yellow and magenta dye in a six-fold mirror.",
        {
            "view": "butterfly",
            "palette": "show-kaleido",
            "kaleidoscope": "6",
            "projector-mask": "off",
        },
        "mid",
    ),
    "piper": (
        "Piper",
        "Swinging London: soft pastel dawn, peach, lavender and primrose, gentle.",
        {
            "view": "butterfly",
            "palette": "show-piper",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "itchycoo": (
        "Itchycoo",
        "Swinging London: park green and gold with cream, a sunny afternoon.",
        {
            "view": "butterfly",
            "palette": "show-itchycoo",
            "kaleidoscope": "0",
            "projector-mask": "off",
        },
    ),
    "carnaby": (
        "Carnaby",
        "Swinging London: hot pink and orange op-art, flat hard-edged bands in a four-fold mirror.",
        {
            "view": "butterfly",
            "palette": "show-carnaby",
            "kaleidoscope": "4",
            "projector-mask": "off",
            "poster": "true",
        },
        "mid",
    ),
}


# Old scene ids (2026-10-05, before the operator named the set): each resolves
# to its new scene through a .conf alias file, so saved settings keep working.
# No .json is generated for an alias, so the chooser lists each scene once.
ALIASES = {
    "butterfly-sixties": "conspiracy",
    "butterfly-royal": "grape",
    "butterfly-sunburst": "haight",
    "butterfly-holographic": "airplane",
    "butterfly-ember": "prune",
    "butterfly-neon-pulse": "butterfly",
    "butterfly-fillmore": "fillmore",
    "butterfly-crimson": "strawberry",
}


def palette_table():
    """(id, label, era, source) for every palette, in table order."""
    rows = re.findall(
        r'\{"([a-z0-9-]+)", "([^"]+)", "([0-9a-z]+)", "([a-z0-9-]+)",',
        PAL_SRC.read_text(),
    )
    return rows


def schema(binary):
    out = subprocess.run(
        [binary, "--dump-schema"], check=True, capture_output=True, text=True
    ).stdout
    opts = []
    pals = palette_table()
    n_ids = len(re.findall(r'^    \{"', PAL_SRC.read_text(), re.MULTILINE))
    if len(pals) != n_ids:
        raise SystemExit(
            f"palette table parse found {len(pals)} of {n_ids} entries in {PAL_SRC.name}"
        )
    for line in out.splitlines():
        f = line.split("\t")
        if len(f) < 10:
            continue
        name, typ, default, lo, hi, choices, label, desc, group, env = f[:10]
        o = {"id": name, "type": typ, "label": label}
        if typ == "int":
            o["default"] = int(float(default))
        elif typ == "float":
            o["default"] = float(default)
        elif typ == "bool":
            o["default"] = default == "true"
        else:
            o["default"] = default
        if typ in ("int", "float") and lo != "":
            o["range"] = (
                [int(float(lo)), int(float(hi))]
                if typ == "int"
                else [float(lo), float(hi)]
            )
        if typ == "enum":
            o["choices"] = choices.split(",")
        o["group"] = group
        o["description"] = desc
        o["level"] = "basic" if name in BASIC else "advanced"
        o["env"] = env or "NCZ_MAGMASIMPLEX_" + name.upper().replace("-", "_")
        if name == "palette":
            groups = []

            def add(groups, label, pred):
                ids = [r[0] for r in pals if pred(r)]
                if ids:
                    groups.append({"label": label, "choices": ids})

            groups.append({"label": "Auto", "choices": ["auto"]})
            groups.append(
                {
                    "label": "Generated",
                    "choices": ["generated-vintage", "generated-neon"],
                }
            )
            add(
                groups,
                "1960s lamps",
                lambda r: r[3] == "era-pairings" and r[2] == "1960s",
            )
            add(
                groups,
                "1970s and 1980s lamps",
                lambda r: r[3] == "era-pairings" and r[2] in ("1970s", "1980s"),
            )
            add(groups, "Color codes 1992-2004", lambda r: r[3] == "codes-1992-2004")
            add(groups, "Reference photos", lambda r: r[3] == "photos")
            add(groups, "Real-lamp footage", lambda r: r[3] == "footage")
            add(groups, "Original colourways", lambda r: r[3] == "magmasimplex-ways")
            o["choice_groups"] = groups
            labels = {
                "auto": "Drawn from the seed",
                "generated-vintage": "Generated (vintage)",
                "generated-neon": "Generated (neon)",
            }
            for pid, plabel, era, src in pals:
                labels[pid] = plabel + (
                    " (" + pid.split("-")[1] + ")"
                    if src in ("codes-1992-2004", "magmasimplex-ways")
                    else ""
                )
            o["choice_labels"] = labels
        opts.append(o)
    return {"options": opts}


def scene(name):
    """(title, description, values, min_class) of a scene; min_class defaults to weak."""
    t = SCENES[name]
    return (t[0], t[1], t[2], t[3] if len(t) > 3 else "weak")


def scene_files():
    files = {}
    for name in SCENES:
        title, desc, values, cls = scene(name)
        files[PRESET_DIR / (name + ".json")] = (
            json.dumps(
                {
                    "title": title,
                    "description": desc,
                    "accuracy": "artistic",
                    "min_class": cls,
                    "values": values,
                },
                indent=2,
            )
            + "\n"
        )
        conf = [
            "[preset]",
            "name=" + title,
            "description=" + desc,
            "accuracy=artistic",
            "min-gpu-class=" + cls,
            "sources=" + SOURCES,
            "measured=" + MEASURED,
            "[magmasimplex]",
        ] + ["{}={}".format(*kv) for kv in values.items()]
        files[PRESET_DIR / (name + ".conf")] = "\n".join(conf) + "\n"
    for old, new in ALIASES.items():
        title, desc, values, cls = scene(new)
        conf = [
            "# alias: the scene '"
            + new
            + "' under its old id '"
            + old
            + "' (saved settings keep working)",
            "[preset]",
            "name=" + title,
            "description=" + desc,
            "accuracy=artistic",
            "min-gpu-class=" + cls,
            "sources=" + SOURCES,
            "measured=" + MEASURED,
            # Explicit marker tools/generate-catalog.py honours: an alias is a
            # .conf only, and the catalogue must not list the scene twice.
            "alias=" + new,
            "[magmasimplex]",
        ] + ["{}={}".format(*kv) for kv in values.items()]
        files[PRESET_DIR / (old + ".conf")] = "\n".join(conf) + "\n"
    return files


def main():
    binary, check = sys.argv[1], "--check" in sys.argv
    sch = schema(binary)
    known = {o["id"] for o in sch["options"]}
    byid = {o["id"]: o for o in sch["options"]}
    for name in SCENES:
        values = scene(name)[2]
        bad = set(values) - known
        for k, v in values.items():
            o = byid.get(k)
            if o is None:
                continue
            if o["type"] == "enum" and v not in o["choices"]:
                bad.add(f"{k}={v} (not a choice)")
            if (
                o["type"] in ("int", "float")
                and not o["range"][0] <= float(v) <= o["range"][1]
            ):
                bad.add(f"{k}={v} (out of range)")
            if o["type"] == "bool" and v not in ("true", "false"):
                bad.add(f"{k}={v} (not a bool)")
        if bad:
            raise SystemExit(
                "scene {} names unknown options: {}".format(
                    name, ", ".join(sorted(bad))
                )
            )
    files = {OPT_DIR / "magmasimplex.json": json.dumps(sch, indent=2) + "\n"}
    files.update(scene_files())
    stale = []
    for path, text in files.items():
        if check:
            if not path.exists() or path.read_text() != text:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    if check and stale:
        print(
            "out of date (run tools/magmasimplex-schema.py BINARY):\n  "
            + "\n  ".join(stale)
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
