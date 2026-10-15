/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_feed_render.c - see ncz_feed_render.h.
 *
 * Off-screen frame source for the NCZ lock client. Speaks the same wire
 * protocol as lock_framesrc_testsrc.c (see include/lock_framesrc_proto.h) but
 * renders a REAL hack through the same callbacks gles3_harness.c drives.
 *
 * Transport selection, in order:
 *   1. EGL surfaceless (EGL_PLATFORM_SURFACELESS_MESA) - Mesa/AMD/Intel/panfrost.
 *   2. EGL device (EGL_PLATFORM_DEVICE_EXT) - the proprietary NVIDIA stack.
 *   3. EGL GBM on a DRM node (EGL_PLATFORM_GBM_KHR, libgbm dlopen'd) - the CIX
 *      Mali blob: libEGL_cix advertises EGL_KHR_platform_gbm (and wayland) but
 *      NOT surfaceless or device, so both of the first two fail on O6N and the
 *      saver never produces a frame. GBM needs no compositor, no window system
 *      and no EGL surface.
 * All render into an FBO, so no compositor and no GBM *surface* is required.
 * If no platform initialises, the process exits non-zero and the feed treats it
 * like any other renderer failure (the lock falls back to its own prompt,
 * fail-closed).
 *
 * Frame rate: NCZ_FEED_FPS (default 30) caps frames/second; the feed lowers it
 * for weak GPU classes.
 *
 * The lock requires the buffer to be exactly logical*scale in size and
 * little-endian XRGB8888 in memory (bytes B,G,R,X), rows top-down. glReadPixels
 * returns RGBA bottom-up, so each frame is swizzled and flipped on copy. */
#include "ncz_feed_render.h"
#include "lock_framesrc_proto.h"
#include "ncz_hack_shim.h"
#include "ncz_gl.h"
#include "ncz_gpu_guard.h"
#include "ncz_stats.h"
#include "gles3_harness_hooks.h"
#include "ncz_fbo_redirect.h"
#include "ncz_time_offset.h"

#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#ifndef EGL_PLATFORM_DEVICE_EXT
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#endif
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#define FR_DEFAULT_FPS   30
#define FR_T2_FPS        8      /* Apple T2 Macs: the lock saver runs slow by policy */
#define FR_WARM_FPS      15     /* thermal governor: FPS above the warm trip   */
#define FR_WARM_MC       75000  /* 75 C */
#define FR_HOT_MC        85000  /* 85 C: pause and hold the last frame          */
#define FR_HYST_MC       5000   /* hysteresis band to stop level flapping       */
#define FR_MAX_MSG       LFS_MAX_MSG
#define FR_READY_MS      5000
#define FR_CONNECT_TRIES 200

/* Distinct "no reading" sentinel; never a plausible millidegree value. A real
 * reading is compared with != FR_TEMP_NONE, not with a magic threshold. */
#define FR_TEMP_NONE     (-1000000)

static volatile sig_atomic_t g_run = 1;
static void on_term(int sig) { (void)sig; g_run = 0; }

/* ---- thermal governor ----------------------------------------------------
 *
 * The lock-hosted saver runs on laptops that can be driven to the hardware
 * throttling / power-cut limit by an unthrottled full-GPU screensaver (see the
 * MacBookPro16,1 power-cut history and /etc/ncz/power-cap.conf). Read the
 * on-die sensors through hwmon (never amdgpu debugfs) and:
 *   < warm            -> the configured frame rate, native render resolution
 *   warm .. hot       -> FR_WARM_FPS and half-resolution render
 *   >= hot            -> stop producing frames; the feed re-sends the last frame
 *                        so the lock keeps the last image (still fail-closed)
 * Every threshold, the divisor and the temperature can be overridden through
 * the environment so the headless test can drive the state machine without a
 * real thermal event. */

struct fr_therm {
    int warm_mc, hot_mc;
};

static int fr_read_mc(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return FR_TEMP_NONE;
    int v = FR_TEMP_NONE;
    if (fscanf(f, "%d", &v) != 1) v = FR_TEMP_NONE;
    fclose(f);
    return v;
}

static int fr_temp_mc_from(const char *base) {
    int best = FR_TEMP_NONE;
    const char *ov = getenv("NCZ_FEED_TEMP_MC");
    if (ov && *ov) return atoi(ov);
    const char *root = getenv("NCZ_FEED_THERMAL_ROOT");
    if (root && *root) base = root;
    DIR *d = opendir(base);
    if (!d) return FR_TEMP_NONE;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        char namep[512];
        snprintf(namep, sizeof namep, "%s/%s/name", base, e->d_name);
        FILE *nf = fopen(namep, "r");
        if (!nf) continue;
        char nm[64] = {0};
        if (!fgets(nm, sizeof nm, nf)) { fclose(nf); continue; }
        fclose(nf);
        /* Only trusted on-die CPU/GPU/SMC sensors. */
        if (!strstr(nm, "coretemp") && !strstr(nm, "amdgpu") &&
            !strstr(nm, "t2smc") && !strstr(nm, "k10temp") &&
            !strstr(nm, "acpitz"))
            continue;
        for (int i = 1; i <= 16; i++) {
            char tp[512];
            snprintf(tp, sizeof tp, "%s/%s/temp%d_input", base, e->d_name, i);
            int v = fr_read_mc(tp);
            if (v > best) best = v;
        }
    }
    closedir(d);
    return best;
}

static bool fr_has_t2(void) {
    const char *root = getenv("NCZ_FEED_THERMAL_ROOT");
    const char *base = (root && *root) ? root : "/sys/class/hwmon";
    DIR *d = opendir(base);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        char namep[512];
        snprintf(namep, sizeof namep, "%s/%s/name", base, e->d_name);
        FILE *nf = fopen(namep, "r");
        if (!nf) continue;
        char nm[64] = {0};
        if (!fgets(nm, sizeof nm, nf)) { fclose(nf); continue; }
        fclose(nf);
        if (strstr(nm, "t2smc")) { found = true; break; }
    }
    closedir(d);
    return found;
}


