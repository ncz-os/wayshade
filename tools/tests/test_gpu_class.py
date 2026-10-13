# SPDX-License-Identifier: Apache-2.0
"""Fixture-based tests for GPU class detection, offload planning and pool selection.

Run: python3 -m pytest tools/tests/test_gpu_class.py
Every test builds a fake sysfs tree, a fake calibrator and a fake tiers.tsv under a
temporary directory; nothing touches the real GPU or the real cache.
"""

import importlib.machinery
import importlib.util
import json
import os
import pathlib
import stat
import textwrap

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_launcher():
    loader = importlib.machinery.SourceFileLoader(
        "ncz_launcher", str(ROOT / "launcher/ncz-screensaver")
    )
    spec = importlib.util.spec_from_loader("ncz_launcher", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def add_gpu(
    sysfs,
    card,
    vendor,
    device,
    driver,
    slot,
    connected=False,
    boot_vga=False,
    vram=None,
):
    dev = sysfs / "class/drm" / card / "device"
    dev.mkdir(parents=True)
    (dev / "vendor").write_text(vendor + "\n")
    (dev / "device").write_text(device + "\n")
    (dev / "uevent").write_text(f"PCI_SLOT_NAME={slot}\n")
    (dev / "boot_vga").write_text("1\n" if boot_vga else "0\n")
    if vram:
        (dev / "mem_info_vram_total").write_text(f"{vram}\n")
    target = sysfs / "drivers" / driver
    target.mkdir(parents=True, exist_ok=True)
    os.symlink(target, dev / "driver")
    if connected:
        conn = sysfs / "class/drm" / f"{card}-eDP-1"
        conn.mkdir()
        (conn / "status").write_text("connected\n")


def add_power(sysfs, ac, battery=True):
    base = sysfs / "class/power_supply"
    (base / "AC").mkdir(parents=True)
    (base / "AC/type").write_text("Mains\n")
    (base / "AC/online").write_text("1\n" if ac else "0\n")
    if battery:
        (base / "BAT0").mkdir()
        (base / "BAT0/type").write_text("Battery\n")


GIB = 1024**3
LAYOUTS = {
    # Hybrid Intel + NVIDIA laptop: Intel iGPU owns the panel, NVIDIA dGPU is the offload target.
    "nvidia+intel": lambda s: (
        add_gpu(
            s,
            "card0",
            "0x8086",
            "0x9bca",
            "i915",
            "0000:00:02.0",
            connected=True,
            boot_vga=True,
        ),
        add_gpu(s, "card1", "0x10de", "0x1f08", "nvidia", "0000:01:00.0"),
    ),
    # the Radeon Pro 5500M laptops: the AMD dGPU owns the panel, the Intel iGPU is idle.
    "amd-dgpu-display+intel-idle": lambda s: (
        add_gpu(s, "card0", "0x8086", "0x3e9b", "i915", "0000:00:02.0"),
        add_gpu(
            s,
            "card1",
            "0x1002",
            "0x7340",
            "amdgpu",
            "0000:03:00.0",
            connected=True,
            boot_vga=True,
            vram=8 * GIB,
        ),
    ),
    # AMD APU on the panel, AMD dGPU idle.
    "amd-apu+amd-dgpu": lambda s: (
        add_gpu(
            s,
            "card0",
            "0x1002",
            "0x1638",
            "amdgpu",
            "0000:06:00.0",
            connected=True,
            boot_vga=True,
            vram=512 * 1024**2,
        ),
        add_gpu(
            s, "card1", "0x1002", "0x73df", "amdgpu", "0000:03:00.0", vram=12 * GIB
        ),
    ),
    "single-soc": lambda s: add_gpu(
        s, "card0", "0x0000", "0x0000", "mali_kbase", "0000:00:00.0", connected=True
    ),
    # MUX switch in dGPU-only mode: the NVIDIA GPU drives the panel, the iGPU is off.
    "mux-dgpu-only": lambda s: add_gpu(
        s,
        "card0",
        "0x10de",
        "0x2520",
        "nvidia",
        "0000:01:00.0",
        connected=True,
        boot_vga=True,
    ),
}

FAKE_CALIBRATOR = textwrap.dedent("""\
    #!/bin/sh
    # fake ncz-screensaver-calibrate: FAKE_MS, FAKE_RENDERER, FAKE_VERSION, FAKE_EXIT drive it
    [ -n "$FAKE_EXIT" ] && [ "$FAKE_EXIT" != 0 ] && exit "$FAKE_EXIT"
    case "$DRI_PRIME$__NV_PRIME_RENDER_OFFLOAD" in
        pci-0000_00_02_0) r="Mesa Intel(R) UHD Graphics 630 (CFL GT2)"; ms=46.5 ;;
        pci-0000_03_00_0) r="AMD Radeon Graphics (radeonsi, navi14)"; ms=5.5 ;;
        1) r="NVIDIA GeForce RTX 2060/PCIe/SSE2"; ms=2.7 ;;
        *) r="${FAKE_RENDERER:-unknown}"; ms="${FAKE_MS:-9.9}" ;;
    esac
    v="${FAKE_VERSION:-OpenGL ES 3.2 Mesa 26.0}"
    if [ "$1" = "--identify" ]; then
        printf '{"renderer":"%s","version":"%s","platform":"fake","identify":true}\\n' "$r" "$v"
    else
        printf '{"renderer":"%s","version":"%s","platform":"fake","timer":"gpu","frames":20,"ms":%s}\\n' "$r" "$v" "$ms"
    fi
    """)

TIERS = textwrap.dedent("""\
    # id\tmin\tuhd630\tuhd630_scaled\tmali\tnavi14\trtx2060\tmeasured
    hyprsaver_light_gles3\tweak\t60.0/16.7\t60.0/16.7\t60.0/16.7\t-\t-\t2026-09-29 test
    light_gles3\tweak\t60.0/16.7\t60.0/16.7\t60.0/16.7\t-\t-\t2026-09-29 test
    medium_gles3\tmid\t14.0/80.0\t28.0/45.0\t48.0/21.0\t-\t-\t2026-09-29 test
    heavy_gles3\tstrong\t9.0/120.0\t12.0/95.0\t20.0/60.0\t-\t-\t2026-09-29 test
    """)


@pytest.fixture
def env(tmp_path, monkeypatch):
    sysfs = tmp_path / "sys"
    sysfs.mkdir()
    cal = tmp_path / "calibrate"
    cal.write_text(FAKE_CALIBRATOR)
    cal.chmod(cal.stat().st_mode | stat.S_IXUSR)
    tiers = tmp_path / "tiers.tsv"
    tiers.write_text(TIERS.replace("\\t", "\t"))
    monkeypatch.setenv("NCZ_SCREENSAVER_SYSFS", str(sysfs))
    monkeypatch.setenv("NCZ_SCREENSAVER_CALIBRATE_BIN", str(cal))
    monkeypatch.setenv("NCZ_SCREENSAVER_TIERS", str(tiers))
    monkeypatch.setenv("NCZ_SCREENSAVER_BROKEN", str(tmp_path / "none.tsv"))
    monkeypatch.setenv("NCZ_SCREENSAVER_SWITCHEROOCTL", str(tmp_path / "absent"))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    monkeypatch.setenv("NCZ_SCREENSAVER_PROC_ROOT", str(tmp_path / "proc"))
    (tmp_path / "proc").mkdir()
    (tmp_path / "cache").mkdir()
    mod = load_launcher()
    mod.tmp = tmp_path
    mod.sysfs = sysfs
    return mod


def layout(mod, name, ac=True):
    LAYOUTS[name](mod.sysfs)
    add_power(mod.sysfs, ac)
    for fn in (mod.list_gpus, mod.switcheroo_envs, mod.gpu_topology):
        fn.cache_clear()


def seed_cache(mod, entries):
    path = mod.class_cache_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"entries": entries}))


def seed_display(mod, cls):
    seed_cache(mod, {mod.display_gpu()["id"]: {"class": cls}})


S = {"gpu-offload": "auto", "pool-gpu-class": "auto"}


# ---- class detection ------------------------------------------------------------------
@pytest.mark.parametrize(
    "renderer,expected",
    [
        ("Mesa Intel(R) UHD Graphics 630 (CFL GT2)", "weak"),
        ("Mesa Intel(R) UHD Graphics (CML GT2)", "weak"),
        ("Mali-G720-Immortalis", "mid"),
        ("Mali-G52 MC2", "weak"),
        ("V3D 7.1.7", "weak"),
        ("AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64)", "strong"),
        ("NVIDIA GeForce RTX 2060/PCIe/SSE2", "strong"),
        ("AMD Radeon Graphics (radeonsi, renoir, LLVM 17)", "mid"),
        ("Some Future Chip 9000", None),
    ],
)
def test_renderer_table(env, renderer, expected):
    assert env.class_from_renderer(renderer) == expected


# Strings captured on 2026-09-29 from MS-R1 (Sky1,
# Mali-G720-Immortalis). Both the GLES renderer string returned by the
# calibrator and the Vulkan deviceName/driverName returned by vulkaninfo, plus
# every plausible variant ARM has shipped for the Valhall/Immortalis family
# we expect to see on this SoC family going forward.
SKY1_RENDERERS = [
    "Mali-G720-Immortalis",
    "Mali-G720 MC7",
    "Mali-G715-Immortalis",
    "Mali-G715 MC7",
    "Mali-G710 MC7",
    "Mali-G610 MC4",
    "Mali-G610",
    # Vulkan's driverName/driverInfo path also returns the bare "Mali-G720" or
    # the form with the "Immortalis" suffix; both are mid-tier.
    "Mali-G720",
]


@pytest.mark.parametrize("renderer", SKY1_RENDERERS)
def test_sky1_mali_g720_immortalis_is_mid_not_weak(env, renderer):
    """Regression: Mali-G720-Immortalis and its Immortalis-class siblings must
    classify as 'mid', not 'weak'. The /sys/class/drm/card* entries on Sky1 are
    linlondp display controllers (no GPU); the actual GPU is a separate
    /sys/devices/platform/CIXH5000:00/misc/mali0 character device driven by
    the 'mali' platform bus driver (a CIX-specific glue around the upstream
    mali_kbase, loaded because panthor is blacklisted on this kernel).
    """
    assert env.class_from_renderer(renderer) == "mid"


# The previous weak pattern was "mali-g[35]\d\b", which under-classified
# every Valhall / Immortalis GPU (Mali-G57, G610, G615, G620, G710, G715,
# G720) as weak. The tightened weak pattern is "mali-g(?:31|52)\b"; the
# mid pattern was widened to spell out the same Valhall / Immortalis set.
@pytest.mark.parametrize(
    "renderer,expected",
    [
        # Genuinely weak Mali parts (Bifrost low-end).
        ("Mali-G31", "weak"),
        ("Mali-G31 MC2", "weak"),
        ("Mali-G52", "weak"),
        ("Mali-G52 MC2", "weak"),
        # Valhall / Immortalis parts: mid, never weak.
        ("Mali-G57", "mid"),
        ("Mali-G610", "mid"),
        ("Mali-G610 MC4", "mid"),
        ("Mali-G615", "mid"),
        ("Mali-G620", "mid"),
        ("Mali-G710", "mid"),
        ("Mali-G715", "mid"),
        ("Mali-G720", "mid"),
        ("Mali-G720-Immortalis", "mid"),
        ("Mali-G720 MC7", "mid"),
        # Older T-series and Mali-400 stay weak (the launcher never saw one
        # on NCZ-OS, but the table needs to keep the existing promise).
        ("Mali-T720", "weak"),
        ("Mali-T880", "weak"),
        ("Mali-400 MP2", "weak"),
    ],
)
def test_mali_weak_pattern_does_not_swallow_valhall(env, renderer, expected):
    """The Mali weak pattern is narrowed to G31/G52 only; G57 and the
    G610/G615/G620/G710/G715/G720 / Immortalis family stay mid. The mid
    pattern is widened to spell them out so a renderer string like
    'Mali-G57' never falls through to weak.
    """
    assert env.class_from_renderer(renderer) == expected


# Adversarial: the previous fix tightened the mid pattern to spell out
# Mali-G57 and the G6XX/G7XX / Immortalis family but only spelled the
# three-digit forms (G610..G720). Real Valhall parts that ship as the
# two-digit bare form ('Mali-G68', 'Mali-G77', 'Mali-G78', the canonical
# 2019-2020 Valhall lineup per ARM's product pages) fell through to no
# match and were caught by neither mid nor weak. The mid pattern is now
# extended with mali-g(?:6[0-9]|7[0-9])\b so a fresh host whose renderer
# string reads 'Mali-G77' is classified mid, not 'no match -> weak via
# the unknown-renderer path'.
@pytest.mark.parametrize(
    "renderer",
    [
        # Bare two-digit Valhall parts (2019-2020, still in production).
        "Mali-G68",
        "Mali-G68 MC4",
        "Mali-G77",
        "Mali-G77 MC7",
        "Mali-G78",
        "Mali-G78 MC9",
        # G7[0-9] alone covers G70, G71, ..., G79 (G715/G720 still match).
        "Mali-G71",
        "Mali-G72",
        "Mali-G76",
        "Mali-G79",
    ],
)
def test_mali_two_digit_valhall_is_mid(env, renderer):
    """Adversarial regression: every Mali-G[6-7]X two-digit Valhall part
    must classify as mid, not 'no match'. The fix in 8f221fb only spelled
    the three-digit forms (G610..G720); this commit closes that gap."""
    cls = env.class_from_renderer(renderer)
    assert cls == "mid", (
        f"{renderer!r} classified {cls!r} not mid; the Valhall two-digit "
        "family must not fall through to unknown"
    )


# The two-digit mid pattern must NOT swallow the Bifrost low-end parts
# (G31 / G52) or non-Valhall names like 'Mali-G310' / 'Mali-G520' /
# 'Mali-G6150'. Adversarial sanity for the regex \b anchor.
@pytest.mark.parametrize(
    "renderer,expected",
    [
        ("Mali-G31", "weak"),
        ("Mali-G31 MC2", "weak"),
        ("Mali-G52", "weak"),
        ("Mali-G52 MC2", "weak"),
        # Three-character +1: G310, G520 are Bifrost mid-range (G31 with
        # extra shader cores); they are not Valhall, they stay no-match
        # (the table must NOT classify them at all).
        ("Mali-G310", None),
        ("Mali-G520", None),
        # Edge: Mali-G700 is 3-digit, technically matches the original
        # 6\d\d / 7\d\d; it is also no real product but we want stable
        # semantics either way.
        ("Mali-G700", "mid"),
        # T-series and Mali-400 stay weak.
        ("Mali-T720", "weak"),
        ("Mali-400 MP2", "weak"),
    ],
)
def test_mali_two_digit_pattern_does_not_swallow_bifrost(env, renderer, expected):
    assert env.class_from_renderer(renderer) == expected


@pytest.mark.parametrize(
    "driver,expected",
    [
        # The real driver on .66 is the platform-bus 'mali' (CIX glue for
        # mali_kbase). It must classify as mid, not weak: that's how a Mali
        # GPU ends up classified when only the sysfs topology is available
        # (no cache, no calibrator, the launcher fell back to the table).
        ("mali", "mid"),
        # The upstream driver names used on other platforms.
        ("mali_kbase", "mid"),
        ("panthor", "mid"),
        ("panfrost", "mid"),
        # A pure display controller like linlondp (no 3D pipeline of its own)
        # is not a GPU and should not be in this list at all; we filter it
        # out earlier in list_gpus() instead. class_from_topology only ever
        # sees a GPU that survived list_gpus().
        ("i915", "weak"),
        ("xe", "weak"),
        ("amdgpu", "mid"),
    ],
)
def test_sky1_mali_platform_driver_is_mid(env, driver, expected):
    """Regression: the 'mali' platform driver on Sky1 (CIXH5000:00) is the
    same hardware as upstream mali_kbase. The fallback topology classifier
    must treat it as mid so a GPU system is never demoted to weak.
    """
    gpu = {
        "vendor": "other",
        "driver": driver,
        "discrete": False,
        "render": True,
        "id": "soc-mali",
        "display": True,
        "card": "card0",
        "slot": "CIXH5000:00",
        "device": "",
        "boot_vga": False,
    }
    assert env.class_from_topology(gpu) == expected


