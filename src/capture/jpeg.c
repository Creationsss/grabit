// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/pixels.h"
#include "capture/save.h"
#include "log.h"

#ifdef HAVE_JPEG

#include <cairo/cairo.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

int grabit_save_jpeg_surface(cairo_surface_t *surface, const char *path, int quality) {
	if (!path) return -1;
	int w, h, stride;
	const unsigned char *src;
	if (grabit_surface_pixels(surface, "jpeg", &w, &h, &stride, &src) != 0) return -1;

	if (quality < 1) quality = 1;
	if (quality > 100) quality = 100;

	FILE *f = fopen(path, "wb");
	if (!f) {
		log_error("jpeg: open %s: %s", path, strerror(errno));
		return -1;
	}

	unsigned char *row = malloc((size_t)w * 3);
	if (!row) {
		fclose(f);
		return -1;
	}

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, f);
	cinfo.image_width = (JDIMENSION)w;
	cinfo.image_height = (JDIMENSION)h;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	for (int y = 0; y < h; y++) {
		const uint32_t *line = (const uint32_t *)(src + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			uint32_t px = line[x];
			row[x * 3 + 0] = (unsigned char)((px >> 16) & 0xff);
			row[x * 3 + 1] = (unsigned char)((px >> 8) & 0xff);
			row[x * 3 + 2] = (unsigned char)(px & 0xff);
		}
		JSAMPROW rows[1] = {row};
		jpeg_write_scanlines(&cinfo, rows, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	free(row);
	if (fclose(f) != 0) {
		log_error("jpeg: close %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

cairo_surface_t *grabit_load_jpeg_surface(const char *path, const char *tag) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		log_error("%s: open %s: %s", tag, path, strerror(errno));
		return NULL;
	}

	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, f);
	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		log_error("%s: %s is not a jpeg", tag, path);
		jpeg_destroy_decompress(&cinfo);
		fclose(f);
		return NULL;
	}
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	int w = (int)cinfo.output_width;
	int h = (int)cinfo.output_height;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		log_error("%s: surface %dx%d: %s", tag, w, h,
				  cairo_status_to_string(cairo_surface_status(surf)));
		cairo_surface_destroy(surf);
		jpeg_abort_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(f);
		return NULL;
	}

	unsigned char *base = cairo_image_surface_get_data(surf);
	int stride = cairo_image_surface_get_stride(surf);
	JSAMPARRAY rows = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE,
												 (JDIMENSION)cinfo.output_width * 3, 1);
	while (cinfo.output_scanline < cinfo.output_height) {
		JDIMENSION y = cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, rows, 1);
		pixels_copy(base + (size_t)y * (size_t)stride, stride, rows[0], 0, w, 1,
					PIX_BGR24, false);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(f);
	cairo_surface_mark_dirty(surf);
	return surf;
}

#else

cairo_surface_t *grabit_load_jpeg_surface(const char *path, const char *tag) {
	(void)path;
	(void)tag;
	log_error("jpeg: not compiled in (rebuild with libjpeg headers)");
	return NULL;
}

int grabit_save_jpeg_surface(cairo_surface_t *surface, const char *path, int quality) {
	(void)surface;
	(void)path;
	(void)quality;
	log_error("jpeg: not compiled in (rebuild with libjpeg headers)");
	return -1;
}

#endif
