// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/capture.h"

#include "capture/backend.h"
#include "wl.h"

#include <stdlib.h>
#include <string.h>

void image_free(struct image *img) {
	if (!img) return;
	free(img->bytes);
	memset(img, 0, sizeof *img);
}

int capture_output_full(struct grabit_wl_state *s, struct grabit_output *o,
						struct image *out) {
	if (!s) return -1;
	if (s->screencopy_manager) return grabit_wlr_capture_full(s, o, out);
	if (s->ext_copy_manager && s->ext_source_manager)
		return grabit_ext_capture_full(s, o, out);
	return -1;
}

int capture_output_region_into(struct grabit_wl_state *s, struct grabit_output *o,
							   int32_t x, int32_t y, int32_t w, int32_t h,
							   bool overlay_cursor,
							   void *dst, int32_t dst_stride, int32_t dst_h,
							   uint32_t *out_format,
							   struct pixels_pool *cache) {
	if (!s) return -1;
	if (s->screencopy_manager)
		return grabit_wlr_capture_region(s, o, x, y, w, h, overlay_cursor,
										 dst, dst_stride, dst_h, out_format, cache);
	if (s->ext_copy_manager && s->ext_source_manager)
		return grabit_ext_capture_region(s, o, x, y, w, h, overlay_cursor,
										 dst, dst_stride, dst_h, out_format, cache);
	return -1;
}