static void fr_log(const char *m) {
    fprintf(stderr, "ncz-feed-render: %s\n", m);
    fflush(stderr);
}
static void fr_die(const char *m) {
    fprintf(stderr, "ncz-feed-render: %s: %s\n", m, strerror(errno));
    exit(4);
}

/* ---- socket helpers ------------------------------------------------------ */

static int send_msg(int fd, const void *payload, uint32_t len,
                    uint16_t type, int passfd) {
    struct lfs_msg m = { .magic = LFS_MAGIC, .type = type, .flags = 0, .len = len };
    struct iovec iov[2] = { { &m, sizeof m }, { (void *)payload, len } };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = iov;
    msg.msg_iovlen = (len > 0) ? 2 : 1;
    char cbuf[CMSG_SPACE(sizeof(int))];
    if (passfd >= 0) {
        memset(cbuf, 0, sizeof cbuf);
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof cbuf;
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &passfd, sizeof(int));
    }
    ssize_t n = sendmsg(fd, &msg, MSG_NOSIGNAL);
    return (int)n;
}

/* Returns bytes, sets *fd_out to the received fd or -1, 0 on EAGAIN, -1 on EOF. */
static int recv_msg(int fd, void *buf, size_t buflen, int *fd_out) {
    struct iovec iov = { .iov_base = buf, .iov_len = buflen };
    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    char cbuf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    ssize_t n = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return -1;
    }
    if (n == 0) return -1;
    *fd_out = -1;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int f = -1;
            memcpy(&f, CMSG_DATA(c), sizeof(int));
            *fd_out = f;
        }
    }
    return (int)n;
}

static int connect_sock(const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (int i = 0; i < FR_CONNECT_TRIES; i++) {
        int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (fd < 0) fr_die("socket");
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0)
            return fd;
        close(fd);
        if (!g_run) break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    return -1;
}

/* ---- EGL ----------------------------------------------------------------- */

static EGLDisplay try_surfaceless(EGLint *maj, EGLint *minn) {
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                         EGL_DEFAULT_DISPLAY, NULL);
    if (d == EGL_NO_DISPLAY) return EGL_NO_DISPLAY;
    if (!eglInitialize(d, maj, minn)) { eglTerminate(d); return EGL_NO_DISPLAY; }
    return d;
}

static EGLDisplay try_device(EGLint *maj, EGLint *minn) {
    PFNEGLQUERYDEVICESEXTPROC query =
        (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    if (!query) return EGL_NO_DISPLAY;
    EGLDeviceEXT devs[8];
    EGLint n = 0;
    if (!query(8, devs, &n) || n < 1) return EGL_NO_DISPLAY;
    for (EGLint i = 0; i < n; i++) {
        EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
        if (d == EGL_NO_DISPLAY) continue;
        if (eglInitialize(d, maj, minn)) return d;
        eglTerminate(d);
    }
    return EGL_NO_DISPLAY;
}

/* ---- GBM fallback --------------------------------------------------------
 *
 * Some vendor EGL stacks (notably the CIX Mali blob on the O6N Sky1) do not
 * implement EGL_MESA_platform_surfaceless or EGL_EXT_platform_device, so both
 * of the paths above fail and the lock-hosted saver never renders a frame. The
 * CIX blob DOES advertise EGL_KHR_platform_gbm; opening a DRM node with libgbm
 * and handing the resulting gbm_device to EGL_PLATFORM_GBM_KHR gives a usable
 * display with no compositor and no surface. libgbm is resolved with dlopen so
 * this adds no build-time dependency and is inert on hosts that never reach it
 * (Mesa/AMD/Intel succeed at surfaceless first). */
struct fr_gbm {
    void *lib;
    int fd;
    void *dev;
    /* Set only once a device + fd have been handed over to EGL for good; the
     * per-node failure path destroys the LOCAL device and never publishes it
     * into g_gbm, so a failed try_gbm leaves active=0 and fr_gbm_cleanup() must
     * not touch dev/fd. Keeping the flag explicit makes that invariant hold even
     * if the loop is refactored later (no double destroy / double close). */
    int active;
    void *(*create_device)(int);
    void (*destroy_device)(void *);
};
static struct fr_gbm g_gbm = { .lib = NULL, .fd = -1, .dev = NULL, .active = 0,
                               .create_device = NULL, .destroy_device = NULL };

static EGLDisplay try_gbm_nodes(const char *const *nodes, EGLint *maj, EGLint *minn);

static EGLDisplay try_gbm(EGLint *maj, EGLint *minn) {
    static const char *const nodes[] = {
        "/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/renderD130",
        "/dev/dri/card0", "/dev/dri/card1", NULL,
    };
    return try_gbm_nodes(nodes, maj, minn);
}

static EGLDisplay try_gbm_nodes(const char *const *nodes, EGLint *maj, EGLint *minn) {
    if (!g_gbm.lib) {
        g_gbm.lib = dlopen("libgbm.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!g_gbm.lib) g_gbm.lib = dlopen("libgbm.so", RTLD_NOW | RTLD_LOCAL);
        if (!g_gbm.lib) return EGL_NO_DISPLAY;
        g_gbm.create_device = (void *(*)(int))dlsym(g_gbm.lib, "gbm_create_device");
        g_gbm.destroy_device = (void (*)(void *))dlsym(g_gbm.lib, "gbm_device_destroy");
        /* Both are needed: without destroy_device a created device could not be
         * released on a failed initialise, so refuse the path rather than leak. */
        if (!g_gbm.create_device || !g_gbm.destroy_device) return EGL_NO_DISPLAY;
    }
    for (int i = 0; nodes[i]; i++) {
        int fd = open(nodes[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        void *dev = g_gbm.create_device(fd);
        if (!dev) { close(fd); continue; }
        EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, dev, NULL);
        if (d != EGL_NO_DISPLAY && eglInitialize(d, maj, minn)) {
            g_gbm.fd = fd;
            g_gbm.dev = dev;
            g_gbm.active = 1;
            return d;
        }
        if (d != EGL_NO_DISPLAY) eglTerminate(d);
        g_gbm.destroy_device(dev);
        close(fd);
        /* Never publish a failed device into g_gbm. */
        g_gbm.dev = NULL;
        g_gbm.fd = -1;
        g_gbm.active = 0;
    }
    return EGL_NO_DISPLAY;
}

/* ---- render node selection ---------------------------------------------
 *
 * NCZ_FEED_RENDER_NODE=/dev/dri/renderDNNN or NCZ_FEED_RENDER_DRIVER=<kernel
 * driver> (i915, xe, amdgpu, nvidia, panthor, mali_kbase, ...) pins the saver
 * child to one GPU. The driver is resolved to a node through sysfs
 * (the device/driver link of each /sys/class/drm render node), never by index. Only this process is
 * affected: no PRIME variables are involved. */

/* Kernel driver of a render node ("renderD129" or a /dev/dri path), or "". */
static void fr_node_driver(const char *node, char *out, size_t outlen) {
    out[0] = 0;
    if (!node || !*node) return;
    const char *b = strrchr(node, '/');
    b = b ? b + 1 : node;
    const char *root = getenv("NCZ_FEED_DRM_ROOT");   /* test seam */
    char link[512], tgt[512];
    snprintf(link, sizeof link, "%s/sys/class/drm/%s/device/driver", root ? root : "", b);
    ssize_t n = readlink(link, tgt, sizeof tgt - 1);
    if (n <= 0) return;
    tgt[n] = 0;
    const char *d = strrchr(tgt, '/');
    snprintf(out, outlen, "%s", d ? d + 1 : tgt);
}

/* The render node to use, from NCZ_FEED_RENDER_NODE / NCZ_FEED_RENDER_DRIVER;
 * 0 = no request, 1 = found (path in out), -1 = requested but not present. */
static int fr_requested_node(char *out, size_t outlen) {
    const char *node = getenv("NCZ_FEED_RENDER_NODE");
    if (node && *node) { snprintf(out, outlen, "%s", node); return 1; }
    const char *drv = getenv("NCZ_FEED_RENDER_DRIVER");
    if (!drv || !*drv) return 0;
    const char *root = getenv("NCZ_FEED_DRM_ROOT");
    char dir[512];
    snprintf(dir, sizeof dir, "%s/sys/class/drm", root ? root : "");
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    char best[256] = "";
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "renderD", 7) != 0) continue;
        char got[128];
        fr_node_driver(e->d_name, got, sizeof got);
        if (strcmp(got, drv) == 0 && (!best[0] || strcmp(e->d_name, best) < 0))
            snprintf(best, sizeof best, "%s", e->d_name);
    }
    closedir(d);
    if (!best[0]) return -1;
    snprintf(out, outlen, "/dev/dri/%s", best);
    return 1;
}