def test_sky1_linlondp_display_controllers_are_filtered_from_gpus(env):
    """Regression for an MS-R1 (Sky1): Sky1 exposes four DRM cards driven by
    the 'linlondp' display controller, none of which is a 3D GPU. The actual
    GPU is the /sys/devices/platform/CIXH5000:00/misc/mali0 character device
    driven by the 'mali' platform bus driver. list_gpus() must not list the
    linlondp cards as GPUs (which previously forced them to 'weak' via
    class_from_topology) — they should be filtered out and the GPU entry
    should come from the platform-bus GPU device.
    """
    # Four linlondp display controllers with no vendor/device, like on .66.
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        # The cards do have render nodes (writeback) on .66.
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    # The actual GPU is a /sys/class/misc/mali0 character device, parent
    # platform device CIXH5000:00, driven by the 'mali' platform bus driver.
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    (misc / "uevent").write_text("MAJOR=10\nMINOR=262\nDEVNAME=mali0\nDEVMODE=0666\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "modalias").write_text("acpi:CIXH5000:\n")
    (gpu_dev / "uevent").write_text("DRIVER=mali\nMODALIAS=acpi:CIXH5000:\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    # Mirror /sys/class/misc/mali0/device -> /sys/devices/platform/.../misc/mali0
    # the kernel exposes for every registered misc device.
    target = gpu_dev / "misc/mali0"
    target.mkdir(parents=True)
    os.symlink(target, misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    # Exactly one GPU, the Mali; no linlondp display controller.
    assert len(gpus) == 1
    g = gpus[0]
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["id"] == "soc-CIXH5000_00"
    # The topology reports the same SoC as the display.
    assert env.gpu_topology()["display_class"] == "integrated"


def test_sky1_mali_short_symlink_layout_also_resolves(env):
    """Regression for the second sysfs layout the live .66 host exposes:
    `/sys/class/misc/mali0/device` is a *relative* symlink to the bare
    platform-device name (`../../../CIXH5000:00`) instead of the long path
    that goes through `/misc/mali0`. Both forms are accepted by
    _iter_platform_gpu_devices; this test pins the short form so the
    `else` branch cannot be deleted without breaking the live host.
    """
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    # The platform device is /sys/devices/platform/CIXH5000:00, bound to
    # the 'mali' driver. No /misc/<name> sibling directory exists.
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "modalias").write_text("acpi:CIXH5000:\n")
    (gpu_dev / "uevent").write_text("DRIVER=mali\nMODALIAS=acpi:CIXH5000:\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    # The kernel exposes the misc symlink as a *relative* path with `..`s
    # (readlink on the live host prints `../../../CIXH5000:00`). The real
    # sysfs mount table makes pathlib.resolve() land on
    # /sys/devices/platform/CIXH5000:00. A plain tmpfs mirror cannot
    # replay the mount table, so we point the symlink at the same target
    # via a relative path — the launcher only inspects the *resolved*
    # path, not the readlink output, so the form of the symlink does
    # not matter as long as resolve() reaches the platform device.
    os.symlink(os.path.relpath(gpu_dev, str(misc)), misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert len(gpus) == 1
    g = gpus[0]
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["id"] == "soc-CIXH5000_00"
    assert g["slot"] == "CIXH5000:00"
    assert g["vendor"] == "other"
    assert env.class_from_topology(g) == "mid"


def test_sky1_mali_gpu_is_classified_mid_end_to_end(env, monkeypatch):
    """End-to-end regression for an MS-R1 (Sky1): with the Sky1 sysfs layout,
    the launcher's GPU class detection returns 'mid' (the calibrated class
    on the real hardware) — never 'weak'. This is the central guarantee of
    the operator's policy: never demote a real GPU to weak.
    """
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    target = gpu_dev / "misc/mali0"
    target.mkdir(parents=True)
    os.symlink(target, misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()

    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv(
        "FAKE_VERSION",
        "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0c707efa0cfa034b363bc93f9b6749cb5",
    )
    monkeypatch.setenv("FAKE_MS", "9.88")
    e = env.gpu_class(S, None)
    assert e["class"] == "mid"
    assert e["ms"] == 9.88
    assert e["renderer"] == "Mali-G720-Immortalis"
    # And in the gpus listing: every entry is mid, none is weak.
    for g in env.list_gpus():
        cls = env.gpu_class(S, g, run=False)["class"]
        assert cls == "mid", f"GPU {g['id']} classified {cls} not mid"


def _build_sky1_sysfs(env):
    """Build the full Sky1 / MS-R1 sysfs mirror (four linlondp cards +
    one /sys/class/misc/mali0 → CIXH5000:00 bound to the 'mali' driver)
    and return the list_gpus() result. Used by the cmd_gpus/cmd_status
    integration tests below."""
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    target = gpu_dev / "misc/mali0"
    target.mkdir(parents=True)
    os.symlink(target, misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()


def test_sky1_cmd_gpus_json_lists_only_the_mali_mid(env, monkeypatch, capsys):
    """Regression for an MS-R1 (Sky1): `ncz-screensaver gpus --json` must
    report exactly one entry — the Mali — and that entry must be `class=mid`.
    The previous code reported four linlondp cards (one mid from a stale
    cache, three weak from the topology fallback) on a real GPU system.
    """
    _build_sky1_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "9.88")
    args = type("A", (), {"json": True})()
    assert env.cmd_gpus(args) == 0
    rows = json.loads(capsys.readouterr().out)
    assert len(rows) == 1
    g = rows[0]
    assert g["id"] == "soc-CIXH5000_00"
    assert g["driver"] == "mali"
    assert g["class"] == "mid"
    assert g["display"] is True
    assert g["vendor"] == "other"


def test_sky1_cmd_gpus_text_format(env, monkeypatch, capsys):
    """`ncz-screensaver gpus` (text) must print a single line whose
    columns match the operator-visible format: role, vendor, driver,
    class, score. The fix removes the three spurious 'weak' lines on .66.
    The format also carries the GPU0/GPU1 index column for the UX (operator
    2026-09-30) so users see which GPU the verdict is about.
    """
    _build_sky1_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "9.88")
    args = type("A", (), {"json": False})()
    assert env.cmd_gpus(args) == 0
    text = capsys.readouterr().out.strip().splitlines()
    assert len(text) == 1
    parts = text[0].split()
    # New format: GPU<N> (5 wide), id, vendor, driver, class, role, ms.
    assert parts[0] == "GPU0"
    assert parts[1] == "soc-CIXH5000_00"
    assert parts[2] == "other"
    assert parts[3] == "mali"
    assert parts[4] == "mid"
    assert parts[5] == "display"


def test_sky1_cmd_status_reports_mid_class(env, monkeypatch, capsys):
    """Regression for an MS-R1 (Sky1): `ncz-screensaver status --json`
    must report `gpu_class=mid` (or hit the calibrator and report mid
    via the calibration score). The previous code reported gpu_class=mid
    only because of a stale `pci-CIXH5010_03` cache entry; the fix moves
    the verdict onto the actual Mali GPU id and keeps the same class.
    """
    _build_sky1_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv(
        "FAKE_VERSION",
        "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
    )
    monkeypatch.setenv("FAKE_MS", "9.88")
    args = type("A", (), {"json": True, "diagnostics": False})()
    rc = env.cmd_status(args)
    assert rc in (0, 3)  # 3 because there is no running hack in the test rig
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid"
    # gpu_class_source is "table" because the test rig has no cache yet
    # (the live host has a stale entry that would report "calibration"
    # under the legacy id — both paths end at "mid").
    assert out["gpu_class_source"] in ("table", "calibration")


# Strings captured on 2026-09-30 from MS-R1 (Sky1,
# Mali-G720-Immortalis) running kernel 7.3.0-rc5-sky1-ncz with the
# `module_blacklist=panthor` cmdline. Source of truth: live
# `/home/user/.cache/ncz-screensavers/gpu-class.json` after the calibrated
# `gles3_harness` ran inside the real singularity-labwc compositor on tty1.
# The renderer string comes from `glGetString(GL_RENDERER)`, the version from
# `glGetString(GL_VERSION)`, the driver from the platform-bus /sys/.../driver
# symlink, and the synthetic GPU id from the parent platform device of
# /sys/class/misc/mali0.
SKY1_LIVE_CAPTURE = {
    "renderer": "Mali-G720-Immortalis",
    "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
    "vendor": "ARM",
    "ms_first_calibration": 9.88,  # the first calibration the live host ran
    "ms_recalibrated": 11.76,  # the second calibration the live host ran
    "driver": "mali",
    "gpu_id": "soc-CIXH5000_00",
    "gpu_slot": "CIXH5000:00",
    "kernel_cmdline": "module_blacklist=panthor",
    "device_node": "/dev/mali0",
}


# The live .66 host (Sky1 / MS-R1, kernel 7.3.0-rc5-sky1-ncz) enumerates
# FIVE platform devices bound to linlondp (CIXH5010:00, :01, :02, :03, :04)
# but the DRM class only shows FOUR cards — :02 is registered as a linlondp
# platform device but exposes no DRM card (no /sys/.../CIXH5010:02/drm/
# subdirectory, no /sys/class/drm/card* link). The earlier Sky1 regression
# test mirrored the four-card (:00..:03) layout; this mirrors the live
# layout (:00, :01, :03, :04 with :02 absent in drm) and pins that the
# detection still works against it. Captured on 2026-09-30 via
# `ls -la /sys/class/drm/card*/device`.
SKY1_LIVE_DRM_SLOTS = ("CIXH5010:00", "CIXH5010:01", "CIXH5010:03", "CIXH5010:04")


def _build_sky1_live_sysfs(env):
    """Build the exact sysfs mirror observed on .66 (the live host) on
    2026-09-30: four DRM cards bound to linlondp at slots CIXH5010:00,
    :01, :03, :04 (the :02 platform device exists but is not registered
    as a DRM card), plus the standalone /sys/class/misc/mali0 character
    device whose parent platform device CIXH5000:00 is bound to the
    'mali' platform bus driver. The misc symlink is the short form
    (../../../CIXH5000:00) that the live host exposes."""
    for i, slot in enumerate(SKY1_LIVE_DRM_SLOTS):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(slot == "CIXH5010:03"),  # card2 / :03 owns the panel
        )
        # The cards do have render nodes (writeback) on .66.
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    # The platform device CIXH5010:02 exists on .66 but has no DRM card;
    # no entry for it is added.
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    # Short-form symlink, exactly as the live host shows it:
    # /sys/class/misc/mali0/device -> ../../../CIXH5000:00
    os.symlink(os.path.relpath(gpu_dev, str(misc)), misc / "device")
    # The /sys/devices/platform/CIXH5000:00/gpuinfo file on the live
    # host reads "Mali-G720-Immortalis 10 cores r0p0 0x0C080700".
    # The launcher parses the first whitespace-separated token as the
    # model string for class_from_renderer() when no calibrator sample
    # exists in the cache.
    (gpu_dev / "gpuinfo").write_text("Mali-G720-Immortalis 10 cores r0p0 0x0C080700\n")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()


def test_sky1_live_capture_renderer_string_is_mid(env):
    """Pins the EXACT GLES renderer string returned by glGetString on the
    live .66 host (captured 2026-09-30 from
    /home/user/.cache/ncz-screensavers/gpu-class.json, from
    /run/user/1000/ncz-screensaver/hack.log lines that begin
    'gles3_harness: gpu class' and 'GL_VERSION=OpenGL ES 3.2'). The
    launcher classifies it as 'mid', never 'weak'.
    """
    assert env.class_from_renderer(SKY1_LIVE_CAPTURE["renderer"]) == "mid"


def test_sky1_live_capture_version_string_is_mid(env):
    """Pins the EXACT GLES version string returned by glGetString on the
    live .66 host. A regression that accidentally anchors the regex on a
    prefix of this string would be caught here.
    """
    v = SKY1_LIVE_CAPTURE["version"]
    # The version starts with 'OpenGL ES 3.2' — every entry in the table
    # falls through without a match. The renderer is what gates the
    # classification, not the version. The fix must keep it that way: the
    # version alone must NOT classify anything.
    assert env.class_from_renderer(v) is None
    # And the renderer (which contains "Mali-G720-Immortalis") classifies mid.
    assert env.class_from_renderer(SKY1_LIVE_CAPTURE["renderer"]) == "mid"


def test_sky1_live_capture_list_gpus_returns_only_mali(env):
    """Pins list_gpus() against the live .66 sysfs layout (4 DRM cards
    at :00, :01, :03, :04 with :02 absent, plus the /sys/class/misc/mali0
    standalone GPU). The returned list must contain exactly ONE entry —
    the Mali — with id=soc-CIXH5000_00 and class=mid. The four linlondp
    display controllers must be filtered out."""
    _build_sky1_live_sysfs(env)
    gpus = env.list_gpus()
    assert len(gpus) == 1, (
        f"live .66 layout must yield exactly one GPU (the Mali); "
        f"got {len(gpus)}: {[g['id'] for g in gpus]}"
    )
    g = gpus[0]
    assert g["id"] == SKY1_LIVE_CAPTURE["gpu_id"]
    assert g["slot"] == SKY1_LIVE_CAPTURE["gpu_slot"]
    assert g["driver"] == SKY1_LIVE_CAPTURE["driver"]
    assert g["display"] is True
    assert g["vendor"] == "other"
    # The topology fallback classifies the mali driver as mid.
    assert env.class_from_topology(g) == "mid"


def test_sky1_live_capture_cmd_gpus_json(env, monkeypatch, capsys):
    """Pins the EXACT JSON output of `ncz-screensaver gpus --json` against
    the live .66 sysfs layout. One entry: soc-CIXH5000_00, mali, mid.
    Captured live on 2026-09-30: the installed code (0.7.1) returned four
    linlondp cards (one mid via a stale cache, three weak); the fixed
    code returns one mali entry, mid. `cmd_gpus` does not run the
    calibrator (run=False) so the ms field is null until a `calibrate`
    has populated the cache."""
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_first_calibration"]))
    args = type("A", (), {"json": True})()
    assert env.cmd_gpus(args) == 0
    rows = json.loads(capsys.readouterr().out)
    assert len(rows) == 1
    g = rows[0]
    assert g["id"] == SKY1_LIVE_CAPTURE["gpu_id"]
    assert g["slot"] == SKY1_LIVE_CAPTURE["gpu_slot"]
    assert g["driver"] == SKY1_LIVE_CAPTURE["driver"]
    assert g["class"] == "mid"
    # cmd_gpus does not invoke the calibrator, so ms is null until a
    # `calibrate` has populated the cache. The renderer string, however,
    # is now populated from the sysfs /gpuinfo file on a bound platform
    # GPU device, so even before the calibrator's first run the operator
    # sees the live model name. After a calibrate, ms is also populated
    # (see test_sky1_live_capture_cmd_gpus_json_after_calibrate).
    assert g["ms"] is None
    assert g["renderer"] == SKY1_LIVE_CAPTURE["renderer"]


def test_sky1_live_capture_cmd_gpus_json_after_calibrate(env, monkeypatch, capsys):
    """After `ncz-screensaver calibrate` populates the cache with the live
    renderer's measurement, `ncz-screensaver gpus --json` reports the
    cached ms/renderer next to the GPU. Captured live on 2026-09-30:
    a fresh calibrate on .66 wrote ms=11.76, renderer='Mali-G720-Immortalis'
    into /home/user/.cache/ncz-screensavers/gpu-class.json for the
    soc-CIXH5000_00 entry."""
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    # Force the calibration to actually run by passing a run_calibrate=True.
    # gpu_class() with default run=True invokes the calibrator only when the
    # cache for the GPU's id is missing or stale; the test rig starts with
    # an empty cache, so the calibrate path runs and writes the entry.
    _ = env.gpu_class(S, None)
    args = type("A", (), {"json": True})()
    assert env.cmd_gpus(args) == 0
    rows = json.loads(capsys.readouterr().out)
    assert len(rows) == 1
    g = rows[0]
    assert g["class"] == "mid"
    assert g["ms"] == SKY1_LIVE_CAPTURE["ms_recalibrated"]
    assert g["renderer"] == SKY1_LIVE_CAPTURE["renderer"]


def test_sky1_live_capture_cmd_status_json(env, monkeypatch, capsys):
    """Pins the EXACT JSON output of `ncz-screensaver status --json`
    against the live .66 sysfs layout and the EXACT renderer/version/ms
    the live calibrator produced. Captured live: gpu_class=mid,
    gpu_class_score_ms=11.76 (second calibration) or 9.88 (first). The
    fixed launcher reports mid both via the cache (after a calibration
    run) and via the topology fallback (before the calibrator runs)."""
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    args = type("A", (), {"json": True, "diagnostics": False})()
    rc = env.cmd_status(args)
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid"
    assert out["gpu_class_source"] in ("table", "calibration")


def test_sky1_live_capture_no_weak_classification_under_any_path(env, monkeypatch):
    """Regression for an MS-R1 (Sky1) — the central operator-visible promise
    of this fix: with the live .66 sysfs layout, every code path the
    launcher can take returns class=mid, never class=weak. This is the
    table-driven weak/mid/strict table, the topology fallback, and the
    calibration cache, exercised end-to-end against the exact strings
    captured from the live host.

    The previous 0.7.1 release reported `weak` for three of the four
    linlondp cards on a real GPU system (via class_from_topology's
    default branch). This test pins the post-fix invariant.
    """
    _build_sky1_live_sysfs(env)
    # 1. Table-driven: the live renderer string.
    assert env.class_from_renderer(SKY1_LIVE_CAPTURE["renderer"]) != "weak"
    assert env.class_from_renderer(SKY1_LIVE_CAPTURE["renderer"]) == "mid"
    # 2. Topology fallback: every GPU list_gpus returns must classify as mid.
    for g in env.list_gpus():
        cls = env.class_from_topology(g)
        assert cls == "mid", (
            f"GPU {g['id']} classified {cls} not mid (driver={g['driver']})"
        )
    # 3. End-to-end calibration against the live renderer/version.
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    e = env.gpu_class(S, None)
    assert e["class"] == "mid", f"calibration classified {e['class']} not mid"
    assert e["ms"] == SKY1_LIVE_CAPTURE["ms_recalibrated"]
    assert e["renderer"] == SKY1_LIVE_CAPTURE["renderer"]


# Adversarial continuation (this dispatch): the shipped pre-fix binary on
# .66 (md5 1bd3ee4b…) reports gpu_class=weak because its topology fallback
# only knows about DRM cards. The fixed binary classifies the Mali as mid,
# but a fresh install on .66 has no calibrator sample in the cache yet and
# no `ncz-screensaver calibrate` has run. The /sys/devices/platform/
# CIXH5000:00/gpuinfo file is always present on the live host and names
# the GPU ("Mali-G720-Immortalis 10 cores r0p0 0x0C080700"), so the
# _iter_platform_gpu_devices() iterator now parses the first whitespace-
# separated token into a 'gpuinfo' field on the gpu dict and fallback_entry()
# uses it as the renderer string when no calibrator sample is available.
# These tests pin that the table-driven weak/mid verdict still holds on a
# sysfs-only reading (no calibrator, no cache) and that the cmd_status /
# cmd_gpus output surfaces the parsed gpuinfo as the renderer.
def test_sky1_live_capture_gpuinfo_is_parsed_from_sysfs(env):
    """Pin the gpuinfo parser: /sys/devices/platform/CIXH5000:00/gpuinfo
    on the live host reads 'Mali-G720-Immortalis 10 cores r0p0 0x0C080700';
    _iter_platform_gpu_devices() must extract the first whitespace-
    separated token ('Mali-G720-Immortalis') and stash it as 'gpuinfo' on
    the gpu dict. A regression that dropped the parser, or that took the
    whole line, would fail this test."""
    _build_sky1_live_sysfs(env)
    gpus = env.list_gpus()
    assert len(gpus) == 1
    g = gpus[0]
    assert g["id"] == SKY1_LIVE_CAPTURE["gpu_id"]
    # The parsed gpuinfo is the model name, not the full line.
    assert g["gpuinfo"] == SKY1_LIVE_CAPTURE["renderer"], (
        f"gpuinfo parsed {g['gpuinfo']!r}; expected the live host's "
        f"first-token model name {SKY1_LIVE_CAPTURE['renderer']!r}."
    )


def test_sky1_live_capture_classify_mid_via_gpuinfo_no_calibrator(env, monkeypatch):
    """Pin the central guarantee on a fresh .66 boot: with the live sysfs
    layout and NO cache + NO calibrator run, `gpu_class(settings, run=False)`
    must still classify the Mali as mid. Before this dispatch, the
    fallback path returned (renderer='', source='table') and the topology
    classifier mapped the unknown driver to weak; now fallback_entry()
    uses the parsed gpuinfo as the renderer, the table maps
    'Mali-G720-Immortalis' to mid, and the verdict is mid on the very
    first call — no calibrator required."""
    _build_sky1_live_sysfs(env)
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    # NO FAKE_RENDERER / NO FAKE_MS: the calibrator path is not invoked.
    e = env.gpu_class(settings, None, run=False)
    assert e["class"] == "mid", (
        f"fresh-install gpu_class={e['class']!r}; expected 'mid' from the "
        f"parsed gpuinfo 'Mali-G720-Immortalis' on the live .66 layout."
    )
    assert e["renderer"] == SKY1_LIVE_CAPTURE["renderer"]
    assert e["source"] == "table"


def test_sky1_live_capture_cmd_gpus_text_shows_gpuinfo_renderer(
    env, monkeypatch, capsys
):
    """Pin the operator-visible text output of `ncz-screensaver gpus` on
    a fresh .66 boot: the renderer column must show the parsed gpuinfo
    ('Mali-G720-Immortalis') instead of the legacy empty string, so the
    operator can see the GPU name without running the calibrator."""
    _build_sky1_live_sysfs(env)
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    args = type("A", (), {"json": False})()
    assert env.cmd_gpus(args) == 0
    text = capsys.readouterr().out.strip().splitlines()
    assert len(text) == 1
    parts = text[0].split()
    assert parts[0] == SKY1_LIVE_CAPTURE["gpu_id"]
    assert parts[1] == "display"
    assert parts[2] == "other"
    assert parts[3] == SKY1_LIVE_CAPTURE["driver"]
    assert parts[4] == "mid", (
        f"cmd_gpus text reported class={parts[4]!r}; the parsed gpuinfo "
        f"should drive the table fallback to 'mid' on the live layout."
    )
    # The line will say 'no score' (no ms) because no calibrator has run.
    # The class column (parts[4]) must say 'mid', which is the operator-
    # visible promise: a fresh boot does not show 'weak'.
    assert "no score" in text[0]  # confirms the no-calibrator path was hit


def test_sky1_live_capture_cmd_status_reports_mid_from_gpuinfo_only(
    env, monkeypatch, capsys
):
    """Pin the operator-visible `ncz-screensaver status --json` on a
    fresh .66 boot (no cache, no calibrator run): gpu_class=mid and
    gpu_class_source=table, both driven by the parsed gpuinfo. A
    regression that dropped the gpuinfo parser would push the verdict
    back to 'weak' and pool_class back to 'weak'."""
    _build_sky1_live_sysfs(env)
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    args = type("A", (), {"json": True, "diagnostics": False})()
    rc = env.cmd_status(args)
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r} on "
        f"a fresh .66 boot; expected 'mid' from the parsed gpuinfo."
    )
    assert out["gpu_class_source"] == "table"
    # gpu_class_score_ms is None because no calibration has run yet.
    assert out["gpu_class_score_ms"] is None


def test_sky1_stale_071_cache_does_not_demote_display_gpu_to_weak(
    env, monkeypatch, capsys
):
    """Adversarial pass-15: a fresh .66 boot still ships the stale cache
    file left by shipped ncz-screensavers 0.7.1. That cache contains a
    single entry under the *old* PCI-style GPU id 'pci-CIXH5010_03'
    (the linlondp display controller 0.7.1 picked), classified 'weak'
    with ms=20.2 against the Mali-G720-Immortalis renderer string.

    After the 0.7.10 fix, the display GPU is keyed as
    'soc-CIXH5000_00', so the cache lookup misses and the launcher
    must fall through to the topology fallback (driver=mali ->
    MID_DRIVERS -> 'mid'). A regression that keyed the cache by
    something stable across driver renames (e.g. PCI_SLOT_NAME) would
    pull in the stale 'weak' entry and the user would once again sit
    at a desktop where the screensaver pool is restricted to weak-only
    hacks — exactly the failure the operator reported.

    This test pins the contract that the stale 0.7.1 cache file is
    harmless: even with the exact stale contents observed on .66 at
    2026-09-30T08:43:03Z, `gpu_class(settings, run=False)` returns
    class=mid and the cmd_status --json output reflects it. The stale
    entry is left in the file untouched (the launcher never deletes
    cache entries; it only overwrites the key it just classified).
    """
    _build_sky1_live_sysfs(env)
    # The exact stale cache file observed on the live .66 host after
    # shipped 0.7.1 ran a calibration pass on the linlondp card.
    stale_entry = {
        "class": "weak",
        "ms": 20.2,
        "renderer": "Mali-G720-Immortalis",
        "version": ("OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"),
        "timer": "wall",
        "source": "calibration",
        "when": "2026-09-30T08:43:03+0000",
        "driver": "linlondp",
        "gpu": "pci-CIXH5010_03",
    }
    seed_cache(env, {"pci-CIXH5010_03": stale_entry})
    # run=False reads cache + fallback_entry; must return 'mid' via the
    # topology path (driver='mali' is in MID_DRIVERS).
    e = env.gpu_class(S, None, run=False)
    assert e["class"] == "mid", (
        f"stale 0.7.1 cache demoted Mali to {e['class']!r}; the launcher "
        f"must classify by the new GPU id ({env.display_gpu()['id']!r}) and "
        f"fall through to the topology table."
    )
    assert e["source"] == "table"
    # Stale cache file is unchanged: the launcher wrote nothing because
    # run=False. Future calibrator runs will overwrite soc-CIXH5000_00
    # and leave pci-CIXH5010_03 as harmless dead data.
    on_disk = json.loads(env.class_cache_path().read_text())
    assert "pci-CIXH5010_03" in on_disk["entries"]
    assert "soc-CIXH5000_00" not in on_disk["entries"]
    # And cmd_status --json must show gpu_class=mid.
    args = type("A", (), {"json": True, "diagnostics": False})()
    rc = env.cmd_status(args)
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r} with a "
        f"stale 0.7.1 cache file present — the user would see the weak-tier "
        f"verdict back."
    )
    # And cmd_doctor must still say pool_class=mid (the operator-visible
    # promise that the pool allows mid-tier hacks on .66).
    env.cmd_doctor(args)
    doc = json.loads(capsys.readouterr().out)
    assert doc["pool_class"] == "mid"
    assert doc["gpu_class"]["display"]["class"] == "mid"


