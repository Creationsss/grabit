// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/pixels.h"

#include "capture/capture.h"
#include "log.h"
#include "util/util.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

const char *pixels_shm_format_name(uint32_t f) {
	switch (f) {
	case WL_SHM_FORMAT_ARGB8888:
		return "ARGB8888";
	case WL_SHM_FORMAT_XRGB8888:
		return "XRGB8888";
	case WL_SHM_FORMAT_ABGR8888:
		return "ABGR8888";
	case WL_SHM_FORMAT_XBGR8888:
		return "XBGR8888";
	case WL_SHM_FORMAT_RGBA8888:
		return "RGBA8888";
	case WL_SHM_FORMAT_RGBX8888:
		return "RGBX8888";
	case WL_SHM_FORMAT_BGRA8888:
		return "BGRA8888";
	case WL_SHM_FORMAT_BGRX8888:
		return "BGRX8888";
	case WL_SHM_FORMAT_RGB565:
		return "RGB565";
	case WL_SHM_FORMAT_BGR888:
		return "BGR888";
	case WL_SHM_FORMAT_RGB888:
		return "RGB888";
	default:
		return NULL;
	}
}

int pixels_conv_src_bpp(enum pixels_conv conv) {
	return (conv == PIX_BGR24 || conv == PIX_RGB24) ? 3 : 4;
}

bool pixels_accept_format(uint32_t fmt, uint32_t *out_format, enum pixels_conv *out_conv) {
	switch (fmt) {
	case WL_SHM_FORMAT_XRGB8888:
	case WL_SHM_FORMAT_ARGB8888:
		*out_format = fmt;
		*out_conv = PIX_COPY;
		return true;
	case WL_SHM_FORMAT_XBGR8888:
	case WL_SHM_FORMAT_ABGR8888:
		*out_format = fmt;
		*out_conv = PIX_SWAP_RB;
		return true;
	case WL_SHM_FORMAT_BGR888:
		*out_format = WL_SHM_FORMAT_XRGB8888;
		*out_conv = PIX_BGR24;
		return true;
	case WL_SHM_FORMAT_RGB888:
		*out_format = WL_SHM_FORMAT_XRGB8888;
		*out_conv = PIX_RGB24;
		return true;
	default:
		return false;
	}
}

uint32_t pixels_resolved_format(uint32_t fmt, enum pixels_conv conv) {
	switch (conv) {
	case PIX_SWAP_RB:
		return fmt == WL_SHM_FORMAT_XBGR8888 ? WL_SHM_FORMAT_XRGB8888
											 : WL_SHM_FORMAT_ARGB8888;
	case PIX_BGR24:
	case PIX_RGB24:
		return WL_SHM_FORMAT_XRGB8888;
	case PIX_COPY:
	default:
		return fmt;
	}
}

void pixels_fmt_offer(struct pixels_fmt_pick *p, uint32_t fmt) {
	if (p->n < sizeof p->advertised / sizeof p->advertised[0]) {
		p->advertised[p->n++] = fmt;
	}
	if (p->chosen) return;
	uint32_t use = 0;
	enum pixels_conv conv = PIX_COPY;
	if (pixels_accept_format(fmt, &use, &conv)) {
		p->format = use;
		p->conv = conv;
		p->chosen = true;
	}
}

void pixels_copy(void *dst, int32_t dst_stride,
				 const void *src, int32_t src_stride,
				 int32_t w, int32_t h, enum pixels_conv conv, bool y_invert) {
	for (int32_t row = 0; row < h; row++) {
		int32_t src_row = y_invert ? (h - 1 - row) : row;
		const uint8_t *sp = (const uint8_t *)src + (size_t)src_row * (size_t)src_stride;
		uint8_t *dp = (uint8_t *)dst + (size_t)row * (size_t)dst_stride;
		switch (conv) {
		case PIX_SWAP_RB: {
			const uint32_t *s32 = (const uint32_t *)sp;
			uint32_t *d32 = (uint32_t *)dp;
			for (int32_t x = 0; x < w; x++) {
				uint32_t p = s32[x];
				d32[x] = (p & 0xff00ff00u) |
						 ((p & 0x00ff0000u) >> 16) |
						 ((p & 0x000000ffu) << 16);
			}
			break;
		}
		case PIX_BGR24: {
			uint32_t *d32 = (uint32_t *)dp;
			for (int32_t x = 0; x < w; x++) {
				const uint8_t *s = sp + (size_t)x * 3;
				d32[x] = 0xff000000u | ((uint32_t)s[0] << 16) |
						 ((uint32_t)s[1] << 8) | (uint32_t)s[2];
			}
			break;
		}
		case PIX_RGB24: {
			uint32_t *d32 = (uint32_t *)dp;
			for (int32_t x = 0; x < w; x++) {
				const uint8_t *s = sp + (size_t)x * 3;
				d32[x] = 0xff000000u | ((uint32_t)s[2] << 16) |
						 ((uint32_t)s[1] << 8) | (uint32_t)s[0];
			}
			break;
		}
		case PIX_COPY:
		default:
			memcpy(dp, sp, (size_t)w * 4);
			break;
		}
	}
}

