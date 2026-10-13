/*
 * lock_framesrc_proto.h - wire protocol between a session-lock client and an
 * out-of-process frame source (a screensaver).
 *
 * SPDX-License-Identifier: MIT
 *
 * While a session is locked, ext-session-lock-v1 presents exactly one client's
 * surface, so a separate screensaver process cannot draw underneath it. A
 * frame source instead renders off-screen and hands the lock client a buffer
 * file descriptor, which the lock imports and presents on its own lock surface.
 * The lock never executes frame-source code, so a fault there can only make the
 * lock fall back to its own prompt.
 *
 * Transport: AF_UNIX SOCK_SEQPACKET. Message boundaries are preserved and
 * SCM_RIGHTS ancillary data arrives with its message, so one recvmsg() yields
 * one message and its file descriptor. All integers are little-endian; this
 * protocol is only used on the same host.
 *
 * This header is dependency-free and is shared verbatim by the lock client and
 * by frame sources.
 */
#ifndef LOCK_FRAMESRC_PROTO_H
#define LOCK_FRAMESRC_PROTO_H

#include <stdint.h>

#define LFS_MAGIC        0x4c465331u   /* "LFS1" */
#define LFS_VERSION      1u

/* No payload may exceed this. Bounds every allocation the host makes from a
 * message it has not yet validated. */
#define LFS_MAX_MSG      4096u

/* Transport bitmask exchanged in HELLO / WELCOME. */
#define LFS_TRANSPORT_SHM    0x1u      /* memfd / wl_shm buffer (always usable) */
#define LFS_TRANSPORT_DMABUF 0x2u      /* linux-dmabuf buffer (needs the global) */

/* Capability bits carried in struct lfs_msg.flags. A lock that understands
 * LFS_HOLD (and keeps the last frame when the source freezes) sets
 * LFS_FLAG_HOLD in the WELCOME it sends. Locks built before HOLD existed send
 * flags = 0. A frame source MUST NOT send LFS_HOLD unless the WELCOME it just
 * received carried LFS_FLAG_HOLD: a new source then degrades safely (runs at
 * the low lock frame rate under the thermal governor, never freezing) against
 * an old lock instead of tripping its "unknown message type" fault loop on
 * every restart. This bit is only meaningful on a lock -> saver message. */
#define LFS_FLAG_HOLD   0x0001u

enum lfs_type {
    LFS_HELLO   = 1,   /* saver -> lock : who I am, what I can send */
    LFS_FRAME   = 2,   /* saver -> lock : one buffer + fd */
    LFS_BYE     = 3,   /* saver -> lock : I am done (clean) */
    LFS_HOLD    = 4,   /* saver -> lock : freeze here, keep the last frame */

    LFS_WELCOME = 0x80,/* lock -> saver : accepted; here is the geometry to use */
    LFS_READY   = 0x81,/* lock -> saver : previous frame consumed; send the next */
    LFS_STOP    = 0x82,/* lock -> saver : stop and exit (input arrived / unlocking) */
};

/* Buffer pixel formats. LFS_FMT_TOKEN covers both transports and maps to
 * WL_SHM_FORMAT_{XRGB,ARGB}8888 and DRM_FORMAT_{XRGB,ARGB}8888. */
enum lfs_format {
    LFS_FMT_ARGB8888 = 0,
    LFS_FMT_XRGB8888 = 1,
};

struct lfs_msg {
    uint32_t magic;
    uint16_t type;
    uint16_t flags;
    uint32_t len;
};

struct lfs_output {
    uint32_t name_hash;   /* stable hash of the output name (see lfs_name_hash) */
    uint32_t width;       /* BUFFER width  = logical width  * scale */
    uint32_t height;      /* BUFFER height = logical height * scale */
    uint32_t scale_120;   /* integer scale * 120 (120 = 1.0) */
};

struct lfs_hello {
    uint32_t abi;
    uint32_t transports;  /* LFS_TRANSPORT_* the sender supports */
    uint32_t n_outputs;
};

struct lfs_welcome {
    uint32_t abi;
    uint32_t transport;   /* the one transport the lock will use */
    uint32_t n_outputs;
};

struct lfs_frame {
    uint32_t seq;
    uint32_t output_hash;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t offset;
    uint32_t format;      /* enum lfs_format */
    uint32_t modifier_lo; /* DRM format modifier, low 32 bits (0 for SHM) */
    uint32_t modifier_hi; /* DRM format modifier, high 32 bits (0 for SHM) */
};

struct lfs_ack {
    uint32_t seq;
};

/* FNV-1a over the output name. The exact function is part of the ABI: both
 * sides must hash identically. An un-named output uses its index string. */
static inline uint32_t lfs_name_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

#endif /* LOCK_FRAMESRC_PROTO_H */
