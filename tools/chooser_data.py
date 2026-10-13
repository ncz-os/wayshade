# SPDX-License-Identifier: Apache-2.0
"""Chooser data files: style categories, simple-mode axes and simple option sets.

Shared by tools/generate-catalog.py (build-time validation) and tools/tests.
The file formats are documented in docs/CHOOSER-TAXONOMY.md and in the header
comment of each file under assets/screensaver-chooser/.
"""

import pathlib
import re

ORIGINS = ("ncz", "ported")
CURVES = ("linear", "step")
MAX_SIMPLE = 8
# Classics ports (xscreensaver) and hyprsaver shaders are never in-house work.
PORTED_PREFIXES = ("hyprsaver_",)
ORIGINAL_HEADER = re.compile(r"original\s+work", re.IGNORECASE)


def _rows(path, ncols, what):
    """Non-comment lines of a TSV file as (lineno, columns); exact column count."""
    out, problems = [], []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) != ncols:
            problems.append(f"{what}:{n}: expected {ncols} columns, got {len(cols)}")
            continue
        out.append((n, cols))
    return out, problems


# ---------------------------------------------------------------------------
# Categories
# ---------------------------------------------------------------------------


def load_category_list(path):
    """[(id, label, description)] in file order."""
    rows, problems = _rows(path, 3, path.name)
    if problems:
        raise SystemExit("category-list.tsv: " + "; ".join(problems))
    return [(c[0], c[1], c[2]) for _, c in rows]


def load_categories(path):
    """{hack id: {"category", "origin", "tags": [..]}} without validation."""
    rows, problems = _rows(path, 4, path.name)
    if problems:
        raise SystemExit("categories.tsv: " + "; ".join(problems))
    out = {}
    for _, (hack, cat, origin, tags) in rows:
        out[hack] = {
            "category": cat,
            "origin": origin,
            "tags": [t.strip() for t in tags.split(",") if t.strip()],
        }
    return out


def validate_categories(root, ships, classics=()):
    """Every shipped hack has exactly one primary category; origins are honest.

    root: repository root. ships: ids of the ship set (binary names).
    classics: stems of the xscreensaver ports (always origin "ported").
    Returns the problems list (empty when valid)."""
    base = pathlib.Path(root) / "assets" / "screensaver-chooser"
    list_path, cat_path = base / "category-list.tsv", base / "categories.tsv"
    problems = []
    if not list_path.exists() or not cat_path.exists():
        return ["category-list.tsv and categories.tsv are required"]
    rows, problems = _rows(list_path, 3, list_path.name)
    cat_ids = []
    for n, (cid, label, desc) in rows:
        if not re.fullmatch(r"[a-z][a-z0-9-]*", cid):
            problems.append(f"category-list.tsv:{n}: bad category id {cid!r}")
        if cid in cat_ids:
            problems.append(f"category-list.tsv:{n}: duplicate category {cid}")
        if not (label.strip() and desc.strip()):
            problems.append(
                f"category-list.tsv:{n}: label and description are required"
            )
        cat_ids.append(cid)
    rows, more = _rows(cat_path, 4, cat_path.name)
    problems += more
    seen = {}
    used_categories = set()
    for n, (hack, cat, origin, tags) in rows:
        where = f"categories.tsv:{n}"
        if hack in seen:
            problems.append(
                f"{where}: {hack} is listed twice (first at line {seen[hack]})"
            )
            continue
        seen[hack] = n
        if hack not in ships:
            problems.append(f"{where}: {hack} is not in the ship set")
        if cat not in cat_ids:
            problems.append(f"{where}: unknown category {cat!r}")
        used_categories.add(cat)
        if origin not in ORIGINS:
            problems.append(f"{where}: origin must be one of {', '.join(ORIGINS)}")
        if not tags.strip():
            problems.append(f"{where}: at least one tag is required")
        stem = hack.removesuffix("_gles3")
        if origin == "ncz" and (hack.startswith(PORTED_PREFIXES) or stem in classics):
            problems.append(f"{where}: {hack} is a port and cannot be marked ncz")
        if hack.startswith("xshadertoy_"):
            glsl = (
                pathlib.Path(root)
                / "vendor"
                / "xshadertoy"
                / "glsl"
                / (stem[len("xshadertoy_") :] + ".glsl")
            )
            if glsl.exists():
                head = "\n".join(glsl.read_text(errors="replace").splitlines()[:40])
                original = bool(ORIGINAL_HEADER.search(head))
                if origin == "ncz" and not original:
                    problems.append(
                        f"{where}: {hack} is marked ncz but its shader header does not say original work"
                    )
                if origin == "ported" and original:
                    problems.append(
                        f"{where}: {hack} is original work per its header; mark it ncz"
                    )
    missing = sorted(set(ships) - set(seen))
    if missing:
        problems.append(
            "categories.tsv: shipped hacks without a category: " + ", ".join(missing)
        )
    return problems