/* EGL device platform on exactly this render node, else GBM on it. */
static EGLDisplay try_node(const char *node, EGLint *maj, EGLint *minn, const char **how) {
    PFNEGLQUERYDEVICESEXTPROC query =
        (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC qstr =
        (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    if (query && qstr) {
        EGLDeviceEXT devs[16];
        EGLint n = 0;
        if (query(16, devs, &n)) {
            for (EGLint i = 0; i < n; i++) {
                const char *rn = qstr(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT);
                if (!rn || strcmp(rn, node) != 0) continue;
                EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
                if (d != EGL_NO_DISPLAY && eglInitialize(d, maj, minn)) { *how = "device"; return d; }
                if (d != EGL_NO_DISPLAY) eglTerminate(d);
            }
        }
    }
    const char *nodes[] = { node, NULL };
    EGLDisplay d = try_gbm_nodes(nodes, maj, minn);
    if (d != EGL_NO_DISPLAY) *how = "gbm";
    return d;
}

/* The render node an initialised display really uses ("" if unknown). */
static void fr_display_node(EGLDisplay dpy, char *out, size_t outlen) {
    out[0] = 0;
    PFNEGLQUERYDISPLAYATTRIBEXTPROC qda =
        (PFNEGLQUERYDISPLAYATTRIBEXTPROC)eglGetProcAddress("eglQueryDisplayAttribEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC qstr =
        (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    EGLAttrib dev = 0;
    if (qda && qstr && qda(dpy, EGL_DEVICE_EXT, &dev) && dev) {
        const char *rn = qstr((EGLDeviceEXT)dev, EGL_DRM_RENDER_NODE_FILE_EXT);
        if (rn) snprintf(out, outlen, "%s", rn);
    }
}

/* Called exactly once, after eglTerminate, on the normal exit path. Only the
 * device/fd that try_gbm() successfully published are released (active). */
static void fr_gbm_cleanup(void) {
    if (g_gbm.active) {
        if (g_gbm.destroy_device && g_gbm.dev) g_gbm.destroy_device(g_gbm.dev);
        if (g_gbm.fd >= 0) close(g_gbm.fd);
    }
    g_gbm.dev = NULL;
    g_gbm.fd = -1;
    g_gbm.active = 0;
    g_gbm.create_device = NULL;
    g_gbm.destroy_device = NULL;
    if (g_gbm.lib) {
        /* The library was loaded by this module (g_gbm.lib set here) and this
         * runs once as the process is tearing down; a later dlclose cannot race
         * a live user because there is none. */
        dlclose(g_gbm.lib);
        g_gbm.lib = NULL;
    }
}

static EGLConfig choose_cfg(EGLDisplay d) {
    EGLint attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE,
    };
    EGLConfig cfg = NULL;
    EGLint num = 0;
    if (!eglChooseConfig(d, attrs, &cfg, 1, &num) || num < 1) return NULL;
    return cfg;
}

/* ---- off-screen framebuffer --------------------------------------------- */

static GLuint make_fbo(int w, int h) {
    GLuint tex = 0, fbo = 0, depth = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "ncz-feed-render: FBO incomplete (0x%x)\n", (unsigned)st);
        return 0;
    }
    glViewport(0, 0, w, h);
    return fbo;
}

/* ---- main ---------------------------------------------------------------- */

static unsigned char *fr_alloc_readback(uint32_t w, uint32_t h,
                                        size_t *out_stride, size_t *out_size) {
    /* Reject an overflowing stride*height before it can wrap: the buffer sizes
     * are derived from the lock's advertised geometry, so this is the one place
     * a hostile/degenerate size has to be rejected rather than multiplied. */
    if (w == 0 || h == 0 || w > (SIZE_MAX / 4u)) {
        *out_stride = 0;
        *out_size = 0;
        return NULL;
    }
    *out_stride = (size_t)w * 4u;
    if (h > (SIZE_MAX / *out_stride)) {
        *out_stride = 0;
        *out_size = 0;
        return NULL;
    }
    *out_size = (*out_stride) * (size_t)h;
    return malloc(*out_size);
}

/* Colour-only FBO the size of the lock buffer: the GPU flips (and, at a reduced
 * render size, scales) the saver's frame into it, so the CPU never touches a
 * pixel. */
static GLuint make_color_fbo(int w, int h) {
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return 0;
    }
    return fbo;
}

