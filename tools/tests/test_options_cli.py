# SPDX-License-Identifier: Apache-2.0
"""Tests for the option schema exposed by `ncz-screensaver options` and JSON presets."""

import importlib.machinery
import importlib.util
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]

TOY = {
    "options": [
        {
            "id": "seed",
            "type": "int",
            "label": "Seed",
            "default": 0,
            "range": [0, 999999],
        },
        {
            "id": "speed",
            "type": "float",
            "default": 1,
            "range": [0.1, 4],
            "group": "Look",
        },
        {
            "id": "chaos",
            "type": "float",
            "default": 0.5,
            "range": [0, 1],
            "level": "advanced",
        },
        {"id": "style", "type": "enum", "default": "a", "choices": ["a", "b"]},
        {"id": "preset", "type": "string"},
        {"id": "bad", "type": "nonsense"},
    ]
}


def load_launcher():
    loader = importlib.machinery.SourceFileLoader(
        "ncz_launcher_o", str(ROOT / "launcher/ncz-screensaver")
    )
    spec = importlib.util.spec_from_loader("ncz_launcher_o", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


@pytest.fixture
def mod(tmp_path, monkeypatch):
    opts = tmp_path / "options"
    (opts / "presets" / "toy").mkdir(parents=True)
    (opts / "toy.json").write_text(json.dumps(TOY))
    (opts / "presets" / "toy" / "calm.json").write_text(
        json.dumps(
            {
                "title": "Calm",
                "description": "Slow",
                "values": {"speed": "0.3", "chaos": "0.1"},
            }
        )
    )
    monkeypatch.setenv("NCZ_SCREENSAVER_OPTIONS", str(opts))
    monkeypatch.setenv("NCZ_SCREENSAVER_PRESETS", str(tmp_path / "none.tsv"))
    return load_launcher()


def test_json_schema_is_loaded_and_levels_default(mod):
    rows = {r["name"]: r for r in mod.load_option_schema("toy_gles3")}
    assert "bad" not in rows
    assert rows["speed"]["min"] == "0.1" and rows["seed"]["max"] == "999999"
    assert rows["seed"]["env"] == "NCZ_TOY_SEED"
    assert mod.option_level(rows["seed"]) == "basic"
    assert mod.option_level(rows["style"]) == "basic"
    assert mod.option_level(rows["chaos"]) == "advanced"


def test_options_command_output(mod, capsys):
    mod.load_settings = lambda: (dict(mod.DEFAULTS), True)
    assert (
        mod.cmd_options(
            type("A", (), {"hack": "toy_gles3", "json": True, "all": False})()
        )
        == 0
    )
    out = json.loads(capsys.readouterr().out)
    assert (
        out["schema_version"] == 1
        and out["seed_option"] == "seed"
        and out["preset_option"] == "preset"
    )
    assert [p["id"] for p in out["presets"]] == ["calm"]
    assert out["presets"][0]["title"] == "Calm"


def test_json_preset_values_apply_below_stored_options(mod):
    settings = dict(mod.DEFAULTS)
    settings["hack-options"] = {"toy_gles3": {"preset": "calm", "chaos": "0.9"}}
    env = mod.hack_option_env("toy_gles3", settings)
    assert env["NCZ_TOY_SPEED"] == "0.3"  # from the scene
    assert env["NCZ_TOY_CHAOS"] == "0.9"  # the user's own value wins
    per_run = mod.hack_option_env("toy_gles3", settings, {"speed": "2"})
    assert per_run["NCZ_TOY_SPEED"] == "2.0"


def test_unknown_preset_file_is_ignored(mod):
    assert mod.preset_values("toy_gles3", "../etc") == {}
    assert mod.preset_values("toy_gles3", "missing") == {}


def test_schema_falls_back_to_the_hack_dump_schema(mod, tmp_path, monkeypatch):
    bindir = tmp_path / "bin"
    bindir.mkdir()
    hack = bindir / "dumpy_gles3"
    hack.write_text(
        '#!/bin/sh\n[ "$1" = --dump-schema ] || exit 9\n'
        'printf "seed\\tint\\t0\\t0\\t99\\t\\tSeed\\tRepeatable\\tLook\\tNCZ_XSHADERTOY_DUMPY_SEED\\n"\n'
        'printf "palette\\tenum\\tauto\\t\\t\\tauto,a,b\\tPalette\\t\\tLook\\t\\n"\n'
    )
    hack.chmod(0o755)
    monkeypatch.setenv("NCZ_SCREENSAVER_DIRS", str(bindir))
    mod.dumped_schema.cache_clear()
    assert mod.load_option_schema("dumpy_gles3") == []  # an override never runs the binary
    monkeypatch.delenv("NCZ_SCREENSAVER_DIRS")
    monkeypatch.setattr(mod, "SYSTEM_HACK_DIRS", (str(bindir) + "/",))
    monkeypatch.setattr(mod, "find_binary", lambda h: str(hack))
    monkeypatch.setattr(mod, "load_catalog", lambda: [{"id": "dumpy_gles3", "title": "D", "group": "G"}])
    mod.dumped_schema.cache_clear()
    rows = {r["name"]: r for r in mod.load_option_schema("dumpy_gles3")}
    assert (
        rows["seed"]["env"] == "NCZ_XSHADERTOY_DUMPY_SEED"
        and rows["seed"]["max"] == "99"
    )
    assert rows["palette"]["choices"] == ["auto", "a", "b"]
    assert mod.option_level(rows["seed"]) == "basic"


def test_choice_groups_and_labels_pass_through(mod, tmp_path):
    path = pathlib.Path(mod.options_dir()) / "wide.json"
    path.write_text(
        json.dumps(
            {
                "options": [
                    {
                        "id": "palette",
                        "type": "enum",
                        "default": "auto",
                        "choices": ["auto", "a1", "b1"],
                        "choice_groups": [
                            {"label": "A", "choices": ["a1"]},
                            {"label": "B", "choices": ["b1"]},
                        ],
                        "choice_labels": {"a1": "Alpha one"},
                    }
                ]
            }
        )
    )
    row = mod.load_option_schema("wide_gles3")[0]
    assert [g["label"] for g in row["choice_groups"]] == ["A", "B"]
    assert row["choice_labels"] == {"a1": "Alpha one"}
    assert row["choices"] == ["auto", "a1", "b1"]


def test_typed_json_schema_and_conf_presets(mod):
    opts = pathlib.Path(mod.options_dir())
    (opts / "sea.json").write_text(
        json.dumps(
            {
                "id": "sea",
                "options": [
                    {
                        "id": "mode",
                        "type": "enum",
                        "default": "calm",
                        "range": ["calm", "storm"],
                    },
                    {"id": "foam", "type": "bool", "default": True, "range": None},
                    {"id": "depth", "type": "float", "default": 2.5, "range": [0, 10]},
                ],
            }
        )
    )
    (opts / "presets" / "sea").mkdir()
    (opts / "presets" / "sea" / "slow-drift.conf").write_text("depth=1\n")
    rows = {r["name"]: r for r in mod.load_option_schema("sea_gles3")}
    assert rows["mode"]["choices"] == ["calm", "storm"] and rows["mode"]["min"] == ""
    assert rows["foam"]["default"] == "true" and rows["foam"]["max"] == ""
    assert rows["depth"]["min"] == "0" and rows["depth"]["max"] == "10"
    assert [p["id"] for p in mod.load_presets("sea_gles3")] == ["slow-drift"]
