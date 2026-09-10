// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/png_hdr.h"

#include "capture/capture.h"
#include "capture/pixels.h"
#include "log.h"
#include "util/util.h"
#include "wl/color.h"

#include "color-management-v1-client-protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>
#include <setjmp.h>

#define CICP_PRIMARIES_BT709 1
#define CICP_PRIMARIES_BT2020 9
#define CICP_PRIMARIES_P3 12
#define CICP_TF_BT709 1
#define CICP_TF_GAMMA22 4
#define CICP_TF_SRGB 13
#define CICP_TF_PQ 16
#define CICP_TF_HLG 18
#define CICP_MATRIX_IDENTITY 0

static int cicp_primaries(uint32_t named) {
	switch (named) {
	case WP_COLOR_MANAGER_V1_PRIMARIES_SRGB:
		return CICP_PRIMARIES_BT709;
	case WP_COLOR_MANAGER_V1_PRIMARIES_BT2020:
		return CICP_PRIMARIES_BT2020;
	case WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3:
		return CICP_PRIMARIES_P3;
	default:
		return -1;
	}
}

static int cicp_transfer(uint32_t named) {
	switch (named) {
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ:
		return CICP_TF_PQ;
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG:
		return CICP_TF_HLG;
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB:
		return CICP_TF_SRGB;
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886:
		return CICP_TF_BT709;
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22:
		return CICP_TF_GAMMA22;
	default:
		return -1;
	}
}

static uint16_t up10(uint32_t v) {
	return (uint16_t)((v << 6) | (v >> 4));
}

static void pack_row(uint16_t *dst, const uint32_t *line, int w, bool swap_rb) {
	for (int x = 0; x < w; x++) {
		uint32_t p = line[x];
		uint32_t a = (p >> 20) & 0x3ffu;
		uint32_t b = (p >> 10) & 0x3ffu;
		uint32_t c = p & 0x3ffu;
		dst[x * 3 + 0] = up10(swap_rb ? c : a);
		dst[x * 3 + 1] = up10(b);
		dst[x * 3 + 2] = up10(swap_rb ? a : c);
	}
}

static double xy(int32_t v) {
	return (double)v / 1000000.0;
}

static void apply_metadata(png_structp png, png_infop info,
						   const struct image *img) {
	if (!img->have_color) return;
	const struct grabit_colorimetry *c = &img->color;

	int pri = cicp_primaries(c->primaries_named);
	int tf = cicp_transfer(c->tf_named);
	if (pri >= 0 && tf >= 0) {
		png_set_cICP(png, info, (png_byte)pri, (png_byte)tf,
					 CICP_MATRIX_IDENTITY, 1);
	} else {
		log_debug("png: no cICP mapping for primaries=%u transfer=%u",
				  c->primaries_named, c->tf_named);
	}

	if (c->have_target) {
		png_set_mDCV(png, info, xy(c->target.w.x), xy(c->target.w.y),
					 xy(c->target.r.x), xy(c->target.r.y), xy(c->target.g.x),
					 xy(c->target.g.y), xy(c->target.b.x), xy(c->target.b.y),
					 (double)c->target_max_lum,
					 (double)c->target_min_lum / 10000.0);
	}

	if (c->max_cll || c->max_fall)
		png_set_cLLI(png, info, (double)c->max_cll, (double)c->max_fall);
}

int grabit_save_png_hdr(const struct image *img, int32_t x, int32_t y,
						int32_t w, int32_t h, const char *path, int level) {
	bool swap_rb = false;
	if (!img || !path || !img->bytes) return -1;
	if (!pixels_is_10bit(img->format, &swap_rb)) return -1;
	if (x < 0 || y < 0 || w <= 0 || h <= 0) return -1;
	if (x + w > img->width || y + h > img->height) return -1;
	if (w > GRABIT_MAX_PIXEL_SIDE || h > GRABIT_MAX_PIXEL_SIDE) {
		log_error("png: %dx%d exceeds the maximum image size", w, h);
		return -1;
	}

	volatile int lvl = level < 0 ? 0 : (level > 9 ? 9 : level);

	FILE *f = fopen(path, "wb");
	if (!f) {
		log_error("png: open %s: %s", path, strerror(errno));
		return -1;
	}

	uint16_t *row = malloc((size_t)w * 3 * sizeof *row);
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
	png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 16,
				 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	apply_metadata(png, info, img);
	png_write_info(png, info);
	png_set_swap(png);

	const unsigned char *src = img->bytes;
	for (int32_t row_y = 0; row_y < h; row_y++) {
		const uint32_t *line =
			(const uint32_t *)(src + (size_t)(y + row_y) * (size_t)img->stride);
		pack_row(row, line + x, w, swap_rb);
		png_write_row(png, (png_bytep)row);
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