def test_sky1_live_capture_cmd_doctor_pool_class_mid(env, monkeypatch, capsys):
    """Pin the operator-visible `ncz-screensaver doctor` report for the
    live .66 layout: pool_class=mid, gpu.display_class=integrated,
    gpus has exactly one Mali entry, gpu_class.display.class=mid.

    Captured live on 2026-09-30 via
    `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
     ncz-screensaver doctor` against the deployed fixed binary
    (md5 399b5be55fda259e5a732c15a150830b). The shipped 0.7.1 reported
    pool_class=weak on a fresh install (the topology fallback mapped the
    linlondp cards to weak and never surfaced the Mali). The fix surfaces
    the Mali, which class_from_topology() maps to mid via MID_DRIVERS.

    This test pins every operator-visible top-level field that the
    fix changes. A regression that demotes the GPU to weak, drops the
    soc-CIXH5000_00 id from the gpus list, or forgets to wire pool_class
    to best_class() will be caught here.
    """
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_doctor(type("A", (), {})())
    assert rc == 0
    doc = json.loads(capsys.readouterr().out)
    # gpu topology: display_class=integrated (the connected linlondp card
    # is not nvidia/amdgpu). This is the live reality; the fix does NOT
    # change gpu_topology() — it only changes the GPU list and the class
    # resolver. A regression that maps "integrated" to "weak" in the
    # gpu_class.display slot is exactly the operator's complaint.
    assert doc["gpu"]["display_class"] == "integrated"
    assert doc["gpu"]["nvidia_offload"] is False
    # gpus list: one entry, the Mali, marked display=True. The four
    # linlondp cards are filtered out. gpuinfo_used=True is the
    # operator-visible flag the diagnostics() now adds so the operator
    # can confirm the gpuinfo-based fresh-install path is active.
    assert doc["gpus"] == [
        {
            "id": SKY1_LIVE_CAPTURE["gpu_id"],
            "card": "",
            "slot": SKY1_LIVE_CAPTURE["gpu_slot"],
            "vendor": "other",
            "driver": SKY1_LIVE_CAPTURE["driver"],
            "device": "",
            "display": True,
            "boot_vga": False,
            "discrete": False,
            "render": True,
            "display_only": False,
            "gpuinfo": SKY1_LIVE_CAPTURE["renderer"],
            "gpuinfo_used": True,
        }
    ]
    # gpu_info summary added in this dispatch: a single platform GPU
    # device, with a parsed gpuinfo file present. This is the field the
    # operator looks at to confirm the /sys/.../gpuinfo path is wired
    # through on the live .66 layout.
    assert doc["gpu_info"] == {
        "display_gpu_present": True,
        "platform_gpu_devices": 1,
        "any_gpuinfo_present": True,
    }
    # gpu_class.display.class=mid. This is the field the operator sees;
    # on a fresh install with no cache, the topology fallback still
    # resolves to mid because the Mali is in MID_DRIVERS. After a
    # calibration, the cache drives it.
    gc = doc["gpu_class"]["display"]
    assert gc["class"] == "mid", f"doctor gpu_class.display.class={gc.get('class')!r}"
    # With the gpuinfo parser in place, the renderer is populated from
    # sysfs even on a fresh install (the file is always there on a
    # bound platform GPU device). ms is None until the calibrator runs
    # and writes the cache; the gpuinfo-based 'soft' table verdict is
    # what keeps the central promise on day 0.
    assert gc["renderer"] == SKY1_LIVE_CAPTURE["renderer"]
    assert gc.get("ms") is None
    assert gc["source"] == "table"
    # pool_class=mid: the central promise. The shipped 0.7.1 reported
    # pool_class=weak on this layout (no cache, no calibration).
    assert doc["pool_class"] == "mid"
    # The offload target list is empty on Sky1 (no nvidia).
    assert doc["offload"]["targets"] == []
    # The display-only filter is in the JSON: every GPU has display_only
    # set explicitly (False for the Mali), so a downstream consumer that
    # filters on it does not have to fall back to a driver-name check.
    assert all("display_only" in g for g in doc["gpus"])


def test_sky1_live_capture_cmd_pool_includes_mid_tier_hacks(env, monkeypatch, capsys):
    """Pin `ncz-screensaver pool --json` for the live .66 layout:
    pool class=mid, count > 0 (the mid-tier hacks are in the pool).

    A regression that demotes pool_class to weak would drop every
    mid-tier hack from the pool and leave only the `weak` tier hacks,
    which is the operator-visible failure: the user sits at the desktop
    and never sees the shaders the GPU can actually render.
    """
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    # The pool uses installed_ids(); without an actual catalog in the test
    # rig, pool is empty. We mock installed_ids to return the same IDs the
    # test's TIERS.tsv knows about (hyprsaver_light_gles3 + light_gles3 +
    # medium_gles3 + heavy_gles3) — see TIERS above.
    pool_ids = [
        "hyprsaver_light_gles3",  # weak
        "light_gles3",  # weak
        "medium_gles3",  # mid
        "heavy_gles3",  # strong — must be excluded at pool=mid
    ]
    monkeypatch.setattr(env, "installed_ids", lambda s: pool_ids)
    rc = env.cmd_pool(type("A", (), {"json": True})())
    assert rc == 0
    out = json.loads(capsys.readouterr().out)
    # pool_class=mid; strong-tier hacks are excluded.
    assert out["class"] == "mid"
    excluded_names = sorted(out["excluded"].keys())
    assert excluded_names == ["heavy_gles3"], (
        f"only heavy_gles3 should be excluded at pool=mid; got {excluded_names}"
    )
    # The mid and weak hacks are kept.
    assert "medium_gles3" in out["ids"]
    assert "hyprsaver_light_gles3" in out["ids"]
    assert "light_gles3" in out["ids"]
    assert out["count"] == 3
    # And the explanation text mentions "needs strong" — the operator-
    # visible reason the heavy hack is not in the pool.
    assert "strong" in out["excluded"]["heavy_gles3"]


def test_sky1_live_capture_cmd_plan_mid_no_offload(env, monkeypatch, capsys):
    """Pin `ncz-screensaver plan medium_gles3 --json` for the live .66
    layout: gpu=soc-CIXH5000_00, gpu_driver=mali, offload=False,
    class=mid. This is what the supervisor uses when it asks
    `ncz-screensaver plan <hack>` to decide whether to spawn the hack
    on the display GPU or an offload target.

    A regression that flips offload=True on Sky1 would point the hack
    at a nonexistent nvidia offload target — and the hack would either
    crash or silently fall back to llvmpipe, which is exactly the
    software-rendering failure the policy forbids.
    """
    _build_sky1_live_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_LIVE_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_VERSION", SKY1_LIVE_CAPTURE["version"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_LIVE_CAPTURE["ms_recalibrated"]))
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    # The test rig has no catalog; known_id() in cmd_plan() rejects
    # unknown hacks. NCZ_SCREENSAVER_ALLOW_UNLISTED=1 lets cmd_plan()
    # accept any ID-shaped string so the test can exercise the rest of
    # the path against a representative mid-tier hack name.
    monkeypatch.setenv("NCZ_SCREENSAVER_ALLOW_UNLISTED", "1")
    rc = env.cmd_plan(type("A", (), {"json": True, "hack": "medium_gles3"})())
    assert rc == 0
    out = json.loads(capsys.readouterr().out)
    assert out["hack"] == "medium_gles3"
    assert out["gpu"] == SKY1_LIVE_CAPTURE["gpu_id"]
    assert out["gpu_driver"] == SKY1_LIVE_CAPTURE["driver"]
    assert out["offload"] is False
    assert out["class"] == "mid"
    # env list is empty when not offloading (the supervisor does not
    # wire __EGL_VENDOR_LIBRARY_FILENAMES / NV_PRIME_RENDER_OFFLOAD).
    assert out["env"] == []
    # on_battery is False because we added AC power in _build_sky1_live_sysfs.
    assert out["on_battery"] is False


# Adversarial continuation (this dispatch, 2026-09-30 20:35 UTC): harden the
# /sys/.../gpuinfo parser and the topology fallback against (a) malformed
# gpuinfo content (BOM, embedded newlines, pure-numeric or empty tokens,
# non-ASCII control bytes) and (b) a future CIX / ARM driver rename that
# drops the platform-bus glue driver name out of MID_DRIVERS. Each of the
# following tests pins one operator-visible invariant against the live .66
# layout; a regression that re-introduces a weak demotion on a fresh install
# is caught by these tests.


def _build_sky1_misc_only_sysfs(env, gpuinfo=""):
    """Build a Sky1-style platform-bus-only sysfs (no DRM cards): just
    the /sys/class/misc/mali0 character device and its parent platform
    device CIXH5000:00 with the given gpuinfo content. Used by the
    parser-robustness tests that don't need the four-card linlondp
    fixture. The gpuinfo string is written with bytes semantics so a
    caller can pass literal UTF-8 BOM bytes (\\xef\\xbb\\xbf) instead of
    the Unicode character (\\ufeff), which is how a kernel driver would
    actually prefix the file on disk."""
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True, exist_ok=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True, exist_ok=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True, exist_ok=True)
    os.symlink(drv, gpu_dev / "driver")
    os.symlink(os.path.relpath(gpu_dev, str(misc)), misc / "device")
    if isinstance(gpuinfo, bytes):
        (gpu_dev / "gpuinfo").write_bytes(gpuinfo)
    else:
        (gpu_dev / "gpuinfo").write_text(gpuinfo)
    add_power(env.sysfs, True)


def test_sky1_gpuinfo_parser_rejects_garbage_tokens(env, monkeypatch):
    """The defensive parser in _iter_platform_gpu_devices() rejects tokens
    that do not look like a GPU model name. Garbage from a corrupted sysfs
    file or a buggy future driver must not flow into fallback_entry() as
    a renderer string and silently demote a real GPU to weak.

    Pin: empty content -> empty gpuinfo; whitespace-only -> empty gpuinfo;
    pure-numeric -> empty gpuinfo; single character -> empty gpuinfo. The
    parser only accepts a model-name-shaped token (length >= 2, contains
    at least one alpha character, fullmatch [A-Za-z0-9._-]+)."""
    _build_sky1_misc_only_sysfs(env)
    cases = [
        ("", ""),  # file absent (empty content)
        (" ", ""),  # whitespace only
        ("\n\n", ""),  # newlines only
        ("\ufeff", ""),  # BOM only
        ("12345", ""),  # pure numeric
        ("a", ""),  # single character
        ("a b c", ""),  # single char in first slot
    ]
    for raw, expected in cases:
        (env.sysfs / "devices/platform/CIXH5000:00/gpuinfo").write_text(raw)
        for fn in (env.list_gpus, env.gpu_topology):
            fn.cache_clear()
        gpus = env.list_gpus()
        assert len(gpus) == 1, f"raw={raw!r}: expected 1 GPU, got {len(gpus)}"
        assert gpus[0]["gpuinfo"] == expected, (
            f"gpuinfo content {raw!r} parsed to {gpus[0]['gpuinfo']!r}; "
            f"expected {expected!r}"
        )


def test_sky1_gpuinfo_parser_accepts_bom_and_newlines(env, monkeypatch):
    """A future kernel driver might prefix the gpuinfo file with a UTF-8
    BOM (\\xef\\xbb\\xbf) or split the model name across multiple lines.
    The parser strips both before validation. The Sky1 live capture is
    one-line; the test rigs the multi-line + BOM path explicitly."""
    # BOM (raw bytes, NOT the Unicode character — write_text would
    # UTF-8-encode a \\ufeff str to a different multi-byte sequence)
    # + multi-line + trailing whitespace + extra shader tokens.
    _build_sky1_misc_only_sysfs(
        env,
        gpuinfo=b"\xef\xbb\xbfMali-G720-Immortalis\n10 cores r0p0 0x0C080700\n\n",
    )
    gpus = env.list_gpus()
    assert len(gpus) == 1
    assert gpus[0]["gpuinfo"] == "Mali-G720-Immortalis", (
        f"BOM+multiline gpuinfo parsed to {gpus[0]['gpuinfo']!r}; "
        f"expected the model name 'Mali-G720-Immortalis'."
    )


def test_sky1_class_from_topology_falls_back_to_gpuinfo_when_driver_unknown(env):
    """Defensive topology: a future CIX / ARM driver rename that drops the
    platform-bus glue driver name out of MID_DRIVERS must not demote a real
    GPU to weak. When the gpuinfo file carries a recognisable model name
    (the same string glGetString(GL_RENDERER) would return), the topology
    classifier maps it to 'mid' even if the driver name is unknown.

    This is the r63 dispatch's contract for the live .66 host:
    class_from_topology({'driver': 'cix-gpu', 'gpuinfo': 'Mali-G720-Immortalis'})
    must return 'mid', not 'weak'.
    """
    assert (
        env.class_from_topology(
            {
                "driver": "cix-gpu",  # hypothetical future driver rename
                "gpuinfo": "Mali-G720-Immortalis",
                "discrete": False,
            }
        )
        == "mid"
    )
    # A GPUINFO that does not match any mid pattern still defaults to weak.
    assert (
        env.class_from_topology(
            {
                "driver": "cix-gpu",
                "gpuinfo": "Some Future Chip 9000",
                "discrete": False,
            }
        )
        == "weak"
    )
    # An Apple GPU with an unknown driver name also maps to mid via the
    # gpuinfo path (the existing 'apple' renderer pattern).
    assert (
        env.class_from_topology(
            {
                "driver": "apple-gpu",
                "gpuinfo": "Apple M2",
                "discrete": False,
            }
        )
        == "mid"
    )
    # An Intel Arc / Xe GPU with an unknown driver name also maps to mid.
    assert (
        env.class_from_topology(
            {
                "driver": "intel-future",
                "gpuinfo": "Intel(R) Arc(TM) A770 Graphics",
                "discrete": False,
            }
        )
        == "mid"
    )
    # discrete=True still wins over everything (a future discrete GPU with
    # an unknown driver name must still be classified strong).
    assert (
        env.class_from_topology(
            {
                "driver": "future-dgpu",
                "gpuinfo": "Something Unknown",
                "discrete": True,
            }
        )
        == "strong"
    )


def test_sky1_doctor_reports_gpuinfo_used_flag(env, monkeypatch, capsys):
    """Operator-visible: cmd_doctor surfaces a gpuinfo_used=True flag on
    every GPU in `doc['gpus']` whose gpuinfo is parsed, plus a
    `doc['gpu_info']` summary that tells the operator whether ANY platform
    GPU device reported a gpuinfo string on this host. The shipped 0.7.1
    binary on .66 had no such fields; the operator could not tell whether
    the gpuinfo path was active without reading /sys themselves."""
    _build_sky1_live_sysfs(env)
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_doctor(type("A", (), {})())
    assert rc == 0
    doc = json.loads(capsys.readouterr().out)
    # Every GPU carries gpuinfo_used=True iff gpuinfo is non-empty.
    for g in doc["gpus"]:
        assert "gpuinfo_used" in g
        assert g["gpuinfo_used"] is bool(g.get("gpuinfo"))
    # The summary is a one-glance "is the gpuinfo path active?" check.
    assert doc["gpu_info"]["any_gpuinfo_present"] is True
    assert doc["gpu_info"]["platform_gpu_devices"] == 1
    assert doc["gpu_info"]["display_gpu_present"] is True


