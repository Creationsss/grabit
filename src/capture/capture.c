// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/capture.h"

#include "capture/backend.h"
#include "log.h"
#include "wl.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void image_free(struct image *img) {
	if (!img) return;
	free(img->bytes);
	memset(img, 0, sizeof *img);
}

enum capture_backend {
	CAP_NONE = 0,
	CAP_WLR = 1,
	CAP_EXT = 2,
};

static enum capture_backend pick_backend(struct grabit_wl_state *s) {
	bool have_wlr = s->screencopy_manager != NULL;
	bool have_ext = s->ext_copy_manager && s->ext_source_manager;
	const char *pref = getenv("GRABIT_CAPTURE_BACKEND");
	if (!pref || !pref[0]) pref = "auto";
	if (strcmp(pref, "wlr") == 0) {
		if (!have_wlr) {
			log_error("capture.backend=wlr but wlr-screencopy isn't advertised");
			return CAP_NONE;
		}
		return CAP_WLR;
	}
	if (strcmp(pref, "ext") == 0) {
		if (!have_ext) {
			log_error("capture.backend=ext but ext-image-copy isn't advertised");
			return CAP_NONE;
		}
		return CAP_EXT;
	}
	if (have_wlr) return CAP_WLR;
	if (have_ext) return CAP_EXT;
	return CAP_NONE;
}

static enum capture_backend resolve_backend(struct grabit_wl_state *s) {
	static bool logged;
	enum capture_backend b = pick_backend(s);
	if (!logged && b != CAP_NONE) {
		logged = true;
		log_debug("capture: using %s backend",
				  b == CAP_WLR ? "wlr-screencopy" : "ext-image-copy");
	}
	return b;
}

int capture_output_full(struct grabit_wl_state *s, struct grabit_output *o,
						bool overlay_cursor, struct image *out) {
	if (!s) return -1;
	switch (resolve_backend(s)) {
	case CAP_WLR:
		return grabit_wlr_capture_full(s, o, overlay_cursor, out);
	case CAP_EXT:
		return grabit_ext_capture_full(s, o, overlay_cursor, out);
	default:
		return -1;
	}
}

int capture_output_region_into(struct grabit_wl_state *s, struct grabit_output *o,
							   int32_t x, int32_t y, int32_t w, int32_t h,
							   bool overlay_cursor,
							   void *dst, int32_t dst_stride, int32_t dst_h,
							   uint32_t *out_format,
							   struct pixels_pool *cache) {
	if (!s) return -1;
	switch (resolve_backend(s)) {
	case CAP_WLR:
		return grabit_wlr_capture_region(s, o, x, y, w, h, overlay_cursor,
										 dst, dst_stride, dst_h, out_format, cache);
	case CAP_EXT:
		return grabit_ext_capture_region(s, o, x, y, w, h, overlay_cursor,
										 dst, dst_stride, dst_h, out_format, cache);
	default:
		return -1;
	}
}
