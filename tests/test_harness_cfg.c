/* SPDX-License-Identifier: Apache-2.0 */
/* Unit tests: render-size math, platform defaults, the software-renderer guard
 * with a fake sysfs fixture (both directions), and option flags. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ncz_render.h"
#include "ncz_gpu_guard.h"
#include "ncz_options.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void mk(const char *root, const char *rel) {
    char cmd[600];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/%s'", root, rel);
    if (system(cmd) != 0) abort();
}
static void fake_node(const char *root, const char *node, const char *driver) {
    char p[600], l[600];
    snprintf(p, sizeof p, "%s/sys/class/drm/%s/device", root, node);
    mk(root, p + strlen(root) + 1);
    char d[600];
    snprintf(d, sizeof d, "sys/bus/pci/drivers/%s", driver);
    mk(root, d);
    snprintf(l, sizeof l, "%s/driver", p);
    snprintf(d, sizeof d, "%s/sys/bus/pci/drivers/%s", root, driver);
    if (symlink(d, l) != 0) abort();
}

int main(void) {
    int rw, rh;
    /* 4K capped to 1080 keeps aspect, rounds even */
    CHECK(ncz_render_size(3840, 2160, 1.0, 1080, &rw, &rh) == 1 && rw == 1920 && rh == 1080);
    CHECK(ncz_render_size(3840, 2160, 0.5, 0, &rw, &rh) == 1 && rw == 1920 && rh == 1080);
    CHECK(ncz_render_size(3840, 2160, 0.5, 900, &rw, &rh) == 1 && rh == 900 && rw == 1600);
    CHECK(ncz_render_size(1920, 1080, 1.0, 1080, &rw, &rh) == 0 && rw == 1920 && rh == 1080);
    CHECK(ncz_render_size(1920, 1080, 1.0, 0, &rw, &rh) == 0);
    CHECK(ncz_render_size(1920, 1080, 0.1, 0, &rw, &rh) == 1 && rw == 480 && rh == 270); /* scale floor 0.25 */
    CHECK(ncz_render_size(1001, 601, 0.75, 0, &rw, &rh) == 1 && rw % 2 == 0 && rh % 2 == 0 && rw <= 1001 && rh <= 601);
    CHECK(ncz_render_size(3072, 1920, 1.0, 1080, &rw, &rh) == 1 && rh == 1080 && rw == 1728);
    CHECK(ncz_render_platform_cap("Mali-G720-Immortalis", 2160) == 1080);
    CHECK(ncz_render_platform_cap("Mali-G720-Immortalis", 720) == 1080);
    CHECK(ncz_render_platform_cap("Mesa Intel(R) UHD Graphics 630", 2160) == 1080);
    CHECK(ncz_render_platform_cap("Mesa Intel(R) UHD Graphics 630", 1080) == 0);
    CHECK(ncz_render_platform_cap("AMD Radeon Graphics (radeonsi, navi14)", 2160) == 0);
    CHECK(!strcmp(ncz_gpu_class("Mesa Intel(R) UHD Graphics 630", NULL), "weak"));
    CHECK(!strcmp(ncz_gpu_class("Mali-G720-Immortalis", NULL), "mid"));
    CHECK(!strcmp(ncz_gpu_class("AMD Radeon Graphics (radeonsi, navi14)", NULL), "strong"));
    CHECK(!strcmp(ncz_gpu_class("NVIDIA GeForce RTX 2060/PCIe/SSE2", NULL), "strong"));
    CHECK(!strcmp(ncz_gpu_class("Mesa Intel(R) UHD Graphics 630", "strong"), "strong"));   /* launcher calibration wins */
    CHECK(!strcmp(ncz_gpu_class("Mesa Intel(R) UHD Graphics 630", "STRONG"), "strong"));  /* case-insensitive */
    CHECK(!strcmp(ncz_gpu_class("Adreno (TM) 740", NULL), "mid"));
    CHECK(!strcmp(ncz_gpu_class(NULL, NULL), "mid"));
    CHECK(!strcmp(ncz_gpu_class("Mesa Intel(R) UHD Graphics 630", "bogus"), "weak"));      /* invalid override ignored */
    CHECK(ncz_render_step_down(1.0) == 0.75 && ncz_render_step_down(0.75) == 0.5 && ncz_render_step_down(0.5) == 0.35 && ncz_render_step_down(0.35) == 0.35);

    /* software renderer detection */
    CHECK(ncz_renderer_is_software("llvmpipe (LLVM 21.1.8, 128 bits)"));
    CHECK(ncz_renderer_is_software("softpipe"));
    CHECK(ncz_renderer_is_software("Software Rasterizer"));
    CHECK(!ncz_renderer_is_software("Mali-G720-Immortalis"));
    CHECK(!ncz_renderer_is_software(NULL));

    /* guard with fake sysfs */
    char root[] = "/tmp/ncz_guard_XXXXXX";
    if (!mkdtemp(root)) return 2;
    char msg[512];
    /* no GPU at all */
    CHECK(ncz_gpu_guard_check(root, "llvmpipe", NULL, msg, sizeof msg) == 1);          /* refuse without override */
    CHECK(ncz_gpu_guard_check(root, "llvmpipe", "1", msg, sizeof msg) == 0);           /* GPU-less VM with override runs */
    CHECK(ncz_gpu_guard_check(root, "llvmpipe", "0", msg, sizeof msg) == 1);
    /* virtual devices do not count */
    fake_node(root, "renderD128", "vgem");
    CHECK(!ncz_gpu_hardware_present(root, NULL, 0));
    CHECK(ncz_gpu_guard_check(root, "llvmpipe", "1", msg, sizeof msg) == 0);
    /* hardware GPU present: refuse llvmpipe with NO override */
    fake_node(root, "renderD129", "amdgpu");
    char drv[64] = "";
    CHECK(ncz_gpu_hardware_present(root, drv, sizeof drv) && !strcmp(drv, "amdgpu"));
    CHECK(ncz_gpu_guard_check(root, "llvmpipe (LLVM 21)", NULL, msg, sizeof msg) == 1 && strstr(msg, "amdgpu"));
    CHECK(ncz_gpu_guard_check(root, "llvmpipe (LLVM 21)", "1", msg, sizeof msg) == 1);  /* override ignored with a GPU */
    /* hardware renderer runs */
    CHECK(ncz_gpu_guard_check(root, "AMD Radeon Graphics (radeonsi)", NULL, msg, sizeof msg) == 0);
    /* Mali via /dev/mali0 counts as hardware */
    char root2[] = "/tmp/ncz_guard2_XXXXXX";
    if (!mkdtemp(root2)) return 2;
    mk(root2, "dev");
    char m0[600]; snprintf(m0, sizeof m0, "%s/dev/mali0", root2);
    FILE *f = fopen(m0, "w"); fclose(f);
    CHECK(ncz_gpu_guard_check(root2, "llvmpipe", "1", msg, sizeof msg) == 1);
    char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s'", root, root2);
    if (system(cmd) != 0) return 2;

    /* option flags: unknown option and stray argument are flagged */
    static const ncz_opt_def defs[] = {{"render-scale", NCZ_OPT_FLOAT, "1", 0.25, 1, "", "", NULL, "l", "d", "g"}};
    ncz_opts o;
    FILE *keep = stderr; stderr = fopen("/dev/null", "w");
    ncz_opts_init(&o, defs, 1);
    char *av[] = {"x", "--render-scale=0.5", "--help", NULL};
    int fl = ncz_opts_apply_argv(&o, 3, av, "x");
    CHECK(fl == 1 && ncz_opts_is_set(&o, "render-scale") && ncz_opts_get_float(&o, "render-scale") == 0.5);
    char *av2[] = {"x", "--nonsense", NULL};
    CHECK((ncz_opts_apply_argv(&o, 2, av2, "x") & 4) == 4);
    char *av3[] = {"x", "stray", NULL};
    CHECK((ncz_opts_apply_argv(&o, 2, av3, "x") & 8) == 8);
    char *av4[] = {"x", "--render-scale=9", NULL};
    ncz_opts_free(&o);
    ncz_opts_init(&o, defs, 1);
    ncz_opts_apply_argv(&o, 2, av4, "x");
    CHECK(!ncz_opts_is_set(&o, "render-scale") && ncz_opts_get_float(&o, "render-scale") == 1.0);
    ncz_opts_free(&o);
    stderr = keep;
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    printf("harness_cfg: all checks passed\n");
    return 0;
}