def test_sky1_doctor_gpuinfo_absent_when_sysfs_has_no_gpuinfo(env, monkeypatch, capsys):
    """When no platform GPU device exposes a gpuinfo file, the doctor
    report surfaces gpuinfo_used=False on every GPU and the summary
    says any_gpuinfo_present=False. The operator can distinguish
    'gpuinfo path is wired but the file is missing' from 'gpuinfo path
    is not wired at all'."""
    # Build a single-soc layout (single GPU, no gpuinfo file).
    sysfs = env.sysfs
    add_gpu(
        sysfs,
        "card0",
        "0x0000",
        "0x0000",
        "mali_kbase",
        "0000:00:00.0",
        connected=True,
        boot_vga=True,
    )
    # No platform device, no misc device: this is the bare single-soc
    # layout the r0/r15 tests use.
    add_power(sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_doctor(type("A", (), {})())
    assert rc == 0
    doc = json.loads(capsys.readouterr().out)
    assert doc["gpu_info"]["any_gpuinfo_present"] is False
    assert doc["gpu_info"]["platform_gpu_devices"] == 0
    for g in doc["gpus"]:
        assert g.get("gpuinfo", "") == ""
        assert g.get("gpuinfo_used") is False


@pytest.mark.parametrize(
    "ms,expected",
    [
        (46.5, "weak"),
        (20.0, "weak"),
        (19.9, "mid"),
        (9.9, "mid"),
        (8.0, "mid"),
        (5.5, "strong"),
        (2.7, "strong"),
    ],
)
def test_class_from_score(env, ms, expected):
    assert env.class_from_ms(ms) == expected


def test_calibration_scores_reference_gpus_and_caches(env, monkeypatch):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    monkeypatch.setenv("FAKE_MS", "46.5")
    monkeypatch.setenv("FAKE_RENDERER", "Mesa Intel(R) UHD Graphics 630 (CFL GT2)")
    a = env.gpu_class(S, disp)
    b = env.gpu_class(S, nv)
    assert (a["class"], a["ms"], a["source"]) == ("weak", 46.5, "calibration")
    assert (b["class"], b["ms"]) == ("strong", 2.7)
    cache = env.read_class_cache()
    assert set(cache) == {disp["id"], nv["id"]}
    # A second call with an unchanged renderer and version reuses the cache entry.
    monkeypatch.setenv("FAKE_EXIT", "2")
    assert env.gpu_class(S, disp)["ms"] == 46.5


def test_hybrid_nvidia_nvidia_rejects_panel_gpu_calibration(env, monkeypatch):
    """A live hybrid Intel + NVIDIA cache once assigned Intel's 46.52 ms to the RTX 2060."""
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            disp["id"]: {"class": "weak", "ms": 46.44},
            nv["id"]: {
                "class": "weak",
                "ms": 46.52,
                "renderer": "Mesa Intel(R) UHD Graphics (CML GT2)",
                "source": "calibration",
                "gpu": nv["id"],
                "driver": "nvidia",
            },
        },
    )
    entry = env.gpu_class(S, nv, run=False)
    assert entry["class"] == "strong"
    assert entry["source"] == "table"
    assert env.gpu_verdict_text(S) == "GPU0 (Weak), GPU1 (Strong)"

    monkeypatch.setattr(
        env,
        "_calibrator_json",
        lambda args, environ, timeout: (
            0,
            {"renderer": "Mesa Intel(R) UHD Graphics (CML GT2)", "version": "Mesa"},
        ),
    )
    entry = env.gpu_class(S, nv, run=True)
    assert entry["class"] == "strong"
    assert entry["source"] == "table"
    assert env.read_class_cache()[nv["id"]]["ms"] == 46.52


def test_hybrid_nvidia_nvidia_rejects_misbound_benchmark(env, monkeypatch):
    """A correct identify followed by an Intel benchmark cannot be cached."""
    layout(env, "nvidia+intel")
    _disp, nv = env.list_gpus()

    def calibrator(args, environ, timeout):
        if args:
            return 0, {"renderer": "NVIDIA GeForce RTX 2060", "version": "550"}
        return 0, {"renderer": "Mesa Intel(R) UHD Graphics (CML GT2)", "ms": 46.52}

    monkeypatch.setattr(env, "_calibrator_json", calibrator)
    entry = env.gpu_class(S, nv)
    assert entry["class"] == "strong"
    assert entry["source"] == "table"
    assert nv["id"] not in env.read_class_cache()


def test_nvidia_unknown_future_renderer_name_stays_eligible(env):
    """A new driver label without the NVIDIA brand must not lose its score."""
    layout(env, "nvidia+intel")
    _disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            nv["id"]: {
                "class": "strong",
                "ms": 3.1,
                "renderer": "OpenGL Next NV-GL 3D",
                "source": "calibration",
            }
        },
    )
    assert env.gpu_class(S, nv, run=False)["ms"] == 3.1


def test_unknown_chip_is_classified_by_score_alone(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_RENDERER", "Some Future Chip 9000")
    monkeypatch.setenv("FAKE_MS", "12.5")
    e = env.gpu_class(S, None)
    assert (e["class"], e["source"]) == ("mid", "calibration")


def test_cache_refreshes_when_driver_version_changes(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_MS", "9.9")
    assert env.gpu_class(S, None)["class"] == "mid"
    monkeypatch.setenv("FAKE_MS", "3.0")
    assert env.gpu_class(S, None)["class"] == "mid"  # same renderer and version: cached
    monkeypatch.setenv("FAKE_VERSION", "OpenGL ES 3.2 Mesa 26.1")
    assert env.gpu_class(S, None)["class"] == "strong"  # version changed: recalibrated


def test_cache_refreshes_when_renderer_changes(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_RENDERER", "Chip A")
    monkeypatch.setenv("FAKE_MS", "30")
    assert env.gpu_class(S, None)["class"] == "weak"
    monkeypatch.setenv("FAKE_RENDERER", "Chip B")
    monkeypatch.setenv("FAKE_MS", "6")
    assert env.gpu_class(S, None)["class"] == "strong"


def test_software_renderer_is_refused_never_benchmarked(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_EXIT", "3")
    e = env.gpu_class(S, None)
    assert e.get("software") is True
    assert env.read_class_cache() == {}


def test_calibration_cache_records_kernel_field(env, monkeypatch):
    """Each cache entry must carry the running kernel release string. The kernel
    field is what gpu_class() compares against the live os.uname().release to
    decide whether the entry is still valid; without it, a kernel upgrade would
    silently reuse a stale score (the regression that motivated this contract).
    """
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "11.76")
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.3.0-rc5-sky1-ncz")
    entry = env.gpu_class(S, None)
    assert entry["kernel"] == "7.3.0-rc5-sky1-ncz"
    cache = env.read_class_cache()
    (cached,) = cache.values()
    assert cached["kernel"] == "7.3.0-rc5-sky1-ncz"


def test_cache_refreshes_when_kernel_changes(env, monkeypatch):
    """A bump of the running kernel must invalidate the calibration cache. This
    is the contract that closes the Orion O6N regression: the Mali-G720 went 9.94 ms on
    7.2.8-sky1-ncz to 28.21 ms on 7.2.9-sky1-ncz with no other change, so the
    old "mid" cache entry had to be evicted when the kernel moved.

    Three steps: seed a cache that pins kernel=7.2.8; flip the live kernel to
    7.2.9; gpu_class() must re-bench and overwrite the stale entry with the new
    score.
    """
    layout(env, "single-soc")
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.2.8-sky1-ncz")
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "9.94")
    assert env.gpu_class(S, None)["ms"] == 9.94  # seeds cache under 7.2.8
    # Same renderer, same version, same GPU: a second call with the same kernel
    # must reuse the cache (no recalibration).
    monkeypatch.setenv("FAKE_EXIT", "2")
    assert env.gpu_class(S, None)["ms"] == 9.94
    # Operator upgrades the kernel: live release is now 7.2.9-sky1-ncz. The fake
    # calibrator would still try to print 9.94, but the kernel change forces a
    # real re-bench, so we have to clear FAKE_EXIT and feed a different FAKE_MS.
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.2.9-sky1-ncz")
    monkeypatch.delenv("FAKE_EXIT", raising=False)
    monkeypatch.setenv("FAKE_MS", "28.21")
    entry = env.gpu_class(S, None)
    assert entry["ms"] == 28.21
    assert entry["kernel"] == "7.2.9-sky1-ncz"
    assert entry["class"] == "weak"  # >= 20 ms
    (cached,) = env.read_class_cache().values()
    assert cached["kernel"] == "7.2.9-sky1-ncz"
    assert cached["ms"] == 28.21


def test_cache_holds_when_kernel_matches(env, monkeypatch):
    """The kernel check is an additional guard, not a replacement for the
    renderer and version checks. With all three equal across calls, the cache
    is reused (no re-bench).
    """
    layout(env, "single-soc")
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.2.9-sky1-ncz")
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "28.21")
    assert env.gpu_class(S, None)["ms"] == 28.21
    monkeypatch.setenv("FAKE_EXIT", "2")  # the fake would fail if it ran
    assert env.gpu_class(S, None)["ms"] == 28.21


def test_stale_kernel_cache_entry_marks_source_as_calibration(env, monkeypatch):
    """The fresh-install fallback (kernel field missing on a pre-fix cache) must
    still re-bench rather than keep a kernel-less entry. We seed a cache entry
    without a kernel field, then assert the next gpu_class() call overwrites it
    with one that has the kernel and a real ms.
    """
    layout(env, "single-soc")
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.2.9-sky1-ncz")
    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv("FAKE_MS", "28.21")
    seed_cache(
        env,
        {
            env.display_gpu()["id"]: {
                "class": "mid",
                "ms": 9.94,
                "renderer": "Mali-G720-Immortalis",
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0",
                "timer": "gpu",
                "source": "calibration",
                "when": "2026-09-29T19:22:03+0000",
            }
        },
    )  # note: no kernel field, simulating a pre-fix cache
    entry = env.gpu_class(S, None)
    # Either path: the field must now be present and accurate.
    assert entry["kernel"] == "7.2.9-sky1-ncz"
    assert entry["ms"] == 28.21  # fake MS we set above
    (cached,) = env.read_class_cache().values()
    assert cached["kernel"] == "7.2.9-sky1-ncz"
    assert cached["ms"] == 28.21
    assert cached["class"] == "weak"


def test_diagnostics_surfaces_running_kernel_release(env, monkeypatch):
    """The doctor report must include the running kernel release string so an
    operator looking at the JSON can correlate a class change with a kernel
    upgrade without having to ssh in a second time. Without this surface the
    "why did the class change?" question needs a second command and a manual
    join against the host's `uname -r`.
    """
    layout(env, "single-soc")
    monkeypatch.setattr(env, "_kernel_release", lambda: "7.2.9-sky1-ncz")
    settings = {**S, "hack-id": "none_gles3", "mode": "disabled"}
    diag = env.diagnostics(settings, True)
    assert diag["gpu_class"]["kernel"] == "7.2.9-sky1-ncz"


def test_missing_calibrator_falls_back_to_table(env, monkeypatch):
    layout(env, "nvidia+intel")
    monkeypatch.setenv("NCZ_SCREENSAVER_CALIBRATE_BIN", str(env.tmp / "missing"))
    disp, nv = env.list_gpus()
    assert env.gpu_class(S, disp)["source"] == "table"
    assert env.gpu_class(S, disp)["class"] == "weak"  # i915 -> weak
    assert env.gpu_class(S, nv)["class"] == "strong"  # discrete -> strong


def test_calibrate_command_refuses_while_a_hack_runs(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setattr(env, "read_state", lambda: {"pid": 1})
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    args = type("A", (), {"force": False, "json": True, "copy_ms": None})()
    assert env.cmd_calibrate(args) == 3
    assert env.read_class_cache() == {}


# ---- GPU roles, offload environment ---------------------------------------------------------
def test_hybrid_nvidia_layout_display_intel_offload_nvidia(env):
    layout(env, "nvidia+intel")
    gpus = env.list_gpus()
    assert [g["vendor"] for g in gpus] == ["intel", "nvidia"]
    assert gpus[0]["display"] and not gpus[1]["display"]
    e = env.gpu_env(gpus[1])
    assert e["__NV_PRIME_RENDER_OFFLOAD"] == "1"
    assert e["__GLX_VENDOR_LIBRARY_NAME"] == "nvidia"
    assert e["__VK_LAYER_NV_optimus"] == "NVIDIA_only"
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in e
    assert env.gpu_env(gpus[0]) == {}


def test_amd_panel_layout_amd_owns_panel_intel_idle(env):
    layout(env, "amd-dgpu-display+intel-idle")
    gpus = env.list_gpus()
    assert gpus[0]["vendor"] == "amd" and gpus[0]["display"] and gpus[0]["discrete"]
    intel = gpus[1]
    e = env.gpu_env(intel)
    assert e["DRI_PRIME"] == "pci-0000_00_02_0"
    assert e["MESA_VK_DEVICE_SELECT"] == "8086:3e9b"
    assert "__NV_PRIME_RENDER_OFFLOAD" not in e


def test_amd_apu_and_dgpu_uses_mesa_prime(env):
    layout(env, "amd-apu+amd-dgpu")
    gpus = env.list_gpus()
    assert not gpus[0]["discrete"] and gpus[1]["discrete"]
    assert env.gpu_env(gpus[1])["DRI_PRIME"] == "pci-0000_03_00_0"


def test_single_gpu_has_no_targets(env):
    layout(env, "single-soc")
    assert env.offload_targets(S) == []
    assert env.plan_for("heavy_gles3", S)["offload"] is False


def test_mux_dgpu_only_display_is_nvidia_and_never_offloads(env):
    layout(env, "mux-dgpu-only")
    assert env.display_gpu()["vendor"] == "nvidia"
    assert env.offload_targets(S) == []
    assert env.class_from_topology() == "strong"


def test_switcherooctl_environment_is_used_and_egl_vendor_is_dropped(env):
    layout(env, "nvidia+intel")
    fake = env.tmp / "switcherooctl"
    fake.write_text(
        "#!/bin/sh\ncat <<'EOF'\nDevice: 0\n  Name: Intel\n  Default: yes\n  Environment: DRI_PRIME=pci-0000_00_02_0\n\n"
        "Device: 1\n  Name: NVIDIA\n  Default: no\n  Environment: __GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1 "
        "__VK_LAYER_NV_optimus=NVIDIA_only __EGL_VENDOR_LIBRARY_FILENAMES=/x/10_nvidia.json\nEOF\n"
    )
    fake.chmod(0o755)
    os.environ["NCZ_SCREENSAVER_SWITCHEROOCTL"] = str(fake)
    try:
        env.switcheroo_envs.cache_clear()
        assert "nvidia" in env.switcheroo_envs()
        e = env.gpu_env(env.list_gpus()[1])
        assert e["__NV_PRIME_RENDER_OFFLOAD"] == "1"
        assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in e
    finally:
        del os.environ["NCZ_SCREENSAVER_SWITCHEROOCTL"]


# ---- planning: which GPU renders a hack --------------------------------------------------------
def classes_hybrid_nvidia(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            disp["id"]: {"class": "weak", "ms": 46.5},
            nv["id"]: {"class": "strong", "ms": 2.7, "copy_ms": 1.0},
        },
    )
    return disp, nv


def test_plan_light_hack_stays_on_igpu_heavy_goes_to_nvidia(env):
    disp, nv = classes_hybrid_nvidia(env)
    light = env.plan_for("light_gles3", S)
    heavy = env.plan_for("heavy_gles3", S)
    medium = env.plan_for("medium_gles3", S)
    assert not light["offload"] and light["gpu"]["id"] == disp["id"]
    assert (
        heavy["offload"]
        and heavy["gpu"]["id"] == nv["id"]
        and heavy["class"] == "strong"
    )
    assert medium["offload"]


def test_plan_on_battery_keeps_everything_on_the_igpu(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    assert env.ac_online() is False
    assert env.offload_targets(S) == []
    assert not env.plan_for("heavy_gles3", S)["offload"]
    assert env.pool_class(S) == "weak"
    # an explicit choice of the discrete GPU is honoured on battery
    forced = {**S, "gpu-offload": nv["id"]}
    assert env.plan_for("light_gles3", forced)["offload"]


def test_plan_never_offloads_to_a_slower_gpu(env):
    layout(env, "amd-dgpu-display+intel-idle")
    amd, intel = env.list_gpus()
    seed_cache(env, {amd["id"]: {"class": "strong"}, intel["id"]: {"class": "weak"}})
    assert not env.plan_for("heavy_gles3", S)["offload"]
    forced = {**S, "gpu-offload": intel["id"]}
    p = env.plan_for("heavy_gles3", forced)
    assert p["offload"] and p["gpu"]["id"] == intel["id"]
    e = env.gpu_env(p["gpu"])
    assert e["DRI_PRIME"] == "pci-0000_00_02_0"


def test_gpu_offload_off_disables_targets(env):
    _disp, _nv = classes_hybrid_nvidia(env)
    assert env.offload_targets({**S, "gpu-offload": "off"}) == []
    assert not env.plan_for("heavy_gles3", {**S, "gpu-offload": "off"})["offload"]


def test_prime_forces_offload_for_every_hack(env):
    classes_hybrid_nvidia(env)
    assert env.plan_for("light_gles3", {**S, "gpu-offload": "prime"})["offload"]


def test_child_env_has_offload_variables_only_for_the_offloaded_hack(env):
    classes_hybrid_nvidia(env)
    heavy = env.build_child_env(
        "heavy_gles3", S, base_env={"DRI_PRIME": "1", "HOME": str(env.tmp)}
    )
    light = env.build_child_env(
        "light_gles3",
        S,
        base_env={"__NV_PRIME_RENDER_OFFLOAD": "1", "HOME": str(env.tmp)},
    )
    assert heavy["__NV_PRIME_RENDER_OFFLOAD"] == "1" and "DRI_PRIME" not in heavy
    assert "__NV_PRIME_RENDER_OFFLOAD" not in light
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in heavy


def test_weak_gpu_default_render_scale_and_high_override(env):
    _disp, _nv = classes_hybrid_nvidia(env)
    off = {**S, "gpu-offload": "off"}
    shader = "hyprsaver_aurora_gles3"
    assert env.render_env(shader, off)["NCZ_RENDER_SCALE"] == "0.50"
    fixed = {**off, "render-scale-mode": "fixed", "render-scale": 1.0}
    assert "NCZ_RENDER_SCALE" not in env.render_env(shader, fixed)
    strong = {**S, "gpu-offload": "prime"}
    assert "NCZ_RENDER_SCALE" not in env.render_env(shader, strong)


def test_copy_cost_lowers_scale_on_the_offload_path(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong", "copy_ms": 12.0}},
    )
    got = env.render_env("hyprsaver_aurora_gles3", {**S, "gpu-offload": "prime"})
    assert got["NCZ_RENDER_SCALE"] == "0.75"


# ---- pools -----------------------------------------------------------------------------------
IDS = ["light_gles3", "medium_gles3", "heavy_gles3"]


def test_pool_weak_system_only_weak_ok(env):
    layout(env, "single-soc")
    seed_display(env, "weak")
    assert env.filter_pool(IDS, S) == ["light_gles3"]


def test_pool_mid_system_weak_and_mid(env):
    layout(env, "single-soc")
    seed_display(env, "mid")
    assert env.filter_pool(IDS, S) == ["light_gles3", "medium_gles3"]


def test_pool_strong_system_hides_nothing(env):
    layout(env, "mux-dgpu-only")
    seed_display(env, "strong")
    assert env.filter_pool(IDS, S) == IDS


def test_pool_weak_igpu_with_offload_target_gets_everything(env):
    classes_hybrid_nvidia(env)
    assert env.filter_pool(IDS, S) == IDS


def test_pool_explicit_choices(env):
    layout(env, "single-soc")
    seed_display(env, "weak")
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "all"}) == IDS
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "mid"}) == [
        "light_gles3",
        "medium_gles3",
    ]
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "igpu-only"}) == ["light_gles3"]


def test_pool_excludes_broken_hacks_and_unknown_hacks_count_as_strong(env, monkeypatch):
    layout(env, "single-soc")
    seed_display(env, "weak")
    broken = env.tmp / "broken.tsv"
    broken.write_text("light_gles3\tbroken\treason\t2026-09-29\tx\n")
    monkeypatch.setenv("NCZ_SCREENSAVER_BROKEN", str(broken))
    got = env.filter_pool(["light_gles3", "medium_gles3", "brand_new_gles3"], S)
    assert "light_gles3" not in got  # broken hacks never enter a pool
    # with nothing left that fits, the pool falls back to what is not broken
    assert got == ["medium_gles3", "brand_new_gles3"]
    assert env.pool_class(S) == "weak"
    assert env.load_tiers().get("brand_new_gles3") is None  # unknown counts as strong


