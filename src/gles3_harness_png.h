/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_harness_png.h — write a real PNG file from raw top-down RGBA.
 *
 * Purpose: the harness's NCZ_FRAME_DUMP path previously fwrite()'d raw
 * RGBA bytes under a .png extension, which made every existing
 * "evidence PNG" unopenable. This helper writes a real PNG using
 * libpng so the captures are inspectable.
 *
 * Layout: the source buffer is assumed TOP-DOWN RGBA8 (the harness
 * already flips GL's bottom-left origin into top-down before calling).
 *
 * No-deps version: this file uses libpng (dep_png in meson.build,
 * already pulled in by common_gles3_deps).
 */
#ifndef NCZ_GLES3_HARNESS_PNG_H
#define NCZ_GLES3_HARNESS_PNG_H

#include <png.h>
#include <stdio.h>
#include <stdlib.h>

/* Returns 0 on success, non-zero on failure (writes a diagnostic to
 * stderr). The caller does NOT need to free anything. */
static int ncz_write_png_rgba(const char *path, const unsigned char *rgba,
                              unsigned int width, unsigned int height) {
    if (!path || !rgba || width == 0 || height == 0) {
        fprintf(stderr, "[png] bad args to ncz_write_png_rgba\n");
        return 1;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) {
        fprintf(stderr, "[png] png_create_write_struct failed\n");
        return 2;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fprintf(stderr, "[png] png_create_info_struct failed\n");
        return 3;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fprintf(stderr, "[png] libpng longjmp error\n");
        return 4;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[png] cannot open %s\n", path);
        png_destroy_write_struct(&png, &info);
        return 5;
    }
    png_init_io(png, fp);
    /* 8-bit RGBA, no interlace. PNG colour-type 6 = RGBA, channels 4. */
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    /* Write one row at a time; png_bytep* row pointer array. */
    png_bytep *rows = malloc(sizeof(png_bytep) * height);
    if (!rows) {
        fclose(fp); png_destroy_write_struct(&png, &info);
        fprintf(stderr, "[png] row pointer malloc failed\n");
        return 6;
    }
    for (unsigned int y = 0; y < height; y++) {
        rows[y] = (png_bytep)(rgba + (size_t)y * width * 4);
    }
    png_write_image(png, rows);
    png_write_end(png, NULL);
    free(rows);
    fclose(fp);
    png_destroy_write_struct(&png, &info);
    return 0;
}

#endif /* NCZ_GLES3_HARNESS_PNG_H */
