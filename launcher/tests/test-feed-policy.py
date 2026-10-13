#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Unit tests for the lock-hosted saver POLICY in ncz-screensaver-feed: the
# Apple-T2 detection used by the freeze policy, and the three knobs
# NCZ_SAVER_LOCK_FPS / NCZ_SAVER_RENDER_DIV / NCZ_SAVER_FREEZE[_AFTER_S].
#
# The feed script is loaded as a module with NCZ_SCREENSAVER_LAUNCHER pointed at
# the launcher next to it, exactly as the feed itself loads it at runtime. No
# GPU/no compositor: these are pure functions plus a fake sysroot.
#
# Run: python3 launcher/tests/test-feed-policy.py
import importlib.machinery
import importlib.util
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
LAUNCHER = REPO / "launcher" / "ncz-screensaver"
FEED = REPO / "launcher" / "ncz-screensaver-feed"

os.environ["NCZ_SCREENSAVER_LAUNCHER"] = str(LAUNCHER)
loader = importlib.machinery.SourceFileLoader("ncz_feed", str(FEED))
spec = importlib.util.spec_from_loader("ncz_feed", loader)
feed = importlib.util.module_from_spec(spec)
loader.exec_module(feed)

failed = 0


def check(name, cond):
    global failed
    print(("  PASS: " if cond else "  FAIL: ") + name)
    if not cond:
        failed += 1


def with_env(**env):
    saved = {k: os.environ.get(k) for k in env}
    for k, v in env.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = str(v)

    class ctx:
        def __enter__(self):
            return None

        def __exit__(self, *a):
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
    return ctx()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


print("== Apple T2 detection (fake sysroots) ==")
with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    # 1. DMI product name
    write(root / "sys/class/dmi/id/product_name", "MacBookPro16,1\n")
    check("MacBookPro16,1 DMI -> T2", feed.apple_t2(str(root)) is True)
    # 2. a non-Apple host with no T2 markers
    write(root / "sys/class/dmi/id/product_name", "ROG Zephyrus M15 GU502LV\n")
    check("ASUS ROG DMI -> not T2", feed.apple_t2(str(root)) is False)
    # 3. t2smc hwmon
    write(root / "sys/class/dmi/id/product_name", "Generic\n")
    write(root / "sys/class/hwmon/hwmon9/name", "t2smc\n")
    check("t2smc hwmon -> T2", feed.apple_t2(str(root)) is True)
    # 4. PCI 106b:1802
    with tempfile.TemporaryDirectory() as td2:
        root2 = Path(td2)
        write(root2 / "sys/class/dmi/id/product_name", "Generic\n")
        write(root2 / "sys/bus/pci/devices/0000:03:00.0/vendor", "0x106b\n")
        write(root2 / "sys/bus/pci/devices/0000:03:00.0/device", "0x1802\n")
        check("PCI 106b:1802 -> T2", feed.apple_t2(str(root2)) is True)

print("== NCZ_SAVER_LOCK_FPS ==")
with with_env(NCZ_SAVER_LOCK_FPS=None, NCZ_FEED_FPS=None):
    check("default lock fps is 8 (or 6 on battery)",
          feed.choose_lock_fps() in (8, 6))
with with_env(NCZ_SAVER_LOCK_FPS="12", NCZ_FEED_FPS=None):
    check("NCZ_SAVER_LOCK_FPS=12 -> 12", feed.choose_lock_fps() == 12)
with with_env(NCZ_SAVER_LOCK_FPS=None, NCZ_FEED_FPS="20"):
    check("legacy NCZ_FEED_FPS=20 -> 20", feed.choose_lock_fps() == 20)
with with_env(NCZ_SAVER_LOCK_FPS="999", NCZ_FEED_FPS=None):
    check("lock fps is clamped to <=240", feed.choose_lock_fps() == 240)
with with_env(NCZ_SAVER_LOCK_FPS="0", NCZ_FEED_FPS=None):
    check("lock fps is clamped to >=1", feed.choose_lock_fps() == 1)

print("== NCZ_SAVER_RENDER_DIV ==")
with with_env(NCZ_SAVER_RENDER_DIV=None):
    check("default render divisor is 2", feed.choose_render_div() == 2)
with with_env(NCZ_SAVER_RENDER_DIV="4"):
    check("NCZ_SAVER_RENDER_DIV=4 -> 4", feed.choose_render_div() == 4)
with with_env(NCZ_SAVER_RENDER_DIV="99"):
    check("render divisor clamped to 8", feed.choose_render_div() == 8)
with with_env(NCZ_SAVER_RENDER_DIV="0"):
    check("render divisor clamped to 1", feed.choose_render_div() == 1)

print("== NCZ_SAVER_FREEZE / _AFTER_S ==")
check("auto + T2 -> freeze", feed.choose_freeze(mode="auto", is_t2=True) is True)
check("auto + non-T2 -> no freeze", feed.choose_freeze(mode="auto", is_t2=False) is False)
check("always -> freeze on a PC", feed.choose_freeze(mode="always", is_t2=False) is True)
check("never -> no freeze on a T2", feed.choose_freeze(mode="never", is_t2=True) is False)
with with_env(NCZ_SAVER_FREEZE=None):
    check("default freeze mode is always (freezes on a PC too)",
          feed.choose_freeze(is_t2=False) is True)
with with_env(NCZ_SAVER_FREEZE_AFTER_S=None):
    check("default freeze-after is 15 s", feed.freeze_after_s() == 15.0)
with with_env(NCZ_SAVER_FREEZE_AFTER_S="3.5"):
    check("NCZ_SAVER_FREEZE_AFTER_S=3.5 -> 3.5", feed.freeze_after_s() == 3.5)

print("== compiled-in policy defaults (verifier marker) ==")
d = feed.policy_defaults()
check("lock_fps default 8", d["lock_fps"] == 8)
check("render_div default 2", d["render_div"] == 2)
check("freeze default always", d["freeze"] == "always")
check("freeze_after_s default 15.0", d["freeze_after_s"] == 15.0)
import io
buf = io.StringIO()
feed.print_policy(stream=buf)
line = buf.getvalue().strip()
check("print_policy emits the marker line",
      line.startswith("ncz-feed-policy ") and "freeze=always" in line
      and "lock_fps=8" in line and "freeze_after_s=15" in line)

print("")
print("== RESULT: %s ==" % ("FAIL" if failed else "PASS"))
sys.exit(1 if failed else 0)