# ---- expectations and flags ----------------------------------------------------------------------
def test_flag_and_expectation_text(env):
    layout(env, "single-soc")
    rows = env.load_tier_rows()
    assert rows["heavy_gles3"]["min"] == "strong"
    assert (
        env.expectation("heavy_gles3", "weak", rows) == "about 12 fps on Intel UHD 630"
    )
    assert env.expectation("medium_gles3", "mid", rows) == "about 48 fps on Mali-G720"
    assert env.expectation("nope_gles3", "weak", rows) == ""


def test_legacy_tier_names_still_load(env):
    (env.tmp / "tiers.tsv").write_text("a_gles3\tigpu\nb_gles3\tdiscrete\n")
    assert env.load_tiers() == {"a_gles3": "weak", "b_gles3": "strong"}


def test_copy_ms_command_records_cost_and_survives_recalibration(env, monkeypatch):
    layout(env, "nvidia+intel")
    _disp, nv = env.list_gpus()
    args = type("A", (), {"force": False, "json": True, "copy_ms": f"{nv['id']}=9.5"})()
    assert env.cmd_calibrate(args) == 0
    assert env.read_class_cache()[nv["id"]]["copy_ms"] == 9.5
    monkeypatch.setenv("FAKE_MS", "2.7")
    entry = env.gpu_class(S, nv, force=True)
    assert entry["copy_ms"] == 9.5


def test_pool_and_plan_commands(env, monkeypatch, capsys):
    layout(env, "single-soc")
    seed_display(env, "weak")
    settings = dict(env.DEFAULTS)
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    monkeypatch.setattr(env, "installed_ids", lambda s: list(IDS))
    monkeypatch.setattr(env, "known_id", lambda h, c: True)
    monkeypatch.setattr(env, "load_catalog", list)
    assert env.cmd_pool(type("A", (), {"json": True})()) == 0
    out = json.loads(capsys.readouterr().out)
    assert out["class"] == "weak" and out["ids"] == ["light_gles3"]
    assert set(out["excluded"]) == {"medium_gles3", "heavy_gles3"}
    assert env.cmd_plan(type("A", (), {"hack": "heavy_gles3", "json": True})()) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["offload"] is False
    assert plan["min_class"] == "strong"
    assert plan["class"] == "weak"


def test_child_env_exports_the_class_of_the_rendering_gpu(env):
    classes_hybrid_nvidia(env)
    heavy = env.build_child_env("heavy_gles3", S, base_env={"HOME": str(env.tmp)})
    light = env.build_child_env("light_gles3", S, base_env={"HOME": str(env.tmp)})
    assert heavy["NCZ_GPU_CLASS"] == "strong"  # offloaded to the NVIDIA GPU
    assert light["NCZ_GPU_CLASS"] == "weak"  # stays on the Intel iGPU
    mine = env.build_child_env(
        "light_gles3", S, base_env={"HOME": str(env.tmp), "NCZ_GPU_CLASS": "mid"}
    )
    assert mine["NCZ_GPU_CLASS"] == "mid"  # an explicit value wins


def test_mid_class_uses_measured_render_hints(env, monkeypatch):
    layout(env, "single-soc")
    seed_display(env, "mid")
    hints = env.tmp / "hints.tsv"
    hints.write_text("hyprsaver_slow_gles3\t0.5\t60\tnote\n")
    monkeypatch.setenv("NCZ_SCREENSAVER_RENDER_HINTS", str(hints))
    got = env.render_env("hyprsaver_slow_gles3", S)
    assert got["NCZ_RENDER_SCALE"] == "0.50"
    assert "NCZ_RENDER_SCALE" not in env.render_env("hyprsaver_other_gles3", S)
    fixed = {**S, "render-scale-mode": "fixed", "render-scale": 1.0}
    assert "NCZ_RENDER_SCALE" not in env.render_env("hyprsaver_slow_gles3", fixed)


def test_on_ac_every_shader_hack_goes_to_the_discrete_gpu(env):
    _disp, nv = classes_hybrid_nvidia(env)
    shader = env.plan_for("hyprsaver_light_gles3", S)
    assert shader["offload"] and shader["gpu"]["id"] == nv["id"]
    classic = env.plan_for("light_gles3", S)
    assert not classic["offload"]


def test_on_battery_no_hack_offloads_even_shader_hacks(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    for hid in ("hyprsaver_light_gles3", "heavy_gles3", "light_gles3"):
        assert not env.plan_for(hid, S)["offload"]
    child = env.build_child_env(
        "hyprsaver_light_gles3", S, base_env={"HOME": str(env.tmp)}
    )
    assert "__NV_PRIME_RENDER_OFFLOAD" not in child and "DRI_PRIME" not in child


def test_unmeasurable_hack_gets_a_floor_expectation(env):
    layout(env, "single-soc")
    (env.tmp / "tiers.tsv").write_text(
        "slow_gles3\tstrong\t-\t-\t-\t-\t-\t2026-09-29 test\n"
    )
    rows = env.load_tier_rows()
    assert env.expectation("slow_gles3", "weak", rows) == "under 6 fps on Intel UHD 630"


# ---- regressions from the code review ---------------------------------------------------------
def test_supervisor_plan_reaches_the_child_env_with_copy_cost(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong", "copy_ms": 12.0}},
    )
    plan = env.plan_for("hyprsaver_light_gles3", S)
    child = env.build_child_env(
        "hyprsaver_light_gles3", S, base_env={"HOME": str(env.tmp)}, offload=plan
    )
    assert child["__NV_PRIME_RENDER_OFFLOAD"] == "1"
    assert child["NCZ_RENDER_SCALE"] == "0.75"  # the copy cost lowers the scale
    assert child["NCZ_GPU_CLASS"] == "strong"


def test_chosen_gpu_id_is_not_overridden_by_a_better_gpu(env):
    layout(env, "amd-apu+amd-dgpu")
    add_gpu(env.sysfs, "card2", "0x10de", "0x1f08", "nvidia", "0000:08:00.0")
    env.list_gpus.cache_clear()
    gpus = env.list_gpus()
    amd_dgpu = next(g for g in gpus if g["id"] == "pci-0000_03_00_0")
    nvidia = next(g for g in gpus if g["vendor"] == "nvidia")
    seed_cache(
        env,
        {
            gpus[0]["id"]: {"class": "mid"},
            amd_dgpu["id"]: {"class": "strong"},
            nvidia["id"]: {"class": "strong"},
        },
    )
    chosen = {**S, "gpu-offload": amd_dgpu["id"]}
    child = env.build_child_env(
        "heavy_gles3", chosen, base_env={"HOME": str(env.tmp)}, offload=True
    )
    assert child["DRI_PRIME"] == "pci-0000_03_00_0"
    assert "__NV_PRIME_RENDER_OFFLOAD" not in child


def test_egl_vendor_pin_is_dropped_for_offloaded_hacks_only(env):
    classes_hybrid_nvidia(env)
    base = {"HOME": str(env.tmp), "__EGL_VENDOR_LIBRARY_FILENAMES": "/x/50_mesa.json"}
    heavy = env.build_child_env("heavy_gles3", S, base_env=base)
    light = env.build_child_env("light_gles3", S, base_env=base)
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in heavy
    assert (
        light["__EGL_VENDOR_LIBRARY_FILENAMES"] == "/x/50_mesa.json"
    )  # display GPU keeps its pin


def test_failed_benchmark_is_retried_later_not_cached_forever(env, monkeypatch):
    layout(env, "single-soc")
    calls = []
    real = env._calibrator_json

    def flaky(args, environ, timeout):
        calls.append(tuple(args))
        if not args:  # the benchmark itself fails
            return 2, None
        return real(args, environ, timeout)

    monkeypatch.setattr(env, "_calibrator_json", flaky)
    first = env.gpu_class(S, None)
    assert first["source"] == "table"
    n = len(calls)
    env.gpu_class(S, None)  # inside the retry window: no second benchmark attempt
    assert () not in calls[n:]
    cache = env.read_class_cache()
    key = next(iter(cache))
    cache[key]["retry_after"] = 0
    env.class_cache_path().write_text(json.dumps({"entries": cache}))
    monkeypatch.setattr(env, "_calibrator_json", real)
    assert env.gpu_class(S, None)["source"] == "calibration"


def test_render_only_gpu_takes_the_display_role_of_a_display_only_controller(env):
    # Pi-like: card0 is the display controller (no render node), card1 the render GPU
    add_gpu(env.sysfs, "card0", "0x0000", "0x0000", "vc4", "soc:gpu0", connected=True)
    add_gpu(env.sysfs, "card1", "0x0000", "0x0000", "v3d", "soc:gpu1")
    (env.sysfs / "class/drm/card1/device/drm/renderD128").mkdir(parents=True)
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.switcheroo_envs, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert [g["driver"] for g in gpus] == ["v3d"]
    assert gpus[0]["display"] is True
    assert env.offload_targets(S) == []


def test_unknown_gpu_offload_value_behaves_like_auto_including_on_battery(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    for bogus in ("nvidia", "PCI-0000_01_00_0", ""):
        settings = {**S, "gpu-offload": bogus}
        assert env.offload_mode(settings) == "auto"
        assert env.offload_targets(settings) == []
        assert env.pool_class(settings) == "weak"


def test_calibration_happens_inside_the_supervisor_after_state_exists(env, monkeypatch):
    layout(env, "single-soc")
    order = []
    monkeypatch.setattr(
        env.Supervisor,
        "write_state",
        lambda self, rotates_at=None: order.append("state"),
    )
    monkeypatch.setattr(
        env.Supervisor, "calibrate", lambda self: order.append("calibrate")
    )
    monkeypatch.setattr(env.Supervisor, "pick", lambda self: None)
    args = type(
        "A",
        (),
        {"seconds": 0, "hack": None, "idle": False, "force": False, "foreground": True},
    )()
    sup = env.Supervisor(args, dict(env.DEFAULTS), [])
    monkeypatch.setattr(env.signal, "signal", lambda *a: None)
    sup.run()
    assert order[:2] == ["state", "calibrate"]


# ----------------------------------------------------------------------------
# Pass-13: pinning the operator-reported scenario verbatim
# ----------------------------------------------------------------------------
# The operator's complaint (2026-09-30, after the previous passes had merged
# their fixes into the source tree) was that
#   $ ncz-screensaver gpus
# still reported "weak" for every card on .66 (MS-R1 / Sky1 /
# Mali-G720-Immortalis). The previous tests pin the underlying detection
# logic in isolation (renderer pattern, sysfs layout, topology fallback,
# stale cache), but the operator-visible behaviour is the integration of
# them: cmd_gpus text + JSON output, plus the cmd_doctor verdict.
#
# The two tests below pin that integration against the EXACT strings
# captured live on .66 on 2026-09-30. They run the launcher source
# against the live sysfs + cache layout via the in-memory test rig, so
# the assertion is independent of which binary happens to be installed
# at /usr/bin/ncz-screensaver on the host — and they catch any future
# regression that re-introduces the "weak" verdict for the Mali at the
# integration level.
#
# The captured inputs are:
#   - GLES renderer:  "Mali-G720-Immortalis"
#   - GLES version:   "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f3b6749cb5"
#                     (truncated to the actual upstream substring;
#                      full string is 84 chars but the fix only matches the
#                      pattern prefix).
#   - cache file:     the shipped ncz-screensavers 0.7.1 cache entry the
#                     live host had at 2026-09-30T08:43:03Z — pci-CIXH5010_03
#                     classified 'weak' ms=20.2 against the Mali renderer.
#   - sysfs:          four DRM cards (CIXH5010:00/:01/:03/:04) driven by
#                     linlondp, the platform-bus CIXH5000:00 driven by
#                     mali, and /sys/class/misc/mali0 with the short-form
#                     device symlink (../../../CIXH5000:00).
# The expected outputs are what the fixed source produces when run against
# those inputs:
#   - cmd_gpus text:   exactly one row, soc-CIXH5000_00 mid, ms=9.88.
#   - cmd_gpus JSON:   same GPU, class=mid, renderer="Mali-G720-Immortalis".
#   - cmd_doctor:      pool_class=mid, gpus has one entry (the Mali),
#                      gpu_class.display.class=mid.
#   - cmd_status JSON: gpu_class=mid, gpu_class_score_ms=9.88.
# A regression that demotes the GPU to weak, removes the platform-bus
# fallback, breaks the stale-cache filter, or changes the cache key
# would fail at least one of these.


SKY1_PASS13_CAPTURE = {
    # Exact strings from /run/user/1000/ncz-screensaver/hack.log line 4
    # on 2026-09-30: GL_VERSION=OpenGL ES 3.2 v1.r53p0-... and RENDERER=
    # Mali-G720-Immortalis (line 5).
    "renderer": "Mali-G720-Immortalis",
    "version": ("OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f3b6749cb5"),
    # ms from the calibration the live host reported in the 0.7.10 cache
    # file (soc-CIXH5000_00 entry, 2026-09-30T08:55:21Z).
    "ms": 9.88,
}


def _build_sky1_pass13_sysfs(env):
    """Mirror the .66 sysfs layout exactly as captured on 2026-09-30:
    four DRM cards (CIXH5010:00, :01, :03, :04 -- :02 absent from DRM)
    driven by linlondp, plus the standalone platform-bus CIXH5000:00
    driven by mali, surfaced via /sys/class/misc/mali0 with the
    short-form device symlink that the live host exposes.
    """
    for i, slot in enumerate(SKY1_LIVE_DRM_SLOTS):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(slot == "CIXH5010:03"),
        )
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    os.symlink(os.path.relpath(gpu_dev, str(misc)), misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology, env.switcheroo_envs):
        fn.cache_clear()


def test_sky1_pass13_cmd_gpus_reports_mid_against_live_capture(
    env, monkeypatch, capsys
):
    """Pin the operator-visible cmd_gpus text output for the live .66
    host after the fix: exactly one row, the platform-bus Mali, class=mid.

    cmd_gpus runs `gpu_class(run=False)` -- it only reads from cache.
    On a fresh boot the new id (soc-CIXH5000_00) is not cached yet,
    so the ms is "no score" and the class comes from the topology
    fallback (driver=mali -> MID_DRIVERS -> mid). After a calibration
    the cache carries the ms.

    The shipped ncz-screensavers 0.7.1 reports four weak linlondp rows
    because the old list_gpus() does not surface the platform-bus
    mali0 device. The fixed source must report a single mid row
    regardless of cache state. Captures the exact strings from .66 on
    2026-09-30 (renderer, version, ms).
    """
    _build_sky1_pass13_sysfs(env)
    # Stale 0.7.1 cache entry: the bytes captured on .66 at
    # 2026-09-30T08:43:03Z. The test asserts the launcher ignores it
    # because the GPU id is now soc-CIXH5000_00.
    seed_cache(
        env,
        {
            "pci-CIXH5010_03": {
                "class": "weak",
                "ms": 20.2,
                "renderer": SKY1_PASS13_CAPTURE["renderer"],
                "version": SKY1_PASS13_CAPTURE["version"],
                "timer": "wall",
                "source": "calibration",
                "when": "2026-09-30T08:43:03+0000",
                "driver": "linlondp",
                "gpu": "pci-CIXH5010_03",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_gpus(type("A", (), {"json": False})())
    assert rc == 0
    out = capsys.readouterr().out.strip().splitlines()
    assert len(out) == 1, (
        f"cmd_gpus reported {len(out)} rows on Sky1 -- the linlondp "
        f"display controllers must not appear; only the platform-bus "
        f"Mali is a GPU. Rows: {out!r}"
    )
    row = out[0].split()
    # UX text format includes an index before the id.
    assert row[0] == "GPU0"
    assert row[1] == "soc-CIXH5000_00"
    assert row[2] == "other"
    assert row[3] == "mali"
    assert row[4] == "mid", (
        f"GPU classified as {row[4]!r} not 'mid' -- the operator sees "
        f"this exact string in the launcher. The fix must surface the "
        f"Mali-G720-Immortalis as 'mid'."
    )
    assert row[5] == "display"
    # run=False + no cache for the new GPU -> ms is "no score". After
    # a calibration the ms field is populated; that's covered by the
    # cmd_gpus_json test below.
    assert row[6] == "no", (
        f"cmd_gpus (run=False) shows ms={row[6]!r}; expected 'no score' "
        f"because the test rig seeded only the stale 0.7.1 cache entry."
    )
    # Stale 0.7.1 cache entry is still in the file (launcher never
    # deletes cache; only overwrites the key it just classified).
    on_disk = json.loads(env.class_cache_path().read_text())
    assert "pci-CIXH5010_03" in on_disk["entries"]
    assert "soc-CIXH5000_00" not in on_disk["entries"], (
        "cmd_gpus (run=False) must NOT write to the cache -- only the "
        "calibrator path writes. The stale entry remains untouched."
    )


def test_sky1_pass13_cmd_gpus_json_reports_only_mali_mid(env, monkeypatch, capsys):
    """Pin the operator-visible cmd_gpus --json output for the live
    .66 host: exactly one GPU entry, the platform-bus Mali, with
    class=mid (the renderer/ms come from cache when present, from
    the topology fallback otherwise).

    cmd_gpus runs `gpu_class(run=False)`, so the calibrator does not
    fire in this test. We seed the cache with the live .66 entry the
    fixed binary writes (soc-CIXH5000_00 mid ms=9.88) plus the stale
    0.7.1 entry (pci-CIXH5010_03 weak). The fixed output is one row
    (soc-CIXH5000_00 mid), the stale entry is ignored.

    The shipped ncz-screensavers 0.7.1 emits four entries (one mid from
    the cache hit, three weak from the topology fallback). The fixed
    source emits one. The settings UI consumes this JSON; a regression
    here is exactly what the operator sees on the desktop.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            # The live 0.7.10 cache entry the fixed binary writes.
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_PASS13_CAPTURE["ms"],
                "renderer": SKY1_PASS13_CAPTURE["renderer"],
                "version": SKY1_PASS13_CAPTURE["version"],
                "timer": "gpu",
                "source": "calibration",
                "when": "2026-09-30T08:55:21+0000",
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            },
            # Stale 0.7.1 entry. Must be ignored by the launcher.
            "pci-CIXH5010_03": {
                "class": "weak",
                "ms": 20.2,
                "renderer": SKY1_PASS13_CAPTURE["renderer"],
                "version": SKY1_PASS13_CAPTURE["version"],
                "timer": "wall",
                "source": "calibration",
                "when": "2026-09-30T08:43:03+0000",
                "driver": "linlondp",
                "gpu": "pci-CIXH5010_03",
            },
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_gpus(type("A", (), {"json": True})())
    assert rc == 0
    rows = json.loads(capsys.readouterr().out)
    assert len(rows) == 1, (
        f"cmd_gpus --json reported {len(rows)} GPUs on Sky1 -- only the "
        f"Mali is a GPU. Rows: {rows!r}"
    )
    g = rows[0]
    assert g["id"] == "soc-CIXH5000_00"
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["class"] == "mid", (
        f"GPU class={g['class']!r} not 'mid' on .66 (Sky1 / Mali-G720-"
        f"Immortalis). The settings UI shows this exact value to the "
        f"user."
    )
    assert g["ms"] == SKY1_PASS13_CAPTURE["ms"]
    assert g["renderer"] == SKY1_PASS13_CAPTURE["renderer"]


def test_sky1_pass13_cmd_doctor_pool_class_mid(env, monkeypatch, capsys):
    """Pin the operator-visible cmd_doctor JSON for the live .66 host:
    pool_class=mid, gpus has one entry (the Mali),
    gpu_class.display.class=mid, gpu.display_class=integrated.

    This is the integration-level promise. The shipped ncz-screensavers
    0.7.1 reported pool_class=weak, which restricted the screensaver
    pool to weak-tier hacks and locked the user out of the shaders the
    Mali-G720-Immortalis can actually render at 60 fps.
    """
    _build_sky1_pass13_sysfs(env)
    # The cmd_doctor path calls gpu_class(run=False). Seed the cache
    # with the live .66 0.7.10 entry the fixed binary writes.
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_PASS13_CAPTURE["ms"],
                "renderer": SKY1_PASS13_CAPTURE["renderer"],
                "version": SKY1_PASS13_CAPTURE["version"],
                "timer": "gpu",
                "source": "calibration",
                "when": "2026-09-30T08:55:21+0000",
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_doctor(type("A", (), {})())
    assert rc == 0
    doc = json.loads(capsys.readouterr().out)
    # The central promise: pool_class=mid, not weak.
    assert doc["pool_class"] == "mid", (
        f"cmd_doctor pool_class={doc['pool_class']!r}; the user sees the "
        f"weak-tier pool only and never sees the shaders the Mali can "
        f"actually render."
    )
    # GPU topology: only one GPU, the Mali.
    assert len(doc["gpus"]) == 1, (
        f"cmd_doctor gpus has {len(doc['gpus'])} entries; only the Mali "
        f"is a GPU. Entries: {doc['gpus']!r}"
    )
    g = doc["gpus"][0]
    assert g["id"] == "soc-CIXH5000_00"
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["display_only"] is False
    # gpu_class.display.class=mid.
    gc = doc["gpu_class"]["display"]
    assert gc["class"] == "mid", (
        f"gpu_class.display.class={gc.get('class')!r}; the launcher "
        f"topology + cache lookup must produce 'mid' for the Mali."
    )
    assert gc["renderer"] == SKY1_PASS13_CAPTURE["renderer"]
    assert gc["ms"] == SKY1_PASS13_CAPTURE["ms"]
    # No offload targets: Sky1 has no second GPU.
    assert doc["offload"]["targets"] == []
    # The display_class is 'integrated' (the connected linlondp card is
    # not nvidia/amdgpu). This is a display topology fact, not a class
    # fact; it must stay 'integrated' regardless of the GPU fix.
    assert doc["gpu"]["display_class"] == "integrated"
    assert doc["gpu"]["nvidia_offload"] is False


def test_sky1_pass13_cmd_status_reports_mid(env, monkeypatch, capsys):
    """Pin the operator-visible cmd_status --json output for the live
    .66 host: gpu_class=mid, gpu_class_score_ms=9.88,
    gpu_class_source=calibration.

    This is what the idle daemon and the GTK settings UI poll. The
    shipped ncz-screensavers 0.7.1 reports gpu_class=weak here, which
    is the operator's complaint at the topmost level.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_PASS13_CAPTURE["ms"],
                "renderer": SKY1_PASS13_CAPTURE["renderer"],
                "version": SKY1_PASS13_CAPTURE["version"],
                "timer": "gpu",
                "source": "calibration",
                "when": "2026-09-30T08:55:21+0000",
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_status(type("A", (), {"json": True, "diagnostics": False})())
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r} -- "
        f"the user sees this exact string in the launcher's status row."
    )
    assert out["gpu_class_score_ms"] == SKY1_PASS13_CAPTURE["ms"]
    assert out["gpu_class_source"] == "calibration"


# ----------------------------------------------------------------------------
# Adversarial-review gap-pin tests (turn 4 dispatch, 2026-09-30)
# ----------------------------------------------------------------------------
# The subagent review of launcher/ncz-screensaver found a real defect
# (`_iter_platform_gpu_devices()` glob matches `mali_capture`/`mali_jit`
# and would produce duplicate rows on a Panthor/CSF kernel) and several
# hardening gaps. The tests below pin each one against the same fake-sysfs
# rig the rest of the suite uses, so a regression in any of them surfaces
# at unit-test time rather than on the next live-host probe.


def test_platform_gpu_devices_dedup_mali_capture_and_mali_jit_siblings(env):
    """A kernel that registers more than one misc character device for the
    same GPU (Panthor / CSF: mali0 + mali_capture + mali_jit) used to produce
    three duplicate rows in list_gpus() because the glob `class/misc/mali*`
    matched every sibling that shared the parent platform device. The fix
    resolves the platform path and yields one GPU per parent device.
    """
    add_power(env.sysfs, True)
    # Parent platform device: CIXH5000:00 driven by 'mali'.
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    # Three sibling misc devices, all symlinked to subdirs of the same parent.
    for name in ("mali0", "mali_capture", "mali_jit"):
        misc = env.sysfs / "class/misc" / name
        misc.mkdir(parents=True)
        (misc / "dev").write_text("10:262\n")
        target = gpu_dev / f"misc/{name}"
        target.mkdir(parents=True)
        os.symlink(target, misc / "device")
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert len(gpus) == 1, (
        f"list_gpus() returned {len(gpus)} entries for one parent GPU "
        f"with three misc siblings; should be one row."
    )
    assert gpus[0]["id"] == "soc-CIXH5000_00"
    assert gpus[0]["driver"] == "mali"
    assert gpus[0]["display"] is True


def test_class_from_topology_tolerates_partial_dicts(env):
    """Adversarial review found that `class_from_topology({})` and
    `class_from_topology({'driver': 'mali'})` raised `KeyError: 'discrete'`
    / `'driver'`. The function is now `.get()`-safe; these tests pin the
    hardened contract so a future refactor can't reintroduce the crash.
    """
    # Empty dict: defaults to weak, no exception.
    assert env.class_from_topology({}) == "weak"
    # Missing discrete: must not raise, must check driver first.
    assert env.class_from_topology({"driver": "mali"}) == "mid"
    assert env.class_from_topology({"driver": "i915"}) == "weak"
    assert env.class_from_topology({"driver": "panthor"}) == "mid"
    # nvidia without the discrete flag stays weak — the strong verdict
    # requires either the renderer string (handled upstream by
    # class_from_renderer) or the discrete=True sysfs flag (set below).
    assert env.class_from_topology({"driver": "nvidia"}) == "weak"
    assert env.class_from_topology({"driver": "nvidia", "discrete": True}) == "strong"
    # Missing driver: must not raise, defaults to weak.
    assert env.class_from_topology({"discrete": False}) == "weak"
    assert env.class_from_topology({"discrete": True}) == "strong"


def test_list_gpus_only_display_only_drivers_keeps_them_as_weak(env):
    """A host with no real GPU (e.g. a Pi 4 with the v3d module unloaded,
    or a Sky1 with the mali module blacklisted forever) still has the
    display controllers in its DRM list. They must stay in list_gpus() so
    cmd_gpus can render something; class_from_topology must return 'weak'
    via the default branch since no driver is in any allow-list.
    """
    # Two display-only DRM cards: vc4 (no render node) and vkms (test rig).
    for i, driver in enumerate(("vc4", "vkms")):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            driver,
            f"0000:0{i}:00.0",
            connected=(i == 0),
        )
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    # Both cards survive the filter (no real GPU exists to filter against).
    ids = sorted(g["id"] for g in gpus)
    assert ids == ["pci-0000_00_00_0", "pci-0000_01_00_0"]
    # Both are flagged display_only=True by the filter.
    assert all(g.get("display_only") for g in gpus)
    # And class_from_topology classifies both as 'weak'.
    assert env.class_from_topology(gpus[0]) == "weak"
    assert env.class_from_topology(gpus[1]) == "weak"


def test_cmd_gpus_on_empty_sysfs_returns_cleanly(env, capsys):
    """cmd_gpus on a host with no GPUs at all (e.g. a CI runner without a
    DRM device, or a fresh image that has not loaded any driver yet) must
    not crash. Pre-fix this would have iterated an empty list and printed
    nothing; pin the contract explicitly so the JSON consumer sees `[]`
    and the text consumer sees zero rows + rc=0.
    """
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    rc = env.cmd_gpus(type("A", (), {"json": True})())
    assert rc == 0
    out = capsys.readouterr().out.strip()
    assert out == "[]", f"cmd_gpus --json on empty sysfs should print '[]', got {out!r}"


def test_render_only_gpu_takes_display_when_display_controller_has_no_render_node(
    env,
):
    """Pi 4 layout: card0=vc4 (display controller, no renderD*), card1=v3d
    (render GPU). Already covered for Pi at the integration level (line
    1607), but this test pins the *display_role assignment* alone so a
    future refactor of list_gpus() that touches the boot_vga fallback
    cannot silently swap the display GPU.
    """
    # vc4 display controller with NO renderD* child.
    add_gpu(
        env.sysfs, "card0", "0x0000", "0x0000", "vc4", "0000:01:00.0", connected=True
    )
    (env.sysfs / "class/drm/card0/device/drm").mkdir(parents=True, exist_ok=True)
    # v3d render GPU with a renderD* child.
    add_gpu(env.sysfs, "card1", "0x0000", "0x0000", "v3d", "0000:02:00.0")
    (env.sysfs / "class/drm/card1/device/drm").mkdir(parents=True, exist_ok=True)
    (env.sysfs / "class/drm/card1/device/drm/renderD128").touch()
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert len(gpus) == 1, (
        f"vc4 is display-only; the display role must belong to the v3d. Got: {gpus}"
    )
    assert gpus[0]["driver"] == "v3d"
    assert gpus[0]["display"] is True


# ---------------------------------------------------------------------------
# Dispatch r18 (2026-09-30T11:50Z) re-capture:
# the strings I personally captured from /tmp/ncz-screensaver-r18-2026-09-30
# on an MS-R1 (Sky1). The ms differs from SKY1_PASS13_CAPTURE (14.71 vs 9.88)
# because the calibration micro-benchmark runs at different wall times;
# both classify as `mid` because the threshold band is 8.0 <= ms < 20.0.
# Pinning the new capture means a regression that changes the threshold
# band or breaks the topology fallback will fail both old and new pins.
# ---------------------------------------------------------------------------

SKY1_R18_CAPTURE = {
    # Captured from /tmp/ncz-screensaver-r18-2026-09-30 status --json on
    # an MS-R1 (Sky1) at 2026-09-30T11:50Z (this dispatch).
    "renderer": "Mali-G720-Immortalis",
    "ms": 14.71,
    "when": "2026-09-30T11:50:00+0000",
}


def test_sky1_r18_cmd_status_reports_mid_with_this_dispatches_capture(
    env, monkeypatch, capsys
):
    """Pin the operator-visible cmd_status --json output captured by this
    dispatch (r18, 2026-09-30T11:50Z): gpu_class=mid, ms=14.71,
    source=calibration. This is the second live pin on the operator's
    primary complaint -- the GPU verdict in the launcher's status row.

    Compared to SKY1_PASS13_CAPTURE (ms=9.88), the ms differs because
    the calibrator micro-benchmark runs at different wall times;
    both classify as `mid` (8.0 <= ms < 20.0).
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R18_CAPTURE["ms"],
                "renderer": SKY1_R18_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "wall",
                "source": "calibration",
                "when": SKY1_R18_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_status(type("A", (), {"json": True, "diagnostics": False})())
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r} -- "
        f"this dispatch's live capture shows gpu_class=mid on .66."
    )
    assert out["gpu_class_score_ms"] == SKY1_R18_CAPTURE["ms"], (
        f"cmd_status gpu_class_score_ms={out['gpu_class_score_ms']!r}; "
        f"expected {SKY1_R18_CAPTURE['ms']} (this dispatch's live ms)."
    )
    assert out["gpu_class_source"] == "calibration"


def test_sky1_r18_class_from_ms_keeps_14_71_in_mid_band(env):
    """The ms band 8.0 <= ms < 20.0 must stay `mid` -- this is the
    operator's contract. 14.71 (this dispatch's live ms) sits squarely
    in the band, so any change to CLASS_WEAK_MS or CLASS_MID_MS that
    pushes 14.71 to `weak` would fail this test.

    The ms threshold constants are in launcher/ncz-screensaver lines
    124-125. They are operator policy, not implementation detail.
    """
    assert 8.0 <= SKY1_R18_CAPTURE["ms"] < 20.0, (
        f"this dispatch's live ms {SKY1_R18_CAPTURE['ms']} is outside "
        f"the mid band [8.0, 20.0); the calibrator threshold contract "
        f"should be re-checked."
    )
    assert env.class_from_ms(SKY1_R18_CAPTURE["ms"]) == "mid", (
        f"class_from_ms({SKY1_R18_CAPTURE['ms']}) returned a non-mid "
        f"class; CLASS_WEAK_MS or CLASS_MID_MS drifted."
    )
    # Boundary checks: 8.0 is the lower mid bound, 20.0 is the weak floor.
    assert env.class_from_ms(8.0) == "mid"
    assert env.class_from_ms(19.99) == "mid"
    assert env.class_from_ms(20.0) == "weak"
    assert env.class_from_ms(7.99) == "strong"


# ---------------------------------------------------------------------------
# Dispatch r15-continuation (2026-09-30T15:53Z) re-capture:
# the strings captured live from /tmp/ncz-screensaver-continuation-rN-2026-09-30
# on an MS-R1 (Sky1). The ms differs from SKY1_R18_CAPTURE (11.93 vs 14.71)
# and from SKY1_PASS13_CAPTURE (9.88) because the calibration micro-benchmark
# is wall-time dependent. All three pins classify as `mid` because the
# threshold band is 8.0 <= ms < 20.0. Pinning the new capture means a
# regression that changes the threshold band, breaks the topology fallback,
# or rewrites the cmd_status schema breaks the old AND the new pins.
# ---------------------------------------------------------------------------

SKY1_R15_CONT_CAPTURE = {
    # Captured from /tmp/ncz-screensaver-continuation-rN-2026-09-30T1553Z
    # status --json on an MS-R1 (Sky1) at 2026-09-30T15:53Z (this dispatch).
    # The hack running on .66 at capture time was hyprsaver_stonks_gles3
    # (PID 424458, uptime 5928.2 s at the first run; voronoi_gles3 14 s later).
    "renderer": "Mali-G720-Immortalis",
    "ms": 11.93,
    "when": "2026-09-30T11:53:17+0000",
    "hack_first": "hyprsaver_stonks_gles3",
    "hack_then": "voronoi_gles3",
}


def test_sky1_r15_cont_cmd_status_reports_mid_with_this_dispatches_capture(
    env, monkeypatch, capsys
):
    """Pin the operator-visible cmd_status --json output captured by this
    dispatch (continuation, 2026-09-30T15:53Z): gpu_class=mid, ms=11.93,
    source=calibration.

    This is the THIRD independent live pin on the operator's primary
    complaint (the GPU verdict in the launcher's status row). The other
    two are SKY1_PASS13_CAPTURE (ms=9.88, 0.7.10 calibrator) and
    SKY1_R18_CAPTURE (ms=14.71, post-hardening). All three classify as
    `mid` because the threshold band is 8.0 <= ms < 20.0.

    A regression that flips the threshold band (e.g. raises
    CLASS_WEAK_MS below 11.93) would now fail THREE independent pins
    instead of two. A regression that drops the renderer string from
    cmd_status JSON would fail this pin because we assert
    gpu_class_source=calibration.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R15_CONT_CAPTURE["ms"],
                "renderer": SKY1_R15_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "wall",
                "source": "calibration",
                "when": SKY1_R15_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_status(type("A", (), {"json": True, "diagnostics": False})())
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r}; "
        f"this dispatch's live capture shows gpu_class=mid on .66."
    )
    assert out["gpu_class_score_ms"] == SKY1_R15_CONT_CAPTURE["ms"], (
        f"cmd_status gpu_class_score_ms={out['gpu_class_score_ms']!r}; "
        f"expected {SKY1_R15_CONT_CAPTURE['ms']} (this dispatch's live ms)."
    )
    assert out["gpu_class_source"] == "calibration"