/* ---- GPU swizzle pass ----------------------------------------------------- */
static GLuint g_swz_prog, g_swz_vao;
static GLint g_swz_loc;

static GLuint swz_shader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

/* 1 when the pass is usable, 0 when the driver refused it (the caller then
 * falls back to the CPU R/B swap). */
static int swz_init(void) {
    static const char *vs =
        "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "  uv = vec2(p.x, 1.0 - p.y);\n"          /* top-down rows in the readback */
        "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D src;\n"
        "in vec2 uv;\n"
        "out vec4 o;\n"
        "void main() { o = vec4(texture(src, uv).bgr, 1.0); }\n";
    GLuint v = swz_shader(GL_VERTEX_SHADER, vs), f = swz_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint pr = glCreateProgram();
    glAttachShader(pr, v); glAttachShader(pr, f);
    glLinkProgram(pr);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(pr); return 0; }
    g_swz_prog = pr;
    g_swz_loc = glGetUniformLocation(pr, "src");
    glGenVertexArrays(1, &g_swz_vao);
    return 1;
}

/* Draw src_fbo's colour texture into dst_fbo (dw x dh), flipped and swizzled. */
static void swz_draw(GLuint src_fbo, GLuint dst_fbo, int dw, int dh, int scaled) {
    GLint prog, vao, active, tex0, samp0, vp[4], wm[4];
    GLboolean en_blend, en_depth, en_scissor, en_cull, en_stencil;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
    glGetIntegerv(GL_SAMPLER_BINDING, &samp0);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_COLOR_WRITEMASK, wm);
    en_blend = glIsEnabled(GL_BLEND); en_depth = glIsEnabled(GL_DEPTH_TEST);
    en_scissor = glIsEnabled(GL_SCISSOR_TEST); en_cull = glIsEnabled(GL_CULL_FACE);
    en_stencil = glIsEnabled(GL_STENCIL_TEST);

    GLint src_tex = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, src_fbo);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &src_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, dw, dh);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(g_swz_prog);
    glBindVertexArray(g_swz_vao);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)src_tex);
    GLint filt = scaled ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glUniform1i(g_swz_loc, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, (GLuint)tex0);
    glBindSampler(0, (GLuint)samp0);
    glActiveTexture((GLenum)active);
    glBindVertexArray((GLuint)vao);
    glUseProgram((GLuint)prog);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glColorMask(wm[0], wm[1], wm[2], wm[3]);
    if (en_blend) glEnable(GL_BLEND);
    if (en_depth) glEnable(GL_DEPTH_TEST);
    if (en_scissor) glEnable(GL_SCISSOR_TEST);
    if (en_cull) glEnable(GL_CULL_FACE);
    if (en_stencil) glEnable(GL_STENCIL_TEST);
}

/* Compute the effective per-frame rate from the configured fps, the warm-band
 * fps, and the current level. Always returns >= 1, so the caller can use it
 * directly in a 1.0 / fps_eff cadence budget without a divide-by-zero. The
 * implementation lives in ncz_feed_render_fps.c (kept separate so the unit
 * test does not have to link the entire EGL/GLES/GBM stack). */
extern int ncz_feed_effective_fps(int fps, int warm_fps, int level);
#define fr_effective_fps(fps, warm, level) ncz_feed_effective_fps((fps), (warm), (level))

/* Tear the swizzle program down before we lose the current GL context. The
     * VAO only exists when swz_init() succeeded; both names are 0 otherwise. */
static void swz_fini(void) {
    if (g_swz_vao) { glDeleteVertexArrays(1, &g_swz_vao); g_swz_vao = 0; }
    if (g_swz_prog) { glDeleteProgram(g_swz_prog); g_swz_prog = 0; }
}

static double fr_ms(const struct timespec *a, const struct timespec *b) {
    return (b->tv_sec - a->tv_sec) * 1e3 + (b->tv_nsec - a->tv_nsec) / 1e6;
}

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#define FR_RING 3        /* frame buffers in rotation (one in flight at the lock) */

