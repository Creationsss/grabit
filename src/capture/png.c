// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/save.h"

#include "log.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

static bool gpng_all_opaque(const unsigned char *data, int w, int h, int stride) {
	for (int y = 0; y < h; y++) {
		const uint32_t *line = (const uint32_t *)(data + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++)
			if ((line[x] >> 24) != 0xffu) return false;
	}
	return true;
}

static void gpng_pack_rgb(unsigned char *dst, const uint32_t *line, int w) {
	for (int x = 0; x < w; x++) {
		uint32_t px = line[x];
		dst[x * 3 + 0] = (unsigned char)((px >> 16) & 0xff);
		dst[x * 3 + 1] = (unsigned char)((px >> 8) & 0xff);
		dst[x * 3 + 2] = (unsigned char)(px & 0xff);
	}
}

static void gpng_pack_rgba(unsigned char *dst, const uint32_t *line, int w) {
	for (int x = 0; x < w; x++) {
		uint32_t px = line[x];
		unsigned a = (px >> 24) & 0xff;
		unsigned r = (px >> 16) & 0xff;
		unsigned g = (px >> 8) & 0xff;
		unsigned b = px & 0xff;
		if (a != 0 && a != 255) {
			r = (r * 255u + a / 2) / a;
			g = (g * 255u + a / 2) / a;
			b = (b * 255u + a / 2) / a;
			if (r > 255) r = 255;
			if (g > 255) g = 255;
			if (b > 255) b = 255;
		} else if (a == 0) {
			r = g = b = 0;
		}
		dst[x * 4 + 0] = (unsigned char)r;
		dst[x * 4 + 1] = (unsigned char)g;
		dst[x * 4 + 2] = (unsigned char)b;
		dst[x * 4 + 3] = (unsigned char)a;
	}
}

int grabit_save_png_surface(cairo_surface_t *surface, const char *path, int level) {
	if (!path) return -1;
	int w, h, stride;
	const unsigned char *src;
	if (grabit_surface_pixels(surface, "png", &w, &h, &stride, &src) != 0) return -1;

	volatile int lvl = level < 0 ? 0 : (level > 9 ? 9 : level);

	bool opaque = gpng_all_opaque(src, w, h, stride);
	int channels = opaque ? 3 : 4;

	FILE *f = fopen(path, "wb");
	if (!f) {
		log_error("png: open %s: %s", path, strerror(errno));
		return -1;
	}

	unsigned char *row = malloc((size_t)w * (size_t)channels);
	if (!row) {
		log_error("png: oom");
		fclose(f);
		return -1;
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!png || !info) {
		log_error("png: libpng init failed");
		if (png) png_destroy_write_struct(&png, info ? &info : NULL);
		free(row);
		fclose(f);
		return -1;
	}

	if (setjmp(png_jmpbuf(png))) {
		log_error("png: libpng error writing %s", path);
		png_destroy_write_struct(&png, &info);
		free(row);
		fclose(f);
		return -1;
	}

	png_init_io(png, f);
	png_set_compression_level(png, lvl);
	png_set_filter(png, 0, lvl >= 6 ? PNG_ALL_FILTERS : PNG_FILTER_UP);
	png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
				 opaque ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA,
				 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);

	for (int y = 0; y < h; y++) {
		const uint32_t *line = (const uint32_t *)(src + (size_t)y * (size_t)stride);
		if (opaque)
			gpng_pack_rgb(row, line, w);
		else
			gpng_pack_rgba(row, line, w);
		png_write_row(png, row);
	}

	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	free(row);

	if (fclose(f) != 0) {
		log_error("png: close %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}