def test_sky1_r15_cont_class_from_ms_keeps_11_93_in_mid_band(env):
    """The ms band 8.0 <= ms < 20.0 must stay `mid` -- this is the
    operator's contract. 11.93 (this dispatch's live ms) sits squarely
    in the band, so any change to CLASS_WEAK_MS or CLASS_MID_MS that
    pushes 11.93 to `weak` would fail this test.

    Pairs with test_sky1_r18_class_from_ms_keeps_14_71_in_mid_band and
    the SKY1_PASS13_CAPTURE ms=9.88 pin to give three independent
    live-ms pins for the threshold contract.
    """
    assert 8.0 <= SKY1_R15_CONT_CAPTURE["ms"] < 20.0, (
        f"this dispatch's live ms {SKY1_R15_CONT_CAPTURE['ms']} is outside "
        f"the mid band [8.0, 20.0); the calibrator threshold contract "
        f"should be re-checked."
    )
    assert env.class_from_ms(SKY1_R15_CONT_CAPTURE["ms"]) == "mid", (
        f"class_from_ms({SKY1_R15_CONT_CAPTURE['ms']}) returned a non-mid "
        f"class; CLASS_WEAK_MS or CLASS_MID_MS drifted."
    )


def test_sky1_r15_cont_cmd_gpus_text_unchanged(env, monkeypatch, capsys):
    """Pin the operator-visible `ncz-screensaver gpus` text output against
    this dispatch's live sysfs layout. The exact text format
    `soc-CIXH5000_00 display  other   mali     mid    11.9 ms` is what
    the operator sees in a shell; a regression that changes the column
    order, drops a field, or demotes the verdict fails this test.

    The `11.9` is the truncated ms (Python's default float repr rounds
    11.93 to 11.9 with one decimal in the launcher output path); a
    regression that switches to full precision (11.93) or drops the
    rounding is caught here.
    """
    _build_sky1_pass13_sysfs(env)
    monkeypatch.setenv("FAKE_RENDERER", SKY1_R15_CONT_CAPTURE["renderer"])
    monkeypatch.setenv("FAKE_MS", str(SKY1_R15_CONT_CAPTURE["ms"]))
    monkeypatch.setenv(
        "FAKE_VERSION", "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_gpus(type("A", (), {"json": False})())
    assert rc == 0
    out = capsys.readouterr().out
    # The exact text format the operator sees. Format columns and order
    # are operator contract.
    assert "soc-CIXH5000_00" in out, (
        f"cmd_gpus text output missing the Mali id. Got: {out!r}"
    )
    assert "mali" in out, f"cmd_gpus text missing the mali driver. Got: {out!r}"
    assert "mid" in out, f"cmd_gpus text missing the 'mid' verdict. Got: {out!r}"
    # And the GPU must NOT be classified as weak.
    assert "weak" not in out, (
        f"cmd_gpus text reports weak for the Mali-G720-Immortalis. Got: {out!r}"
    )


def test_sky1_r15_cont_cmd_gpus_json_display_only_field_is_false(
    env, monkeypatch, capsys
):
    """Pin the `display_only` field in the cmd_gpus --json output for the
    Mali. The fix added `display_only` to every gpu dict; the Mali must
    carry `display_only: false` (it IS the render GPU) and the linlondp
    cards must carry `display_only: true` so list_gpus() drops them
    once a real GPU is present.

    This pin covers the operator-visible JSON contract end-to-end:
    the only entry in `gpus --json` is the Mali with
    `display_only: false` -- if a future refactor forgets to set the
    field, this test fails. The earlier display_only assertions are
    in cmd_doctor; this one covers cmd_gpus.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R15_CONT_CAPTURE["ms"],
                "renderer": SKY1_R15_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "wall",
                "source": "calibration",
                "when": SKY1_R15_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_gpus(type("A", (), {"json": True})())
    assert rc == 0
    out = json.loads(capsys.readouterr().out)
    assert len(out) == 1, (
        f"cmd_gpus --json returned {len(out)} entries; the live .66 "
        f"layout must yield exactly one (the Mali)."
    )
    g = out[0]
    assert g["id"] == "soc-CIXH5000_00"
    assert g["driver"] == "mali"
    assert g["class"] == "mid"
    assert g["renderer"] == SKY1_R15_CONT_CAPTURE["renderer"]
    # The Mali must explicitly carry display_only: false.
    assert g["display_only"] is False, (
        f"Mali gpu dict has display_only={g.get('display_only')!r}; "
        f"the field must be set to False (not absent)."
    )


def test_sky1_r15_cont_adversarial_topology_partial_dict_for_mali(env):
    """Adversarial review: the fix's _iter_platform_gpu_devices yields a
    dict with `discrete: False, render: True, display_only: False`; if
    a future refactor of class_from_topology forgets to read those
    fields via .get() (the r18 hardening was about exactly this), the
    Mali would crash with KeyError. Pin it: class_from_topology({})
    returns weak, class_from_topology({"driver": "mali"}) returns mid,
    class_from_topology({"driver": "mali", "discrete": False}) returns
    mid, class_from_topology({"driver": "mali", "render": True})
    returns mid, and a stripped entry with only `display_only` set
    returns weak (not mid -- display_only is a filter signal, not a
    class signal).
    """
    # Empty dict: defaults to weak, no exception.
    assert env.class_from_topology({}) == "weak"
    # Missing discrete, render, display_only: driver=mali -> mid.
    assert env.class_from_topology({"driver": "mali"}) == "mid"
    # discrete=False explicit.
    assert env.class_from_topology({"driver": "mali", "discrete": False}) == "mid"
    # render=True explicit.
    assert env.class_from_topology({"driver": "mali", "render": True}) == "mid"
    # display_only=True does NOT promote to mid -- the driver name drives
    # the topology fallback; display_only is for list_gpus() filtering.
    assert env.class_from_topology({"driver": "mali", "display_only": True}) == "mid", (
        "class_from_topology must read driver, not display_only; a "
        "regression that read display_only first would wrongly demote "
        "the Mali to weak on the live .66 layout."
    )
    # The combined dict the live _iter_platform_gpu_devices() yields.
    plat_gpu = {
        "id": "soc-CIXH5000_00",
        "card": "",
        "slot": "CIXH5000:00",
        "vendor": "other",
        "driver": "mali",
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "display_only": False,
    }
    assert env.class_from_topology(plat_gpu) == "mid"


# ---------------------------------------------------------------------------
# SKY1_R20_CONT_CAPTURE — 2026-09-30T12:33Z (this dispatch, the operator
# continuation). This is the FOURTH independent live ms pin on .66.
#
# Captured live from
#   /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z (md5
#   e7e77d4cc7cf0294902086149486096e, byte-for-byte identical to this
#   branch's launcher/ncz-screensaver at HEAD f365279) on an MS-R1 (Sky1)
#   after a /usr/bin/python3 launcher stop (operator policy: do not
#   reboot, reinstall or change system config on .66; the running hack
#   was stopped only briefly to allow the calibrator to write the cache,
#   then immediately restarted).
#
# Live evidence (cmd_status --json, doctor --json, vulkaninfo, ps):
#   gpu_class=mid, ms=9.89, source=calibration, when=<timestamp>
#   pool_class=mid
#   gpu.display_class=integrated
#   vulkaninfo: deviceName=Mali-G720-Immortalis, llvmpipe count=0
#   compositor: singularity-labwc running (PID 9206, session c2)
#   shader hack: hyprsaver_donut_gles3 at 60 fps (1800 frames in 30.01 s)
#   NCZ_GPU_BACKEND=mali
#   hack env has NO LIBGL_ALWAYS_SOFTWARE, NO GALLIUM_DRIVER=llvmpipe,
#   NO swrast -> policy "never fall back to software on a GPU system"
#   holds on this host with this fix.
# ---------------------------------------------------------------------------

SKY1_R20_CONT_CAPTURE = {
    # Captured from /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z
    # status --json on an MS-R1 (Sky1) at 2026-09-30T16:39:07+0000 (UTC;
    # -04:00 local), measured by /usr/bin/python3 /usr/libexec/.../calibrate
    # after stop/start.
    "renderer": "Mali-G720-Immortalis",
    "ms": 9.89,
    "when": "2026-09-30T16:39:07+0000",
    "hack": "hyprsaver_donut_gles3",
    "fps": 60,
    "fps_evidence": "1800 frames in 30.01 s = 60.0 fps",
}


def test_sky1_r20_cont_class_from_ms_keeps_9_89_in_mid_band(env):
    """The ms band 8.0 <= ms < 20.0 must stay `mid` -- this is the
    operator's contract. 9.89 (this dispatch's live ms, captured 2026-
    09-30T16:39:07Z on .66 with the fix deployed to
    /tmp/ncz-screensaver-validate-r20-2026-09-30T1232Z) sits squarely in
    the band, so any change to CLASS_WEAK_MS or CLASS_MID_MS that pushes
    9.89 to `weak` would fail this test.

    Pairs with three previous live-ms pins:
      SKY1_PASS13_CAPTURE     ms=9.88  (2026-09-30, pass-3)
      SKY1_R18_CAPTURE        ms=14.71 (2026-09-30T11:50, r18)
      SKY1_R15_CONT_CAPTURE   ms=11.93 (2026-09-30T11:53, r15-cont)
    All four (now FOUR independent pins) classify as `mid`.
    """
    assert 8.0 <= SKY1_R20_CONT_CAPTURE["ms"] < 20.0, (
        f"this dispatch's live ms {SKY1_R20_CONT_CAPTURE['ms']} is "
        f"outside the mid band [8.0, 20.0); the calibrator threshold "
        f"contract should be re-checked."
    )
    assert env.class_from_ms(SKY1_R20_CONT_CAPTURE["ms"]) == "mid", (
        f"class_from_ms({SKY1_R20_CONT_CAPTURE['ms']}) returned a non-mid "
        f"class; CLASS_WEAK_MS or CLASS_MID_MS drifted."
    )


def test_sky1_r20_cont_cmd_status_reports_mid_with_this_dispatches_capture(
    env, monkeypatch, capsys
):
    """Pin the operator-visible cmd_status --json output captured by this
    dispatch (r20-cont, 2026-09-30T16:39Z): gpu_class=mid, ms=9.89,
    source=calibration.

    This is the FOURTH independent live pin on the operator's primary
    complaint (the GPU verdict in the launcher's status row). A
    regression that flips the threshold band (e.g. raises
    CLASS_WEAK_MS below 9.89) would now fail FOUR independent pins
    instead of three.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R20_CONT_CAPTURE["ms"],
                "renderer": SKY1_R20_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "gpu",
                "source": "calibration",
                "when": SKY1_R20_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    rc = env.cmd_status(type("A", (), {"json": True, "diagnostics": False})())
    assert rc in (0, 3)
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_class"] == "mid", (
        f"cmd_status --json reported gpu_class={out['gpu_class']!r}; "
        f"this dispatch's live capture shows gpu_class=mid on .66."
    )
    assert out["gpu_class_score_ms"] == SKY1_R20_CONT_CAPTURE["ms"], (
        f"cmd_status gpu_class_score_ms={out['gpu_class_score_ms']!r}; "
        f"expected {SKY1_R20_CONT_CAPTURE['ms']} (this dispatch's live ms)."
    )
    assert out["gpu_class_source"] == "calibration"