int ncz_feed_render_main(struct xscreensaver_function_table *hack,
                         int argc, char **argv) {
    (void)argc; (void)argv;
    const char *spath = getenv("NCZ_FEED_SOCKET");
    if (!spath || !*spath) {
        fr_log("NCZ_FEED_SOCKET not set");
        return 4;
    }
    int fps = FR_DEFAULT_FPS;
    const char *f = getenv("NCZ_FEED_FPS");
    if (f && *f) { int v = atoi(f); if (v >= 1 && v <= 240) fps = v; }
    /* Apple T2 MacBooks: the AMD dGPU is always-on and already warm; cap lower
     * unless the caller pinned NCZ_FEED_FPS explicitly. */
    if (fr_has_t2() && !(f && *f) && fps > FR_T2_FPS) fps = FR_T2_FPS;

    struct fr_therm th = { .warm_mc = FR_WARM_MC, .hot_mc = FR_HOT_MC };
    const char *e;
    if ((e = getenv("NCZ_FEED_TEMP_WARM_MC")) && *e) th.warm_mc = atoi(e);
    if ((e = getenv("NCZ_FEED_TEMP_HOT_MC")) && *e) th.hot_mc = atoi(e);
    int warm_fps = FR_WARM_FPS;
    if ((e = getenv("NCZ_FEED_FPS_WARM")) && *e) { int v = atoi(e); if (v >= 0 && v <= 240) warm_fps = v; }
    int div_warm = 2;
    if ((e = getenv("NCZ_FEED_RENDER_DIV_WARM")) && *e) { int v = atoi(e); if (v >= 1 && v <= 8) div_warm = v; }
    /* Base render divisor: the lock saver renders at reduced resolution all the
     * time and lets the lock upscale (the feed sets this). */
    int base_div = 1;
    if ((e = getenv("NCZ_FEED_RENDER_DIV")) && *e) { int v = atoi(e); if (v >= 1 && v <= 8) base_div = v; }
    if (base_div > 1)
        fprintf(stderr, "ncz-feed-render: render divisor %d\n", base_div);
    if (warm_fps < 1) warm_fps = 1;
    if (div_warm < base_div) div_warm = base_div;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    int fd = connect_sock(spath);
    if (fd < 0) { fr_log("could not connect to the feed socket"); return 4; }

    /* HELLO first: the feed (and through it the lock) starts its watchdog the
     * moment HELLO is accepted, so the handshake must not wait on EGL. */
    struct lfs_hello he = { .abi = LFS_VERSION, .transports = LFS_TRANSPORT_SHM,
                            .n_outputs = 0 };
    if (send_msg(fd, &he, sizeof he, LFS_HELLO, -1) < 0) fr_die("send HELLO");

    char buf[FR_MAX_MSG];
    int wfd = -1;
    int n = 0;
    for (int i = 0; i < 500; i++) {   /* up to ~5 s for WELCOME */
        n = recv_msg(fd, buf, sizeof buf, &wfd);
        if (n > 0) break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    if (wfd >= 0) close(wfd);
    struct lfs_msg *m = (struct lfs_msg *)buf;
    if (n < (int)sizeof *m || m->magic != LFS_MAGIC || m->type != LFS_WELCOME) {
        fr_log("expected WELCOME");
        close(fd);
        return 4;
    }
    struct lfs_welcome *w = (struct lfs_welcome *)(buf + sizeof *m);
    struct lfs_output *outs = (struct lfs_output *)(w + 1);
    if (w->abi != LFS_VERSION || w->transport != LFS_TRANSPORT_SHM ||
        w->n_outputs < 1) {
        fr_log("unsupported WELCOME (abi/transport/outputs)");
        close(fd);
        return 4;
    }
    uint32_t bw = outs[0].width, bh = outs[0].height;
    if (bw < 2 || bh < 2 || bw > 16384 || bh > 16384) {
        fr_log("implausible output geometry");
        close(fd);
        return 4;
    }
    fprintf(stderr, "ncz-feed-render: WELCOME %ux%u scale %u/120\n",
            bw, bh, outs[0].scale_120);
    fflush(stderr);

    /* EGL: surfaceless first, then a device platform, then GBM (the CIX Mali
     * blob exposes neither of the first two but does expose EGL_KHR_platform_gbm). */
    EGLint maj = 0, minn = 0;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    const char *how = "surfaceless";
    char want[256];
    int req = fr_requested_node(want, sizeof want);
    if (req < 0) {
        fprintf(stderr, "ncz-feed-render: requested render driver '%s' has no render node; not rendering\n",
                getenv("NCZ_FEED_RENDER_DRIVER"));
        close(fd);
        return 4;
    }
    if (req > 0) {
        dpy = try_node(want, &maj, &minn, &how);
        if (dpy == EGL_NO_DISPLAY) {
            /* Fail loudly: never fall back to another GPU or to software. */
            fprintf(stderr, "ncz-feed-render: requested render node %s is not usable\n", want);
            close(fd);
            return 4;
        }
    }
    if (dpy == EGL_NO_DISPLAY) dpy = try_surfaceless(&maj, &minn);
    if (dpy == EGL_NO_DISPLAY) {
        dpy = try_device(&maj, &minn);
        how = "device";
    }
    if (dpy == EGL_NO_DISPLAY) {
        dpy = try_gbm(&maj, &minn);
        how = "gbm";
    }
    if (dpy == EGL_NO_DISPLAY) {
        fr_log("no usable off-screen EGL platform (surfaceless, device and gbm all failed)");
        close(fd);
        return 4;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { fr_log("eglBindAPI failed"); close(fd); return 4; }
    EGLConfig cfg = choose_cfg(dpy);
    if (!cfg) { fr_log("no GLES3 EGL config"); close(fd); return 4; }
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { fr_log("eglCreateContext failed"); close(fd); return 4; }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fr_log("eglMakeCurrent (offscreen) failed");
        close(fd);
        return 4;
    }
    {
        char node[256], drv[128];
        fr_display_node(dpy, node, sizeof node);
        if (!node[0] && g_gbm.active && req > 0) snprintf(node, sizeof node, "%s", want);
        fr_node_driver(node, drv, sizeof drv);
        fprintf(stderr, "ncz-feed-render: EGL %d.%d via %s, GL_RENDERER=%s, render node %s (%s)\n",
                maj, minn, how, (const char *)glGetString(GL_RENDERER),
                node[0] ? node : "unknown", drv[0] ? drv : "unknown driver");
    }
    fflush(stderr);

    /* Never software-render on a machine that has a GPU (same policy as the
     * on-screen harness). A headless test sets NCZ_ALLOW_SOFTWARE=1. */
    {
        const char *rend = (const char *)glGetString(GL_RENDERER);
        const char *allow = getenv("NCZ_ALLOW_SOFTWARE");
        if (!allow || !*allow) allow = getenv("NCZ_ALLOW_SOFTWARE_FALLBACK");
        /* Test seam: point the hardware-GPU probe at an empty root so a
         * software-only harness (no /dev/dri in the sandbox) can run. Real
         * hosts leave this unset and probe the real /sys. */
        const char *groot = getenv("NCZ_FEED_GUARD_ROOT");
        char gmsg[512];
        if (ncz_gpu_guard_check(groot ? groot : "", rend, allow, gmsg, sizeof gmsg)) {
            fprintf(stderr, "ncz-feed-render: guard: %s\n", gmsg);
            close(fd);
            return 3;
        }
    }

    /* Render resolution is reduced (base_div) on every frame; it may be reduced
     * further under thermal load. The memfd the lock receives is always the full
     * lock buffer (bw x bh). */
    int level = 0;                 /* 0 normal, 1 warm, 2 hot (paused)   */
    int div = base_div;            /* current render divisor             */
    uint32_t rw = bw, rh = bh;     /* current render size                */
    if (div > 1) {
        rw = bw / (uint32_t)div; if (rw < 2) rw = 2;
        rh = bh / (uint32_t)div; if (rh < 2) rh = 2;
    }
    GLuint fbo = make_fbo((int)rw, (int)rh);
    if (!fbo) { close(fd); return 4; }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    /* This FBO is the hack's "screen": a bind of framebuffer 0 lands here. */
    ncz_screen_fbo = fbo;

    /* Drive the hack exactly as gles3_harness.c does, but with our FBO current
     * and no surface. The globals belong to the shim/runtime. */
    static int dummy_display;
    g_harness_xscreen = &dummy_display;
    g_harness_egl_display = (void *)dpy;
    g_harness_egl_surface = (void *)EGL_NO_SURFACE;
    g_harness_width = (int)rw;
    g_harness_height = (int)rh;
    g_harness_initialized = 1;

    static ModeInfo mi;
    memset(&mi, 0, sizeof mi);
    xs_compat_apply_var_defaults(hack->opts, hack->defaults_str);
    xs_compat_apply_mode_defaults(&mi);
    init_GL(&mi);
    hack->init_cb(&mi);
    if (hack->reshape_cb) hack->reshape_cb(&mi, (int)rw, (int)rh);
    fr_log("hack initialised; streaming frames");

    size_t stride = (size_t)bw * 4u;   /* destination (lock) stride */
    size_t size = stride * (size_t)bh;
    size_t rstride = 0, rsize = 0;
    unsigned char *tmp = fr_alloc_readback(rw, rh, &rstride, &rsize);
    if (!tmp) { fr_log("readback allocation failed"); close(fd); return 4; }

    /* Suspended time (the lock pauses the saver behind its prompt) never
     * reaches the saver's clock. */
    ncz_time_offset_arm();

    /* Frame output path. Default: the GPU flips/scales into a lock-sized FBO and
     * glReadPixels writes BGRA straight into one of FR_RING reused memfds, so the
     * CPU copies nothing per pixel and no buffer is allocated per frame. The old
     * CPU path (readback + per-pixel swizzle/upscale into a fresh memfd) stays
     * behind NCZ_FEED_LEGACY_COPY=1 for comparison. */
    const char *lc = getenv("NCZ_FEED_LEGACY_COPY");
    int legacy = (lc && *lc == '1');
    GLuint ofbo = 0;
    GLenum rfmt = GL_RGBA;
    int swizzle = 0;   /* GPU writes .bgra so an RGBA readback is already XRGB8888 */
    int ring_fd[FR_RING];
    unsigned char *ring_map[FR_RING];
    for (int i = 0; i < FR_RING; i++) { ring_fd[i] = -1; ring_map[i] = NULL; }
    if (!legacy) {
        ofbo = make_color_fbo((int)bw, (int)bh);
        for (int i = 0; ofbo && i < FR_RING; i++) {
            ring_fd[i] = memfd_create("ncz-feed-frame", MFD_CLOEXEC);
            if (ring_fd[i] < 0 || ftruncate(ring_fd[i], (off_t)size) != 0) { legacy = 1; break; }
            void *mp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, ring_fd[i], 0);
            if (mp == MAP_FAILED) { legacy = 1; break; }
            ring_map[i] = mp;
        }
        if (!ofbo) legacy = 1;
        if (!legacy) {
            GLint f = 0, t = 0;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ofbo);
            glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &f);
            glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &t);
            if (f == GL_BGRA_EXT && t == GL_UNSIGNED_BYTE) rfmt = GL_BGRA_EXT;
        }
        const char *ns = getenv("NCZ_FEED_NO_SWIZZLE");
        if (!legacy && rfmt != GL_BGRA_EXT && !(ns && *ns == '1') && swz_init())
            swizzle = 1;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        while (glGetError() != GL_NO_ERROR) { /* drain */ }
    }
    fprintf(stderr, "ncz-feed-render: frame path %s (render %ux%u -> lock %ux%u)\n",
            legacy ? "cpu-copy (legacy)" : (rfmt == GL_BGRA_EXT ? "gpu-blit + BGRA readback"
                                       : swizzle ? "gpu-swizzle + RGBA readback"
                                                 : "gpu-blit + RGBA readback + cpu swap"),
            rw, rh, bw, bh);
    fflush(stderr);

    /* Per-stage cost, logged once a minute (wall ms per frame; CPU ms is this
     * thread's CPU time per frame, which is what heats a laptop). */
    double c_draw = 0, c_read = 0, c_conv = 0, c_send = 0, c_cpu = 0;
    unsigned c_n = 0;
    struct timespec c_t0, c_cpu0;
    clock_gettime(CLOCK_MONOTONIC, &c_t0);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c_cpu0);

    ncz_stats_start();
    uint32_t seq = 0;
    int rc = 0;
    struct timespec frame_start, therm_at;
    clock_gettime(CLOCK_MONOTONIC, &frame_start);
    therm_at = frame_start;

    while (g_run) {
        /* Anything from the feed before we render (STOP / close). */
        int rfd = -1;
        int rn = recv_msg(fd, buf, sizeof buf, &rfd);
        if (rfd >= 0) close(rfd);
        if (rn < 0) { fr_log("feed closed the connection"); break; }
        if (rn > 0 && rn >= (int)sizeof(struct lfs_msg)) {
            struct lfs_msg *cm = (struct lfs_msg *)buf;
            if (cm->magic == LFS_MAGIC && cm->type == LFS_STOP) {
                fr_log("STOP");
                break;
            }
        }

        /* ---- thermal governor: re-evaluate at most once a second ---- */
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double since = (now.tv_sec - therm_at.tv_sec) +
                           (now.tv_nsec - therm_at.tv_nsec) / 1e9;
            if (since >= 1.0) {
                therm_at = now;
                int mc = fr_temp_mc_from("/sys/class/hwmon");
                int nlevel = level;
                if (mc != FR_TEMP_NONE) {
                    if (level == 2)
                        nlevel = (mc >= th.hot_mc - FR_HYST_MC) ? 2
                                 : (mc >= th.warm_mc - FR_HYST_MC ? 1 : 0);
                    else if (level == 1)
                        nlevel = (mc >= th.hot_mc) ? 2
                                 : (mc < th.warm_mc - FR_HYST_MC ? 0 : 1);
                    else
                        nlevel = (mc >= th.hot_mc) ? 2
                                 : (mc >= th.warm_mc ? 1 : 0);
                }
                if (nlevel != level) {
                    level = nlevel;
                    fprintf(stderr, "ncz-feed-render: thermal %d mC -> level %d\n",
                            mc, level);
                    fflush(stderr);
                    int ndiv = (level == 0) ? base_div : div_warm;
                    if (ndiv != div) {
                        div = ndiv;
                        rw = bw / (uint32_t)div; if (rw < 2) rw = 2;
                        rh = bh / (uint32_t)div; if (rh < 2) rh = 2;
                        glDeleteFramebuffers(1, &fbo);
                        fbo = make_fbo((int)rw, (int)rh);
                        if (!fbo) { fr_log("FBO recreate failed"); g_run = 0; continue; }
                        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                        ncz_screen_fbo = fbo;
                        /* Allocate the new readback BEFORE freeing the old one,
                         * so a failed allocation leaves tmp valid and there is
                         * no window where tmp is freed but still used. */
                        size_t nrs = 0, nrsz = 0;
                        unsigned char *ntmp = fr_alloc_readback(rw, rh, &nrs, &nrsz);
                        if (!ntmp) { fr_log("readback allocation failed"); g_run = 0; continue; }
                        free(tmp);
                        tmp = ntmp; rstride = nrs; rsize = nrsz;
                        g_harness_width = (int)rw;
                        g_harness_height = (int)rh;
                        if (hack->reshape_cb) hack->reshape_cb(&mi, (int)rw, (int)rh);
                    }
                }
            }
        }

        if (level == 2) {
            /* Paused: produce nothing. The feed re-sends the last frame it
             * holds, so the lock keeps the last image and stays fail-closed. */
            struct timespec sl = { .tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 };
            nanosleep(&sl, NULL);
            continue;
        }
        /* The warm band only ever slows the saver down. fr_effective_fps() always
         * returns >= 1, so the cadence budget below cannot divide by zero even
         * if a future change relaxes the NCZ_FEED_FPS_WARM clamp. */
        int fps_eff = fr_effective_fps(fps, warm_fps, level);

        ncz_time_offset_frame((long long)(1e9 / (double)fps_eff));
        ncz_harness_attach_frame_size((int)rw, (int)rh);
        struct timespec ta, tb, tc, td, te;
        clock_gettime(CLOCK_MONOTONIC, &ta);
        /* Each frame starts and is read back on the screen FBO, whatever
         * framebuffer the hack left bound. */
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        hack->draw_cb(&mi);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        int slot = (int)(seq % FR_RING);
        if (!legacy && swizzle) {
            swz_draw(fbo, ofbo, (int)bw, (int)bh, !(rw == bw && rh == bh));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ofbo);
        } else if (!legacy) {
            /* Flip to top-down and scale to the lock buffer on the GPU. */
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ofbo);
            glBlitFramebuffer(0, 0, (GLint)rw, (GLint)rh, 0, (GLint)bh, (GLint)bw, 0,
                              GL_COLOR_BUFFER_BIT,
                              (rw == bw && rh == bh) ? GL_NEAREST : GL_LINEAR);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, ofbo);
        }
        clock_gettime(CLOCK_MONOTONIC, &tb);
        if (legacy)
            glReadPixels(0, 0, (GLsizei)rw, (GLsizei)rh, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        else {
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, (GLsizei)bw, (GLsizei)bh, rfmt, GL_UNSIGNED_BYTE, ring_map[slot]);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        }
        clock_gettime(CLOCK_MONOTONIC, &tc);
        /* Hacks routinely leave benign GL errors set; never make that fatal
         * (the on-screen harness logs and continues too). */
        GLenum e = glGetError();
        if (e != GL_NO_ERROR && seq < 3) {
            fprintf(stderr, "ncz-feed-render: GL error 0x%x on frame %u\n",
                    (unsigned)e, seq);
            fflush(stderr);
        }
        while (glGetError() != GL_NO_ERROR) { /* drain */ }

        int mfd = -1;
        if (!legacy) {
            mfd = ring_fd[slot];
            if (rfmt != GL_BGRA_EXT && !swizzle) {
                /* RGBA in memory -> XRGB8888 (bytes B,G,R,X): swap R and B. */
                uint32_t *px = (uint32_t *)ring_map[slot];
                size_t npx = (size_t)bw * bh;
                for (size_t i = 0; i < npx; i++) {
                    uint32_t v = px[i];
                    px[i] = (v & 0x0000ff00u) | ((v >> 16) & 0xffu) | ((v & 0xffu) << 16) | 0xff000000u;
                }
            }
        } else {
        mfd = memfd_create("ncz-feed-frame", MFD_CLOEXEC);
        if (mfd < 0) { fr_die("memfd_create"); }
        if (ftruncate(mfd, (off_t)size) != 0) fr_die("ftruncate");
        unsigned char *dst = mmap(NULL, size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, mfd, 0);
        if (dst == MAP_FAILED) fr_die("mmap");
        /* RGBA bottom-up -> XRGB8888 top-down (bytes B,G,R,X); nearest-neighbour
         * upscale when the render size is reduced. */
        for (uint32_t y = 0; y < bh; y++) {
            uint32_t sy = (uint32_t)(((uint64_t)y * rh) / bh);
            const unsigned char *srow = tmp + (size_t)(rh - 1 - sy) * rstride;
            unsigned char *d = dst + (size_t)y * stride;
            if (rw == bw) {
                for (uint32_t x = 0; x < bw; x++) {
                    const unsigned char *s = srow + (size_t)x * 4u;
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0xff;
                    d += 4;
                }
            } else {
                for (uint32_t x = 0; x < bw; x++) {
                    uint32_t sx = (uint32_t)(((uint64_t)x * rw) / bw);
                    const unsigned char *s = srow + (size_t)sx * 4u;
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0xff;
                    d += 4;
                }
            }
        }
        munmap(dst, size);
        }
        clock_gettime(CLOCK_MONOTONIC, &td);

        struct lfs_frame fm = {
            .seq = seq, .output_hash = outs[0].name_hash,
            .width = bw, .height = bh, .stride = (uint32_t)stride, .offset = 0,
            .format = LFS_FMT_XRGB8888, .modifier_lo = 0, .modifier_hi = 0,
        };
        if (send_msg(fd, &fm, sizeof fm, LFS_FRAME, mfd) < 0) {
            if (legacy) close(mfd);
            fr_log("feed closed during FRAME");
            g_run = 0;
            break;
        }
        if (legacy) close(mfd);
        seq++;
        ncz_stats_frame();

        /* One buffer in flight: wait for READY (or STOP / close). */
        int done = 0;
        for (int wait = 0; wait < FR_READY_MS && !done && g_run; wait++) {
            struct pollfd p = { .fd = fd, .events = POLLIN };
            int pr = poll(&p, 1, 1);
            if (pr > 0) {
                int xfd = -1;
                int xn = recv_msg(fd, buf, sizeof buf, &xfd);
                if (xfd >= 0) close(xfd);
                if (xn < 0) { fr_log("feed closed waiting for READY"); g_run = 0; break; }
                if (xn >= (int)sizeof(struct lfs_msg)) {
                    struct lfs_msg *rm = (struct lfs_msg *)buf;
                    if (rm->magic == LFS_MAGIC &&
                        (rm->type == LFS_READY || rm->type == LFS_STOP)) {
                        if (rm->type == LFS_STOP) g_run = 0;
                        done = 1;
                    }
                }
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &te);
        c_draw += fr_ms(&ta, &tb); c_read += fr_ms(&tb, &tc);
        c_conv += fr_ms(&tc, &td); c_send += fr_ms(&td, &te); c_n++;
        if (fr_ms(&c_t0, &te) >= 60000.0) {
            struct timespec cpu1;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu1);
            c_cpu = fr_ms(&c_cpu0, &cpu1);
            fprintf(stderr, "ncz-feed-render: cost per frame over %u frames: draw+blit %.1f ms, "
                    "readback %.1f ms, convert %.1f ms, send+ready %.1f ms; CPU %.1f ms/frame "
                    "(%.0f%% of a core at %.1f fps)\n",
                    c_n, c_draw / c_n, c_read / c_n, c_conv / c_n, c_send / c_n, c_cpu / c_n,
                    c_cpu / fr_ms(&c_t0, &te) * 100.0, c_n * 1000.0 / fr_ms(&c_t0, &te));
            fflush(stderr);
            c_draw = c_read = c_conv = c_send = 0; c_n = 0;
            c_t0 = te; c_cpu0 = cpu1;
        }

        /* Frame-rate cap (governor-adjusted): deadline pacing, one frame per
         * budget; a late frame starts the next period now (no catch-up burst). */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double budget = 1.0 / (double)fps_eff;
        double elapsed = (now.tv_sec - frame_start.tv_sec) +
                         (now.tv_nsec - frame_start.tv_nsec) / 1e9;
        if (elapsed < budget) {
            struct timespec sl = { .tv_sec = 0,
                .tv_nsec = (long)((budget - elapsed) * 1e9) };
            nanosleep(&sl, NULL);
            long ns = frame_start.tv_nsec + (long)(budget * 1e9);
            frame_start.tv_sec += ns / 1000000000L;
            frame_start.tv_nsec = ns % 1000000000L;
        } else {
            frame_start = now;
        }
    }

    if (hack->free_cb) hack->free_cb(&mi);
    for (int i = 0; i < FR_RING; i++) {
        if (ring_map[i]) munmap(ring_map[i], size);
        if (ring_fd[i] >= 0) close(ring_fd[i]);
    }
    free(tmp);
    close(fd);
    /* swizzle program + VAO are GL objects; free them while the context is
     * still current. Safe even if swz_init() never ran (names stay 0). */
    swz_fini();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    fr_gbm_cleanup();
    fprintf(stderr, "ncz-feed-render: exit after %u frames (rc=%d)\n", seq, rc);
    fflush(stderr);
    return rc;
}
