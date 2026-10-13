#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert a hack's option schema (the 10-column TSV from `<binary> --dump-schema`)
to JSON for generic settings UIs.

usage: schema-tsv-to-json.py HACK_ID [SCHEMA.tsv]   (reads stdin without a file)

TSV columns: name, type, default, min, max, choices, label, description, group, env.
JSON: {"id": HACK_ID, "options": [{"id", "label", "type", "default", "range"
(for int/float: [min, max]; for enum/list: the list of choices; else null),
"group", "description", "env"}]}. The TSV stays canonical; this output is derived.
"""

import json
import sys


def num(s, typ):
    if s == "":
        return None
    v = float(s)
    return int(v) if typ == "int" and v.is_integer() else v


def convert(hack, lines):
    opts = []
    for line in lines:
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 10:
            sys.exit(f"schema line has {len(f)} columns, expected 10: {line[:60]}")
        name, typ, default, lo, hi, choices, label, desc, group, env = f
        if typ in ("int", "float"):
            rng = [num(lo, typ), num(hi, typ)]
            dflt = num(default, typ)
        elif typ in ("enum", "list"):
            rng = [c for c in choices.split(",") if c]
            dflt = default
        elif typ == "bool":
            rng = None
            dflt = default == "true"
        else:
            rng = None
            dflt = default
        opts.append(
            {
                "id": name,
                "label": label,
                "type": typ,
                "default": dflt,
                "range": rng,
                "group": group,
                "description": desc,
                "env": env,
            }
        )
    return {"id": hack, "options": opts}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if len(sys.argv) > 2:
        with open(sys.argv[2], encoding="utf-8") as src:
            doc = convert(sys.argv[1], src)
    else:
        doc = convert(sys.argv[1], sys.stdin)
    json.dump(doc, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