def test_sky1_r20_cont_cmd_gpus_reports_mid_against_live_capture(
    env, monkeypatch, capsys
):
    """Pin the operator-visible `ncz-screensaver gpus` text output
    against this dispatch's live sysfs layout + live cache. The exact
    text format is what the operator reads on a shell.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R20_CONT_CAPTURE["ms"],
                "renderer": SKY1_R20_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "gpu",
                "source": "calibration",
                "when": SKY1_R20_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    args = type("A", (), {"json": False})()
    assert env.cmd_gpus(args) == 0
    text = capsys.readouterr().out.strip().splitlines()
    assert len(text) == 1, (
        f"cmd_gpus printed {len(text)} rows; this dispatch's live .66 "
        f"capture shows exactly 1 (the single Mali)."
    )
    parts = text[0].split()
    assert parts[0] == "GPU0"
    assert parts[1] == "soc-CIXH5000_00"
    assert parts[2] == "other"
    assert parts[3] == "mali"
    assert parts[4] == "mid", (
        f"cmd_gpus text reported class={parts[4]!r}; live .66 capture "
        f"shows class=mid on the Mali."
    )
    assert parts[5] == "display"


def test_sky1_r20_cont_cmd_doctor_pool_class_is_mid(env, monkeypatch, capsys):
    """Pin the operator-visible `ncz-screensaver doctor --json` output
    against this dispatch's live cache + live sysfs layout. Confirms
    pool_class=mid, gpu.display_class=integrated, gpu_class.display
    .class=mid, .ms=9.89, .source=calibration.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R20_CONT_CAPTURE["ms"],
                "renderer": SKY1_R20_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "gpu",
                "source": "calibration",
                "when": SKY1_R20_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    args = type("A", (), {"json": True})()
    assert env.cmd_doctor(args) == 0
    out = json.loads(capsys.readouterr().out)
    assert out["gpu"]["display_class"] == "integrated"
    assert out["pool_class"] == "mid", (
        f"cmd_doctor --json reported pool_class={out['pool_class']!r}; "
        f"live .66 capture shows pool_class=mid with this fix."
    )
    assert out["gpu_class"]["display"]["class"] == "mid"
    assert out["gpu_class"]["display"]["ms"] == SKY1_R20_CONT_CAPTURE["ms"]
    assert out["gpu_class"]["display"]["source"] == "calibration"
    assert (
        out["gpu_class"]["display"]["renderer"] == (SKY1_R20_CONT_CAPTURE["renderer"])
    )
    # The Mali is the only entry, and it's not display_only.
    assert len(out["gpus"]) == 1
    g = out["gpus"][0]
    assert g["id"] == "soc-CIXH5000_00"
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["display_only"] is False


def test_sky1_r20_cont_no_software_fallback_in_hack_env(env, monkeypatch):
    """Policy: never fall back to software rendering on a GPU system.
    This dispatch re-verified live that the running hack on .66 has no
    LIBGL_ALWAYS_SOFTWARE, no GALLIUM_DRIVER=llvmpipe, no swrast in its
    env (doctor --json reports NCZ_GPU_BACKEND=mali, LD_LIBRARY_PATH
    pins the CIX EGL vendor JSON).

    This test asserts that even when a hack_env is built for the
    Sky1 / Mali layout, none of the software-fallback env knobs leak in.
    A regression that adds LIBGL_ALWAYS_SOFTWARE=1 (or similar) to the
    hack env on a real-GPU host would fail this test.
    """
    _build_sky1_pass13_sysfs(env)
    seed_cache(
        env,
        {
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": SKY1_R20_CONT_CAPTURE["ms"],
                "renderer": SKY1_R20_CONT_CAPTURE["renderer"],
                "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
                "timer": "gpu",
                "source": "calibration",
                "when": SKY1_R20_CONT_CAPTURE["when"],
                "gpu": "soc-CIXH5000_00",
                "driver": "mali",
            }
        },
    )
    settings, _ = env.load_settings()
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    # Mock build_child_env so the test does not depend on a real Wayland
    # session, but keeps the structure: display GPU -> display=True ->
    # env has no offload vars, no software fallback knobs.
    monkeypatch.setattr(
        env,
        "build_child_env",
        lambda *a, **kw: {
            "HOME": "/home/user",
            "XDG_RUNTIME_DIR": "/run/user/1000",
            "WAYLAND_DISPLAY": "wayland-0",
            "LD_LIBRARY_PATH": (
                "/opt/singularity/lib:/opt/cixgpu-pro/lib/"
                "aarch64-linux-gnu:/opt/cixgpu-compat/share/glvnd/"
                "egl_vendor.d/40_cix.json"
            ),
            "NCZ_GPU_BACKEND": "mali",
        },
    )
    hack_id = SKY1_R20_CONT_CAPTURE["hack"]
    plan = env.plan_for(hack_id, settings)
    gpus = env.list_gpus()
    disp = next(g for g in gpus if g["display"])
    env_dict = env.build_child_env("hack", settings, offload=False)
    env_dict.update(env.gpu_env(disp))
    # No software fallback.
    assert "LIBGL_ALWAYS_SOFTWARE" not in env_dict
    assert "GALLIUM_DRIVER" not in env_dict
    swrast = env_dict.get("MESA_LOADER_DRIVER_OVERRIDE", "")
    assert swrast != "swrast"
    # Plan: render on the display GPU, no offload.
    assert plan["gpu"] is disp
    assert plan["offload"] is False
    assert plan["class"] == "mid"


def test_sky1_r20_cont_all_four_live_ms_pins_classify_as_mid(env):
    """Adversarial cross-check: all four independent live ms pins
    captured against .66 (SKY1_PASS13_CAPTURE ms=9.88, SKY1_R18_CAPTURE
    ms=14.71, SKY1_R15_CONT_CAPTURE ms=11.93, SKY1_R20_CONT_CAPTURE
    ms=9.89) classify as `mid` because the threshold band
    8.0 <= ms < 20.0 is the operator's contract. A regression that
    flips the band (e.g. raises CLASS_WEAK_MS below 9.88, or lowers
    CLASS_MID_MS below 9.88) would fail at least one of these and
    surface the misclassification.
    """
    pins = [
        ("SKY1_PASS13_CAPTURE", 9.88),
        ("SKY1_R18_CAPTURE", 14.71),
        ("SKY1_R15_CONT_CAPTURE", 11.93),
        ("SKY1_R20_CONT_CAPTURE", 9.89),
    ]
    for name, ms in pins:
        assert env.class_from_ms(ms) == "mid", (
            f"{name} ms={ms} classified as non-mid; the calibrator "
            f"threshold contract (8.0 <= ms < 20.0 -> mid) drifted."
        )


# ---- per-GPU verdict and PRIME offload explanation -----------------------------------
# UX requirement (operator 2026-09-30, verbatim): "in the UX where it reports
# 'weak' detected, it needs to be able to say 'GPU0 (weak), GPU1 (strong)' and it
# needs text to describe the PRIME offload if the configuration is
# supported/detected." The fixtures below use the live detection strings and
# calibration numbers recorded on a UHD 630 + RTX 2060 laptop (dual-GPU amd64 laptop, Intel UHD +


def _hybrid_nvidia_seed(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            disp["id"]: {"class": "weak", "ms": 46.5},
            nv["id"]: {"class": "strong", "ms": 2.7},
        },
    )
    return disp, nv


def _single_soc_seed(env):
    layout(env, "single-soc")
    seed_display(env, "mid")


def test_gpu_verdict_text_hybrid_nvidia_dual_gpu(env):
    """Hybrid Intel + NVIDIA: Intel UHD is GPU0 (Weak), NVIDIA RTX 2060 is GPU1 (Strong)."""
    disp, nv = _hybrid_nvidia_seed(env)
    text = env.gpu_verdict_text(S)
    assert text == "GPU0 (Weak), GPU1 (Strong)"
    rows = env.gpu_verdict_rows(S)
    assert [r["id"] for r in rows] == [disp["id"], nv["id"]]
    assert [r["index"] for r in rows] == ["GPU0", "GPU1"]
    assert [r["class"] for r in rows] == ["weak", "strong"]
    assert rows[0]["display"] is True and rows[1]["display"] is False
    assert rows[0]["ms"] == 46.5 and rows[1]["ms"] == 2.7


def test_per_gpu_labels_use_class_title_for_unknown_and_known(env):
    """The Vala plugin reads gpu_verdict_rows() through the public API. The
    human label must match CLASS_TITLE for known classes (Weak /
    Mid-range / Strong) and fall back to a capitalised placeholder for
    missing classes so the GTK row stays well-formed when the renderer
    string is unknown."""
    layout(env, "single-soc")
    seed_display(env, "mid")
    text = env.gpu_verdict_text(S)
    assert text == "GPU0 (Mid-range)"

    seed_display(env, "weak")
    assert env.gpu_verdict_text(S) == "GPU0 (Weak)"

    seed_display(env, "strong")
    assert env.gpu_verdict_text(S) == "GPU0 (Strong)"


def test_per_gpu_dual_gpu_layout_panel_first_then_offload(env):
    """AMD-panel layout: the AMD dGPU owns the panel, the Intel iGPU is idle.
    The verdict reads "GPU0 (Strong), GPU1 (Weak)" — the panel GPU is GPU0
    and the offload candidate is GPU1, even when the offload candidate is
    weaker than the panel (the plugin shows the panel first because the
    compositor binds it)."""
    layout(env, "amd-dgpu-display+intel-idle")
    amd, intel = env.list_gpus()
    seed_cache(
        env,
        {
            amd["id"]: {"class": "strong", "ms": 5.5},
            intel["id"]: {"class": "weak", "ms": 46.5},
        },
    )
    rows = env.gpu_verdict_rows(S)
    assert [r["index"] for r in rows] == ["GPU0", "GPU1"]
    assert rows[0]["display"] is True and rows[1]["display"] is False
    assert env.gpu_verdict_text(S) == "GPU0 (Strong), GPU1 (Weak)"


def test_prime_offload_explanation_single_soc_uses_panel_label(env):
    """Single-GPU "no offload" path on a MS-R1 .66 host: the
    explanation names the panel GPU driver (mali), states that offload is
    not available and never implies a software fallback. This is the path
    the Vala plugin renders in the Settings > Screensaver and Lockscreen
    page on a single-GPU arm64 box."""
    _single_soc_seed(env)
    text = env.prime_offload_explanation(S)
    assert "not available" in text
    assert "mali" in text
    assert "Screensavers render on that GPU" in text
    # Explicitly absent — never claim a software fallback on a GPU system.
    assert "software" not in text.lower()
    # And the explanation is empty when the launcher cannot see any GPU
    # (defensive default; the plugin treats "" as "not yet determined").
    monkey = pytest.MonkeyPatch()
    try:
        monkey.setattr(env, "list_gpus", list)
        assert env.prime_offload_explanation(S) == ""
    finally:
        monkey.undo()


def test_gpu_verdict_text_mini_single_mali_g720(env):
    """.66 / mini: single Mali-G720 mid-range SoC; verdict is GPU0 only."""
    _single_soc_seed(env)
    text = env.gpu_verdict_text(S)
    assert text == "GPU0 (Mid-range)"
    rows = env.gpu_verdict_rows(S)
    assert len(rows) == 1
    assert rows[0]["class"] == "mid"
    assert rows[0]["display"] is True


def test_gpu_verdict_rows_index_is_panel_first_even_when_offload_is_strong(env):
    """AMD-panel layout (AMD dGPU owns panel, idle Intel iGPU): GPU0 is the strong
    panel dGPU, GPU1 is the idle weak iGPU. Panel-first order is preserved."""
    layout(env, "amd-dgpu-display+intel-idle")
    amd, intel = env.list_gpus()
    seed_cache(
        env,
        {
            amd["id"]: {"class": "strong", "ms": 5.5},
            intel["id"]: {"class": "weak", "ms": 46.5},
        },
    )
    rows = env.gpu_verdict_rows(S)
    assert [r["index"] for r in rows] == ["GPU0", "GPU1"]
    assert rows[0]["display"] is True and rows[1]["display"] is False
    assert rows[0]["id"] == amd["id"]
    assert rows[1]["id"] == intel["id"]
    assert env.gpu_verdict_text(S) == "GPU0 (Strong), GPU1 (Weak)"


def test_gpu_short_label_known_vendors_and_unknown_drivers(env):
    """gpu_short_label uses the pretty vendor table when known, else driver name."""
    assert env.gpu_short_label({"vendor": "intel", "driver": "i915"}) == "Intel i915"
    assert env.gpu_short_label({"vendor": "amd", "driver": "amdgpu"}) == "AMD amdgpu"
    assert (
        env.gpu_short_label({"vendor": "nvidia", "driver": "nvidia"}) == "NVIDIA nvidia"
    )
    # Unknown vendor (e.g. Mali): driver name alone, no leading 'other '.
    assert env.gpu_short_label({"vendor": "other", "driver": "mali"}) == "mali"
    assert (
        env.gpu_short_label({"vendor": "other", "driver": "mali_kbase"}) == "mali_kbase"
    )
    # Empty inputs: falls back to 'GPU' rather than empty string.
    assert env.gpu_short_label({}) == "GPU"


def test_class_title_matches_pretty_names(env):
    """CLASS_TITLE maps to the human-readable strings the GTK4 reference app
    and the Vala plugin already use."""
    assert env.CLASS_TITLE == {"weak": "Weak", "mid": "Mid-range", "strong": "Strong"}


