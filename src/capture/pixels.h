// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_PIXELS_H
#define GRABIT_CAPTURE_PIXELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const char *pixels_shm_format_name(uint32_t fmt);

bool pixels_accept_format(uint32_t fmt, uint32_t *out_format, bool *out_swap_rb);

uint32_t pixels_resolved_format(uint32_t fmt, bool swap_rb);

void pixels_copy(void *dst, int32_t dst_stride,
				 const void *src, int32_t src_stride,
				 int32_t w, int32_t h, bool swap_rb, bool y_invert);

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
};

void pixels_pool_destroy(struct pixels_pool *p);

int pixels_pool_acquire(struct wl_shm *shm, const char *tag,
						struct pixels_pool *pool,
						int32_t w, int32_t h, int32_t stride, uint32_t format,
						struct pixels_shm_buf *out);

int pixels_wl_wait(struct wl_display *dpy, const int *status);

#endif
