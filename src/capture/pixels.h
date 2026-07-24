// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_PIXELS_H
#define GRABIT_CAPTURE_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum pixels_conv {
	PIX_COPY = 0, // 4 bytes/px, already XRGB/ARGB order
	PIX_SWAP_RB,  // 4 bytes/px, swap R and B (X/ABGR8888)
	PIX_BGR24,	  // 3 bytes/px BGR888, expand to XRGB8888 (swaps R/B)
	PIX_RGB24,	  // 3 bytes/px RGB888, expand to XRGB8888
};

const char *pixels_shm_format_name(uint32_t fmt);

int pixels_conv_src_bpp(enum pixels_conv conv);

bool pixels_accept_format(uint32_t fmt, uint32_t *out_format, enum pixels_conv *out_conv);

uint32_t pixels_resolved_format(uint32_t fmt, enum pixels_conv conv);

struct pixels_fmt_pick {
	uint32_t advertised[16];
	size_t n;
	uint32_t format;
	enum pixels_conv conv;
	bool chosen;
};

void pixels_fmt_offer(struct pixels_fmt_pick *p, uint32_t fmt);

void pixels_copy(void *dst, int32_t dst_stride,
				 const void *src, int32_t src_stride,
				 int32_t w, int32_t h, enum pixels_conv conv, bool y_invert);

struct image;

int pixels_image_from_buf(struct image *out, const void *map, size_t map_size,
						  int32_t w, int32_t h, int32_t stride, uint32_t fmt,
						  enum pixels_conv conv, bool y_invert);

void pixels_log_advertised(const char *backend,
						   const uint32_t *advertised, size_t n);

struct wl_shm;
struct wl_buffer;
struct wl_display;

struct pixels_shm_buf {
	struct wl_buffer *buffer;
	void *map;
	size_t map_size;
	bool external;
};

int pixels_shm_buf_alloc(struct wl_shm *shm, const char *tag,
						 int32_t w, int32_t h, int32_t stride, uint32_t format,
						 struct pixels_shm_buf *out);
void pixels_shm_buf_destroy(struct pixels_shm_buf *b);

struct pixels_pool {
	struct wl_buffer *buffer;
	void *map;
	size_t map_size;
	int32_t width, height, stride;
	uint32_t format;
	void *backend_priv;
	void (*backend_priv_destroy)(void *priv);
};

void pixels_pool_destroy(struct pixels_pool *p);

int pixels_pool_acquire(struct wl_shm *shm, const char *tag,
						struct pixels_pool *pool,
						int32_t w, int32_t h, int32_t stride, uint32_t format,
						struct pixels_shm_buf *out);

int pixels_wl_wait(struct wl_display *dpy, const int *status);

#endif