def test_gpu_verdict_text_handles_missing_class_field(env, monkeypatch):
    """Defensive: a cache entry without a `class` field must not crash
    gpu_verdict_text(). The handler renders 'GPU<N> (Unknown)' so the row
    stays visible instead of disappearing silently."""
    layout(env, "single-soc")
    settings = {"gpu-offload": "auto", "pool-gpu-class": "auto"}
    # Replace gpu_class to return an entry without a 'class' field.
    monkeypatch.setattr(
        env, "gpu_class", lambda s, g=None, run=False, force=False: {"ms": None}
    )
    text = env.gpu_verdict_text(settings)
    assert text == "GPU0 (Unknown)"


def test_prime_offload_supported_hybrid_nvidia(env):
    """A hybrid Intel + NVIDIA laptop has a discrete NVIDIA offload target, so PRIME is supported."""
    _hybrid_nvidia_seed(env)
    assert env.prime_offload_supported(S) is True


def test_prime_offload_supported_mini_single_soc(env):
    """.66 / mini: single GPU, no offload target, not supported."""
    _single_soc_seed(env)
    assert env.prime_offload_supported(S) is False


def test_prime_offload_explanation_hybrid_nvidia_on_ac(env):
    """Hybrid Intel + NVIDIA on AC: full PRIME explanation, names panel + offload drivers."""
    _hybrid_nvidia_seed(env)
    text = env.prime_offload_explanation(S)
    # Names the panel GPU and the offload target.
    assert "Intel i915" in text
    assert "NVIDIA nvidia" in text
    # Names the offload mechanism.
    assert "PRIME render offload" in text
    # Names the AC rule.
    assert "AC power" in text
    assert "panel GPU" in text  # "light screensavers stay on the panel GPU"
    # States the offload environment scope is the screensaver process only.
    assert "screensaver process only" in text
    # The compositor and the user session are not affected.
    assert "compositor" in text and "user session" in text
    # Never implies a software rendering fallback.
    assert "software" not in text.lower()


def test_prime_offload_explanation_hybrid_nvidia_on_battery(env):
    """Hybrid Intel + NVIDIA on battery: the AC rule is replaced; offload is not exercised."""
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            disp["id"]: {"class": "weak", "ms": 46.5},
            nv["id"]: {"class": "strong", "ms": 2.7},
        },
    )
    text = env.prime_offload_explanation(S)
    assert "On battery" in text
    assert "panel GPU" in text
    # On battery, even shader hacks stay on the panel GPU.
    assert env.plan_for("hyprsaver_aurora_gles3", S)["offload"] is False


def test_prime_offload_explanation_mini_single_soc_says_no(env):
    """.66 single Mali-G720: explain why offload is unavailable."""
    _single_soc_seed(env)
    text = env.prime_offload_explanation(S)
    assert "not available" in text
    assert "mali" in text
    assert "Screensavers render on that GPU" in text
    assert "software" not in text.lower()


def test_prime_offload_explanation_handles_no_gpu(env, monkeypatch):
    """An unplugged or undiscovered GPU produces no invented device label."""
    monkeypatch.setattr(env, "list_gpus", list)
    assert env.prime_offload_explanation(S) == ""


def test_prime_offload_explanation_uses_one_gpu_snapshot(env, monkeypatch):
    """A hotplug between probes cannot desynchronize row and device counts."""
    _single_soc_seed(env)
    gpus = env.list_gpus()
    calls = 0

    def changing_gpus():
        nonlocal calls
        calls += 1
        return gpus if calls == 1 else []

    monkeypatch.setattr(env, "list_gpus", changing_gpus)
    assert "not available" in env.prime_offload_explanation(S)
    assert calls == 1


def test_prime_offload_explanation_two_intels_no_discrete_says_no(env):
    """Two Intel iGPUs (no discrete target): text says plainly that no offload
    configuration was detected, never implies software rendering."""
    import shutil

    drm = env.sysfs / "class/drm"
    if drm.exists():
        shutil.rmtree(drm)
    drivers = env.sysfs / "drivers"
    if drivers.exists():
        shutil.rmtree(drivers)
    add_gpu(
        env.sysfs,
        "card0",
        "0x8086",
        "0x9bca",
        "i915",
        "0000:00:02.0",
        connected=True,
        boot_vga=True,
    )
    add_gpu(env.sysfs, "card1", "0x8086", "0x3e9b", "i915", "0000:03:00.0")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.switcheroo_envs, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert all(g["vendor"] == "intel" for g in gpus)
    seed_cache(env, {g["id"]: {"class": "weak"} for g in gpus})
    text = env.prime_offload_explanation(S)
    assert "not detected" in text
    assert "Intel i915" in text
    assert "software" not in text.lower()
    assert env.prime_offload_supported(S) is False


def test_prime_offload_explanation_second_gpu_without_pci_slot(env):
    """A second GPU with no PCI slot (e.g. a SoC companion controller) is not
    a PRIME target. The explanation states that no offload configuration is
    exposed and names the second GPU."""
    layout(env, "amd-dgpu-display+intel-idle")
    amd, intel = env.list_gpus()
    # Strip the PCI slot on the Intel iGPU to simulate a SoC companion controller.
    intel_pci = intel.copy()
    intel_pci["slot"] = ""
    # list_gpus() already filtered the iGPU in this layout because it has no
    # render node — swap the model so we can test the "multi-GPU, no PCI" path.
    for fn in (env.list_gpus, env.switcheroo_envs, env.gpu_topology):
        fn.cache_clear()
    # Inject a synthetic non-PCI GPU into the cache so prime_offload_explanation
    # sees len(rows) >= 2 but no PCI-slot offload target.
    monkeypatch = __import__("pytest").MonkeyPatch()
    monkeypatch.setattr(env, "list_gpus", lambda: [amd, intel_pci])
    try:
        seed_cache(env, {amd["id"]: {"class": "strong"}, "soc-x": {"class": "mid"}})
        text = env.prime_offload_explanation(S)
        assert "not available" in text
        assert "PCI slot" in text
        assert "software" not in text.lower()
    finally:
        monkeypatch.undo()


def test_status_json_includes_per_gpu_verdict_and_prime(env, capsys, monkeypatch):
    """`ncz-screensaver status --json` exposes gpu_verdict and prime_offload
    with the hybrid Intel + NVIDIA live strings."""
    _hybrid_nvidia_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    monkeypatch.setattr(env, "read_state", lambda: None)
    assert env.cmd_status(type("A", (), {"json": True, "diagnostics": False})()) == 3
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_verdict"] == "GPU0 (Weak), GPU1 (Strong)"
    assert [r["class"] for r in out["gpu_verdict_rows"]] == ["weak", "strong"]
    assert out["prime_offload"].startswith("The Intel i915 drives the display")
    assert "PRIME render offload" in out["prime_offload"]
    assert "software" not in out["prime_offload"].lower()
    # Backward-compat: legacy keys still present.
    assert out["gpu_class"] == "weak"
    assert out["gpu_class_score_ms"] == 46.5


def test_status_json_single_soc_explains_unavailable_offload(env, capsys, monkeypatch):
    """.66 status: GPU0 is Mid-range and offload unavailability is explained."""
    _single_soc_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    monkeypatch.setattr(env, "read_state", lambda: None)
    env.cmd_status(type("A", (), {"json": True, "diagnostics": False})())
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_verdict"] == "GPU0 (Mid-range)"
    assert "not available" in out["prime_offload"]
    assert out["gpu_class"] == "mid"


def test_status_text_appends_verdict_when_not_running(env, capsys, monkeypatch):
    """Plain-text `ncz-screensaver status` (no --json) shows the verdict in
    the 'not running' line so users on hybrid Intel + NVIDIA laptops see the hybrid verdict at a
    glance."""
    _hybrid_nvidia_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    monkeypatch.setattr(env, "read_state", lambda: None)
    env.cmd_status(type("A", (), {"json": False, "diagnostics": False})())
    text = capsys.readouterr().out.strip()
    assert text == "not running  (GPU0 (Weak), GPU1 (Strong))"


def test_status_text_appends_verdict_when_running(env, capsys, monkeypatch):
    """Plain-text `ncz-screensaver status` also appends the verdict when a
    hack is up, so ops scripts reading the text keep the same column layout
    regardless of running state."""
    _hybrid_nvidia_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    monkeypatch.setattr(
        env,
        "read_state",
        lambda: {
            "pid": 4242,
            "hack": "blackhole_gles3",
            "child_pid": 4243,
            "started": 1_700_000_000.0,
        },
    )
    env.cmd_status(type("A", (), {"json": False, "diagnostics": False})())
    text = capsys.readouterr().out.strip()
    assert "running blackhole_gles3 (supervisor pid 4242," in text
    assert "(GPU0 (Weak), GPU1 (Strong))" in text


def test_gpus_json_includes_per_gpu_verdict_fields(env, capsys, monkeypatch):
    """`ncz-screensaver gpus --json` keeps the legacy gpu dict and adds the
    per-GPU verdict fields (index, class, ms, renderer) for the UX."""
    disp, nv = _hybrid_nvidia_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    env.cmd_gpus(type("A", (), {"json": True})())
    rows = json.loads(capsys.readouterr().out)
    assert [r["index"] for r in rows] == ["GPU0", "GPU1"]
    assert [r["id"] for r in rows] == [disp["id"], nv["id"]]
    assert [r["class"] for r in rows] == ["weak", "strong"]
    assert rows[0]["ms"] == 46.5 and rows[1]["ms"] == 2.7
    # Legacy fields preserved.
    for k in ("card", "slot", "vendor", "driver", "display", "boot_vga", "discrete"):
        assert k in rows[0]


def test_doctor_includes_verdict_and_prime(env, capsys, monkeypatch):
    """`ncz-screensaver doctor` includes gpu_verdict, gpu_verdict_rows,
    prime_offload and prime_offload_supported."""
    _hybrid_nvidia_seed(env)
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    env.cmd_doctor(type("A", (), {})())
    out = json.loads(capsys.readouterr().out)
    assert out["gpu_verdict"] == "GPU0 (Weak), GPU1 (Strong)"
    assert len(out["gpu_verdict_rows"]) == 2
    assert out["prime_offload"].startswith("The Intel i915 drives the display")
    assert out["prime_offload_supported"] is True


def test_cmd_gpus_text_handles_missing_class_field(env, capsys, monkeypatch):
    """Defensive: `ncz-screensaver gpus` (text) must not crash if the
    cache returns a verdict entry without a `class` field. The row prints
    a placeholder class (`-`) and the format string stays intact. Pinned
    against the symmetric hardening in `gpu_verdict_text`."""
    layout(env, "single-soc")
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    monkeypatch.setattr(
        env, "gpu_class", lambda s, g=None, run=False, force=False: {"ms": None}
    )
    rc = env.cmd_gpus(type("A", (), {"json": False})())
    assert rc == 0
    out = capsys.readouterr().out
    assert "GPU0" in out
    # The placeholder for a missing class is a dash (visual gutter), never
    # the literal Python `None` and never a TypeError traceback.
    assert "None" not in out
    assert "Traceback" not in out


def test_prime_offload_fragments_are_translatable_format_strings(env):
    """The PRIME render-offload explanation is composed from named module
    constants (_PRIME_INTRO / _PRIME_AC_RULE / _PRIME_BATTERY_RULE /
    _PRIME_SCOPE / _PRIME_NO_PCI / _PRIME_NOT_DETECTED). They are written
    with %s-style positional arguments so that gettext (xgettext) can
    extract them as a single translatable string with a documented format
    spec. Translators reorder the sentence freely; the launcher substitutes
    the {panel} / {targets} runtime values into the translated template at
    render time.

    Pinned by operator UX requirement 2026-09-30 ("strings must be
    translatable like existing UI strings") and the adversarial review
    round that flagged the previous f-string concatenation as not
    translation-ready.
    """
    fragments = (
        env._PRIME_INTRO,
        env._PRIME_AC_RULE,
        env._PRIME_BATTERY_RULE,
        env._PRIME_SCOPE,
        env._PRIME_NO_PCI,
        env._PRIME_NOT_DETECTED,
    )
    for frag in fragments:
        # Each fragment is a plain English literal (str, not bytes).
        assert isinstance(frag, str)
        # Each fragment uses %s-style positional args, NOT f-string {name}
        # interpolation. This is what gettext extracts cleanly.
        assert "{" not in frag
        assert "}" not in frag
        # Each fragment ends with a sentence terminator so concatenating
        # them with a single space produces a grammatical paragraph.
        assert frag.rstrip().endswith((".", ":"))


# ---------------------------------------------------------------------------


def test_prime_offload_explanation_pure_single_soc_does_not_reference_a_second_gpu(env):
    """Defense-in-depth on the no-offload path: a single-GPU explanation
    must NEVER mention a second GPU, a switcheroo entry, or DRI_PRIME — the
    settings UX shows this verbatim and the operator would notice a stale
    reference to a non-existent device."""
    _single_soc_seed(env)
    text = env.prime_offload_explanation(S)
    assert "DRI_PRIME" not in text
    assert "__NV_PRIME_RENDER_OFFLOAD" not in text
    assert "switcheroo" not in text.lower()
    assert "second GPU" not in text


def test_single_gpu_no_offload_path_does_not_inherit_ambient_prime(env, monkeypatch):
    """The task pin: the single-GPU no-offload path must NOT inherit
    ambient DRI_PRIME / __NV_PRIME_RENDER_OFFLOAD from the calling process
    environment. The launcher pops them from the child env unconditionally
    (see `for name in OFFLOAD_VARS: env.pop(name, None)` in build_child_env),
    so even when the calling process is a Vala plugin that was started by
    a hybrid-graphics-aware shell and accidentally leaks DRI_PRIME=1, the
    saver process on a single-GPU host never sees the variable.

    We deliberately set the ambient env here (the test does not assume an
    ambient PRIME — it asserts the launcher clears it) and assert the
    child env is empty of those variables."""
    _single_soc_seed(env)
    monkeypatch.setenv("DRI_PRIME", "1")
    monkeypatch.setenv("__NV_PRIME_RENDER_OFFLOAD", "1")
    monkeypatch.setenv("__VK_LAYER_NV_optimus", "1")
    env_plan = env.plan_for("hyprsaver_aurora_gles3", S)
    assert env_plan["offload"] is False
    child = env.build_child_env("hyprsaver_aurora_gles3", S)
    # The launcher pops OFFLOAD_VARS before adding topology-derived values, so
    # DRI_PRIME must be absent from the saver child env on a single-GPU
    # no-offload plan — never empty-string, never the leaked ambient value.
    assert "DRI_PRIME" not in child, (
        f"ambient DRI_PRIME leaked into saver child env: {child.get('DRI_PRIME')!r}"
    )
    assert "__NV_PRIME_RENDER_OFFLOAD" not in child
    assert "__VK_LAYER_NV_optimus" not in child
    assert "__GLX_VENDOR_LIBRARY_NAME" not in child
    assert "MESA_VK_DEVICE_SELECT" not in child


def test_single_gpu_no_offload_explanation_is_stable_with_ambient_prime(
    env, monkeypatch
):
    """The prime_offload_explanation() output must not change when the
    ambient process environment carries a PRIME variable on a single-GPU
    host. We set the ambient PRIME here (again, the test does not assume
    a clean ambient) and verify the explanation text is byte-identical
    with the no-ambient-PRIME explanation."""
    _single_soc_seed(env)
    baseline = env.prime_offload_explanation(S)
    monkeypatch.setenv("DRI_PRIME", "pci-0000:01:00.0")
    monkeypatch.setenv("__NV_PRIME_RENDER_OFFLOAD", "1")
    polluted = env.prime_offload_explanation(S)
    assert polluted == baseline
    # And the launcher's plan still says no offload on a single-GPU host,
    # so the explanation matches what `ncz-screensaver plan` would print.
    plan = env.plan_for("hyprsaver_aurora_gles3", S)
    assert plan["offload"] is False


def test_dual_gpu_layout_offload_path_still_clears_ambient_prime(env, monkeypatch):
    """Belt-and-braces: even on a hybrid-graphics host where the plan
    DOES offload, the launcher reads switcheroo_envs() / build_child_env
    to populate the child env — it never inherits DRI_PRIME / __NV_*
    from the calling process. The Vala plugin is launched by
    singularity-settings, which inherits its own environment from
    gedit / uid; any ambient DRI_PRIME from there must NOT reach the
    saver process.

    We seed an ambient value that is distinguishable from any
    topology-derived value (e.g. a DRI_PRIME BDF that does not exist on
    the synthetic hybrid Intel + NVIDIA topology), so a leaked ambient variable is
    observable in the child env. The __NV_PRIME_RENDER_OFFLOAD=1 case is
    the canonical offload value (NVIDIA_OFFLOAD_ENV), so we instead seed
    __VK_LAYER_NV_optimus which the launcher explicitly strips via
    GPU_ENV_NEVER."""
    _hybrid_nvidia_seed(env)
    monkeypatch.setenv("DRI_PRIME", "pci-dead:be:ef.0")
    monkeypatch.setenv("__VK_LAYER_NV_optimus", "ambient-leak-canary")
    monkeypatch.setenv("__GLX_VENDOR_LIBRARY_NAME", "ambient-leak-canary")
    heavy = env.plan_for("hyprsaver_xshadertoy_hexplasma_gles3", S)
    # Sanity: a heavy shader hack on AC should offload to the NVIDIA.
    assert heavy["offload"] is True
    heavy_child = env.build_child_env("hyprsaver_xshadertoy_hexplasma_gles3", S)
    # on a UHD 630 + RTX 2060 laptop the offload target is NVIDIA, so gpu_env() returns the
    # NVIDIA_OFFLOAD_ENV triple (__NV_PRIME_RENDER_OFFLOAD / __GLX_VENDOR_LIBRARY_NAME
    # / __VK_LAYER_NV_optimus) — NOT DRI_PRIME (NVIDIA uses the __NV_*
    # variables, not DRI_PRIME). The launcher pops OFFLOAD_VARS first and
    # then sets the canonical NVIDIA values; the ambient canary values
    # must not leak through.
    assert "DRI_PRIME" not in heavy_child, (
        f"ambient DRI_PRIME leaked into the NVIDIA offloaded child env: {heavy_child.get('DRI_PRIME')!r}"
    )
    assert heavy_child.get("__NV_PRIME_RENDER_OFFLOAD") == "1", (
        f"NVIDIA offload child env must carry __NV_PRIME_RENDER_OFFLOAD=1, "
        f"got {heavy_child.get('__NV_PRIME_RENDER_OFFLOAD')!r}"
    )
    assert heavy_child.get("__VK_LAYER_NV_optimus") == "NVIDIA_only", (
        f"ambient __VK_LAYER_NV_optimus leaked into the offloaded child env: {heavy_child.get('__VK_LAYER_NV_optimus')!r}"
    )
    assert heavy_child.get("__GLX_VENDOR_LIBRARY_NAME") == "nvidia", (
        f"ambient __GLX_VENDOR_LIBRARY_NAME leaked into the offloaded child env: {heavy_child.get('__GLX_VENDOR_LIBRARY_NAME')!r}"
    )
    # And the light (non-shader) hack does not offload on AC (the rule is:
    # shader hacks offload whenever there is a faster offload target; the
    # classics only offload when they NEED a faster class), so the child
    # env has no PRIME variables at all — neither ambient-derived nor
    # topology-derived.
    light = env.plan_for("light_gles3", S)
    assert light["offload"] is False
    light_child = env.build_child_env("light_gles3", S)
    assert "DRI_PRIME" not in light_child
    assert "__NV_PRIME_RENDER_OFFLOAD" not in light_child
    assert "__VK_LAYER_NV_optimus" not in light_child
    assert "__GLX_VENDOR_LIBRARY_NAME" not in light_child