# ---------------------------------------------------------------------------
# Simple-mode axes (blackhole-simple.tsv)
# ---------------------------------------------------------------------------


def parse_stops(text):
    """'0=0.5,50=1,100=2' -> [(0.0, '0.5'), (50.0, '1'), (100.0, '2')] sorted by position."""
    stops = []
    for item in text.split(","):
        pos, sep, value = item.partition("=")
        if not sep:
            raise ValueError(f"stop {item!r} is not position=value")
        p = float(pos)
        if not 0 <= p <= 100:
            raise ValueError(f"stop position {pos} outside 0..100")
        stops.append((p, value.strip()))
    stops.sort(key=lambda s: s[0])
    if len({p for p, _ in stops}) != len(stops):
        raise ValueError("duplicate stop position")
    return stops


def axis_value(curve, stops, position):
    """Value of one option row at slider position 0..100 (string, like the schema)."""
    position = min(100.0, max(0.0, float(position)))
    if curve == "step":
        value = stops[0][1]
        for p, v in stops:
            if p <= position:
                value = v
        return value
    lo = stops[0]
    for hi in stops[1:]:
        if position <= hi[0]:
            if hi[0] == lo[0]:
                return hi[1]
            t = (position - lo[0]) / (hi[0] - lo[0])
            v = float(lo[1]) + t * (float(hi[1]) - float(lo[1]))
            return _fmt(v)
        lo = hi
    return stops[-1][1]


def axis_position(curve, stops, value):
    """Inverse of axis_value for the lead option: slider position for a value (clamped)."""
    if curve == "step":
        pos = stops[0][0]
        for p, v in stops:
            if v == value:
                return p
        return pos
    try:
        x = float(value)
    except ValueError:
        return 50.0
    best, best_err = 50.0, None
    lo = stops[0]
    for hi in stops[1:]:
        a, b = float(lo[1]), float(hi[1])
        if a == b:
            cand = lo[0] if x == a else None
        else:
            t = (x - a) / (b - a)
            cand = lo[0] + max(0.0, min(1.0, t)) * (hi[0] - lo[0])
        if cand is not None:
            err = abs(float(axis_value(curve, stops, cand)) - x)
            if best_err is None or err < best_err:
                best, best_err = cand, err
        lo = hi
    return round(best, 1)


def _fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return s if s not in ("", "-0") else "0"


def load_simple_axes(path):
    """[{"id", "label", "description", "low", "high", "options": [{"name", "curve", "stops"}]}].

    Axis rows have eight columns; option rows may stop after the stops column
    (editors strip trailing tabs), so four to eight columns are accepted."""
    rows, problems = [], []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 4 or len(cols) > 8 or (cols[1] == "-" and len(cols) != 8):
            problems.append(
                f"{path.name}:{n}: expected 8 columns (4 for an option row), got {len(cols)}"
            )
            continue
        rows.append((n, cols + [""] * (8 - len(cols))))
    if problems:
        raise SystemExit("; ".join(problems))
    axes, by_id = [], {}
    for n, (axis, option, curve, stops, label, desc, low, high) in rows:
        if option == "-":
            entry = {
                "id": axis,
                "label": label,
                "description": desc,
                "low": low,
                "high": high,
                "options": [],
            }
            axes.append(entry)
            by_id[axis] = entry
            continue
        if axis not in by_id:
            raise SystemExit(
                f"{path.name}:{n}: option row before the axis row of {axis!r}"
            )
        by_id[axis]["options"].append(
            {"name": option, "curve": curve, "stops": parse_stops(stops)}
        )
    return axes


