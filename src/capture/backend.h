// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_BACKEND_H
#define GRABIT_CAPTURE_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct grabit_wl_state;
struct grabit_output;
struct image;
struct pixels_pool;

int grabit_wlr_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							bool overlay_cursor,
							struct image *out);
int grabit_wlr_capture_many(struct grabit_wl_state *s, struct grabit_output *const *outs,
							size_t n, bool overlay_cursor, struct image *out);
int grabit_wlr_capture_region(struct grabit_wl_state *s, struct grabit_output *o,
							  int32_t x, int32_t y, int32_t w, int32_t h,
							  bool overlay_cursor,
							  void *dst, int32_t dst_stride, int32_t dst_h,
							  uint32_t *out_format,
							  struct pixels_pool *cache);

int grabit_ext_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							bool overlay_cursor,
							struct image *out);
int grabit_ext_capture_region(struct grabit_wl_state *s, struct grabit_output *o,
							  int32_t x, int32_t y, int32_t w, int32_t h,
							  bool overlay_cursor,
							  void *dst, int32_t dst_stride, int32_t dst_h,
							  uint32_t *out_format,
							  struct pixels_pool *cache);

bool grabit_kwin_screenshot_available(void);
int grabit_kwin_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							 bool overlay_cursor,
							 struct image *out);

#endif