int pixels_image_from_buf(struct image *out, const void *map, size_t map_size,
						  int32_t w, int32_t h, int32_t stride, uint32_t fmt,
						  enum pixels_conv conv, bool y_invert) {
	int32_t dst_stride = pixels_conv_src_bpp(conv) == 3 ? w * 4 : stride;
	size_t dst_size = pixels_conv_src_bpp(conv) == 3 ? (size_t)dst_stride * (size_t)h
													 : map_size;
	out->width = w;
	out->height = h;
	out->stride = dst_stride;
	out->format = pixels_resolved_format(fmt, conv);
	out->size = dst_size;
	out->bytes = malloc(dst_size);
	if (!out->bytes) return -1;
	pixels_copy(out->bytes, dst_stride, map, stride, w, h, conv, y_invert);
	return 0;
}

void pixels_log_advertised(const char *backend,
						   const uint32_t *advertised, size_t n) {
	log_error("%s: compositor advertised no supported shm format "
			  "(want XRGB8888/ARGB8888/XBGR8888/ABGR8888/BGR888/RGB888)",
			  backend);
	for (size_t i = 0; i < n; i++) {
		const char *name = pixels_shm_format_name(advertised[i]);
		log_error("  saw: %s (0x%08x)", name ? name : "unknown", advertised[i]);
	}
}

int pixels_shm_buf_alloc(struct wl_shm *shm, const char *tag,
						 int32_t w, int32_t h, int32_t stride, uint32_t format,
						 struct pixels_shm_buf *out) {
	memset(out, 0, sizeof *out);
	if (w <= 0 || h <= 0 || stride <= 0) return -1;
	size_t size = (size_t)stride * (size_t)h;

	int fd = grabit_shm_anon(tag, size);
	if (fd < 0) return -1;

	out->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (out->map == MAP_FAILED) {
		log_error("mmap(%zu): %s", size, strerror(errno));
		close(fd);
		out->map = NULL;
		return -1;
	}
	out->map_size = size;

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
	out->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, format);
	wl_shm_pool_destroy(pool);
	close(fd);
	if (!out->buffer) {
		munmap(out->map, size);
		out->map = NULL;
		return -1;
	}
	return 0;
}

void pixels_shm_buf_destroy(struct pixels_shm_buf *b) {
	if (!b || b->external) {
		if (b) memset(b, 0, sizeof *b);
		return;
	}
	if (b->buffer) wl_buffer_destroy(b->buffer);
	if (b->map) munmap(b->map, b->map_size);
	memset(b, 0, sizeof *b);
}

void pixels_pool_destroy(struct pixels_pool *p) {
	if (!p) return;
	if (p->backend_priv && p->backend_priv_destroy)
		p->backend_priv_destroy(p->backend_priv);
	if (p->buffer) wl_buffer_destroy(p->buffer);
	if (p->map) munmap(p->map, p->map_size);
	memset(p, 0, sizeof *p);
}

int pixels_pool_acquire(struct wl_shm *shm, const char *tag,
						struct pixels_pool *pool,
						int32_t w, int32_t h, int32_t stride, uint32_t format,
						struct pixels_shm_buf *out) {
	memset(out, 0, sizeof *out);

	if (pool && pool->buffer &&
		pool->width == w && pool->height == h &&
		pool->stride == stride && pool->format == format) {
		out->buffer = pool->buffer;
		out->map = pool->map;
		out->map_size = pool->map_size;
		out->external = true;
		return 0;
	}

	if (pixels_shm_buf_alloc(shm, tag, w, h, stride, format, out) != 0) return -1;

	if (pool) {
		if (pool->buffer) wl_buffer_destroy(pool->buffer);
		if (pool->map) munmap(pool->map, pool->map_size);
		pool->buffer = out->buffer;
		pool->map = out->map;
		pool->map_size = out->map_size;
		pool->width = w;
		pool->height = h;
		pool->stride = stride;
		pool->format = format;
		out->external = true;
	}
	return 0;
}

int pixels_wl_wait(struct wl_display *dpy, const int *status) {
	while (*status == 0) {
		if (wl_display_dispatch(dpy) < 0) {
			log_error("wl_display_dispatch: lost connection");
			return -1;
		}
	}
	return *status == 1 ? 0 : -1;
}