def validate_simple_axes(path, schema, max_axes=6):
    """Check a *-simple.tsv against the option schema rows {name: 10 columns}.

    Rules: at most max_axes axes, each with a label, description, end words and
    at least one option; every option exists in the schema, curves are known,
    stops have 0 and 100, every stop value is inside the option's range or
    choices, the curve is monotone in position for floats, and the value at
    position 50 equals the schema default. Returns the problems list."""
    problems = []
    if not path.exists():
        return [f"{path.name}: missing"]
    try:
        axes = load_simple_axes(path)
    except (SystemExit, ValueError) as exc:
        return [str(exc)]
    if len(axes) > max_axes:
        problems.append(f"{path.name}: {len(axes)} axes, at most {max_axes}")
    seen_axes, seen_options = set(), {}
    for axis in axes:
        aid = axis["id"]
        if not re.fullmatch(r"[a-z][a-z0-9-]*", aid):
            problems.append(f"{path.name}: bad axis id {aid!r}")
        if aid in seen_axes:
            problems.append(f"{path.name}: axis {aid} defined twice")
        seen_axes.add(aid)
        if not all(axis[k].strip() for k in ("label", "description", "low", "high")):
            problems.append(
                f"{path.name}: axis {aid} needs label, description, low and high words"
            )
        if not axis["options"]:
            problems.append(f"{path.name}: axis {aid} maps no option")
        for opt in axis["options"]:
            name, curve, stops = opt["name"], opt["curve"], opt["stops"]
            where = f"{path.name}: {aid}/{name}"
            if name in seen_options:
                problems.append(
                    f"{where}: option already used by axis {seen_options[name]}"
                )
            seen_options[name] = aid
            row = schema.get(name)
            if row is None:
                problems.append(f"{where}: not an option of the schema")
                continue
            typ, default, lo, hi, choices = row[1], row[2], row[3], row[4], row[5]
            if curve not in CURVES:
                problems.append(f"{where}: unknown curve {curve!r}")
                continue
            positions = [p for p, _ in stops]
            if 0.0 not in positions or 100.0 not in positions:
                problems.append(f"{where}: stops must include 0 and 100")
            if typ in ("int", "float"):
                if curve != "linear":
                    problems.append(f"{where}: numeric options use the linear curve")
                try:
                    values = [float(v) for _, v in stops]
                except ValueError:
                    problems.append(f"{where}: stop values must be numbers")
                    continue
                for v in values:
                    if not float(lo) <= v <= float(hi):
                        problems.append(
                            f"{where}: stop value {_fmt(v)} outside {lo}..{hi}"
                        )
                if not (
                    values == sorted(values) or values == sorted(values, reverse=True)
                ):
                    problems.append(f"{where}: stops must be monotone")
                if typ == "int" and any(v != int(v) for v in values):
                    problems.append(f"{where}: int option with a fractional stop")
            elif typ == "enum":
                if curve != "step":
                    problems.append(f"{where}: enum options use the step curve")
                for _, v in stops:
                    if v not in choices.split(","):
                        problems.append(
                            f"{where}: stop value {v!r} not one of {choices}"
                        )
            elif typ == "bool":
                if curve != "step":
                    problems.append(f"{where}: bool options use the step curve")
                for _, v in stops:
                    if v not in ("true", "false"):
                        problems.append(f"{where}: bool stop must be true or false")
            else:
                problems.append(f"{where}: {typ} options cannot be mapped")
                continue
            at50 = axis_value(curve, stops, 50)
            same = (
                (float(at50) == float(default))
                if typ in ("int", "float")
                else at50 == default
            )
            if not same:
                problems.append(
                    f"{where}: value at 50 is {at50}, the schema default is {default}"
                )
    return problems


# ---------------------------------------------------------------------------
# Simple option sets (simple.tsv)
# ---------------------------------------------------------------------------


def load_simple_sets(path):
    """{hack id (as written): [option names]}."""
    if not path.exists():
        return {}
    rows, problems = _rows(path, 2, path.name)
    if problems:
        raise SystemExit("simple.tsv: " + "; ".join(problems))
    return {
        hack: [o.strip() for o in opts.split(",") if o.strip()]
        for _, (hack, opts) in rows
    }


def validate_simple_sets(options_dir, load_schema):
    """Every listed hack has a schema in options_dir and names at most MAX_SIMPLE existing options."""
    path = pathlib.Path(options_dir) / "simple.tsv"
    problems = []
    if not path.exists():
        return problems
    try:
        sets = load_simple_sets(path)
    except SystemExit as exc:
        return [str(exc)]
    for hack, names in sets.items():
        stem = hack.removesuffix("_gles3")
        schema_path = pathlib.Path(options_dir) / (stem + ".tsv")
        if not schema_path.exists():
            problems.append(f"simple.tsv: {hack}: no options/{stem}.tsv")
            continue
        schema = load_schema(schema_path)
        if not names:
            problems.append(f"simple.tsv: {hack}: empty option list")
        if len(names) > MAX_SIMPLE:
            problems.append(
                f"simple.tsv: {hack}: {len(names)} options, at most {MAX_SIMPLE}"
            )
        if len(set(names)) != len(names):
            problems.append(f"simple.tsv: {hack}: duplicate option")
        for name in names:
            if name not in schema:
                problems.append(
                    f"simple.tsv: {hack}: {name!r} is not in options/{stem}.tsv"
                )
    return problems
