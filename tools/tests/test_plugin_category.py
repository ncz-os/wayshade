# SPDX-License-Identifier: Apache-2.0
"""The category the plugin file declares must be one the shell's Plugins page knows.

The page validates X-Singularity-Category against a table in the shell
(`plugins_page.vala`, `categories ()`); an id that is not in it falls through to a name
guess and lands in "Other". SINGULARITY_SHELL_PLUGINS_PAGE points at that file of the
shell the plugin is installed on (the pinned shell with the downstream patches applied);
without it only the packaging list is checked.
"""

import os
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugin/screensaver/screensaver.plugin"
PACKER = ROOT / "tools/make-plugin-deb.sh"
SHELL_PAGE = os.environ.get("SINGULARITY_SHELL_PLUGINS_PAGE")


def declared():
    for line in PLUGIN.read_text().splitlines():
        if line.startswith("X-Singularity-Category="):
            return line.split("=", 1)[1].strip()
    raise AssertionError("screensaver.plugin has no X-Singularity-Category")


def packer_ids():
    m = re.search(
        r"case \"\$\(meta X-Singularity-Category\)\" in\s+([^)]+)\)", PACKER.read_text()
    )
    assert m, "make-plugin-deb.sh has no category list"
    return set(m.group(1).strip().split("|"))


def shell_ids(path):
    text = pathlib.Path(path).read_text()
    body = text.split("Category[] categories ()", 1)[1].split("};", 1)[0]
    ids = re.findall(r'\{\s*"([a-z]+)",\s*_\(', body)
    assert ids, "no category rows found in " + str(path)
    return ids


def test_plugin_declares_the_lock_category():
    assert declared() == "lock"
    assert declared() in packer_ids()


@pytest.mark.skipif(not SHELL_PAGE, reason="SINGULARITY_SHELL_PLUGINS_PAGE not set")
def test_declared_id_exists_in_the_shell_category_table():
    ids = shell_ids(SHELL_PAGE)
    assert declared() in ids
    assert packer_ids() <= set(ids)
    assert ids.index("files") < ids.index(declared()) < ids.index("tools")


@pytest.mark.skipif(not SHELL_PAGE, reason="SINGULARITY_SHELL_PLUGINS_PAGE not set")
def test_a_shell_without_the_row_would_not_find_the_id(tmp_path):
    unpatched = tmp_path / "plugins_page.vala"
    unpatched.write_text(
        "\n".join(
            line
            for line in pathlib.Path(SHELL_PAGE).read_text().splitlines()
            if '{ "lock",' not in line
        )
    )
    assert declared() not in shell_ids(unpatched)
