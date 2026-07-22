// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/backend.h"
#include "capture/capture.h"
#include "capture/pixels.h"

#include "log.h"
#include "util/util.h"
#include "wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

struct sc_state {
	struct grabit_wl_state *wls;
	struct zwlr_screencopy_frame_v1 *frame;
	struct pixels_shm_buf buf;

	int32_t width;
	int32_t height;
	int32_t stride;
	struct pixels_fmt_pick fmt;
	bool y_invert;

	struct pixels_pool *pool;

	int status;
};

static void sc_buffer(void *data, struct zwlr_screencopy_frame_v1 *f,
					  uint32_t format, uint32_t w, uint32_t h, uint32_t stride) {
	(void)f;
	struct sc_state *c = data;

	const char *fname = pixels_shm_format_name(format);
	log_debug("wlr-screencopy: buffer format=%s (0x%08x) %ux%u stride=%u",
			  fname ? fname : "?", format, w, h, stride);

	bool had_format = c->fmt.chosen;
	pixels_fmt_offer(&c->fmt, format);

	if (c->buf.buffer || had_format || !c->fmt.chosen) return;

	if (w == 0 || h == 0 || w > GRABIT_MAX_PIXEL_SIDE || h > GRABIT_MAX_PIXEL_SIDE ||
		stride < (uint32_t)w * 4u) {
		log_error("wlr-screencopy: bogus geometry %ux%u stride=%u", w, h, stride);
		c->status = -1;
		return;
	}
	if (h > 0 && stride > SIZE_MAX / (size_t)h) {
		log_error("wlr-screencopy: %ux%u stride=%u overflows", w, h, stride);
		c->status = -1;
		return;
	}

	c->width = (int32_t)w;
	c->height = (int32_t)h;
	c->stride = (int32_t)stride;

	if (pixels_pool_acquire(c->wls->shm, "grabit-screencopy", c->pool,
							c->width, c->height, c->stride, c->fmt.format,
							&c->buf) != 0) {
		c->status = -1;
	}
}

static void sc_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *f,
							uint32_t fmt, uint32_t w, uint32_t h) {
	(void)data;
	(void)f;
	(void)fmt;
	(void)w;
	(void)h;
}

static void sc_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *f) {
	struct sc_state *c = data;
	if (!c->buf.buffer) {
		pixels_log_advertised("wlr-screencopy", c->fmt.advertised, c->fmt.n);
		c->status = -1;
		return;
	}
	zwlr_screencopy_frame_v1_copy(f, c->buf.buffer);
}

static void sc_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {
	(void)f;
	((struct sc_state *)data)->y_invert =
		(flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void sc_damage(void *data, struct zwlr_screencopy_frame_v1 *f,
					  uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
	(void)data;
	(void)f;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void sc_ready(void *data, struct zwlr_screencopy_frame_v1 *f,
					 uint32_t hi, uint32_t lo, uint32_t nsec) {
	(void)f;
	(void)hi;
	(void)lo;
	(void)nsec;
	((struct sc_state *)data)->status = 1;
}

static void sc_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
	(void)f;
	struct sc_state *c = data;
	c->status = -1;
	log_error("wlr-screencopy: compositor reported capture failure");
}

static const struct zwlr_screencopy_frame_v1_listener sc_listener = {
	.buffer = sc_buffer,
	.flags = sc_flags,
	.ready = sc_ready,
	.failed = sc_failed,
	.damage = sc_damage,
	.linux_dmabuf = sc_linux_dmabuf,
	.buffer_done = sc_buffer_done,
};

static void cleanup_state(struct sc_state *c) {
	if (c->frame) zwlr_screencopy_frame_v1_destroy(c->frame);
	pixels_shm_buf_destroy(&c->buf);
}

int grabit_wlr_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							bool overlay_cursor, struct image *out) {
	return grabit_wlr_capture_many(s, &o, 1, overlay_cursor, out);
}

int grabit_wlr_capture_many(struct grabit_wl_state *s, struct grabit_output *const *outs,
							size_t n, bool overlay_cursor, struct image *out) {
	if (!s || !s->screencopy_manager || !outs || !out || n == 0) return -1;

	struct sc_state *cs = calloc(n, sizeof *cs);
	if (!cs) {
		log_error("screencopy: oom");
		return -1;
	}

	int rc = 0;
	for (size_t i = 0; i < n; i++) {
		memset(&out[i], 0, sizeof out[i]);
		cs[i].wls = s;
		if (!outs[i] || outs[i]->dead || !outs[i]->wl_output) {
			cs[i].status = -1;
			rc = -1;
			continue;
		}
		cs[i].frame = zwlr_screencopy_manager_v1_capture_output(
			s->screencopy_manager, overlay_cursor ? 1 : 0, outs[i]->wl_output);
		if (!cs[i].frame) {
			log_error("zwlr_screencopy_manager_v1_capture_output: NULL");
			cs[i].status = -1;
			rc = -1;
			continue;
		}
		zwlr_screencopy_frame_v1_add_listener(cs[i].frame, &sc_listener, &cs[i]);
	}

	while (rc == 0) {
		bool pending = false;
		for (size_t i = 0; i < n; i++)
			if (cs[i].status == 0) pending = true;
		if (!pending) break;
		if (wl_display_dispatch(s->display) < 0) {
			log_error("wl_display_dispatch: lost connection");
			rc = -1;
		}
	}

	for (size_t i = 0; i < n; i++) {
		if (rc == 0 && cs[i].status == 1 && cs[i].buf.map) {
			if (pixels_image_from_buf(&out[i], cs[i].buf.map, cs[i].buf.map_size,
									  cs[i].width, cs[i].height, cs[i].stride,
									  cs[i].fmt.format, cs[i].fmt.swap_rb,
									  cs[i].y_invert) != 0)
				rc = -1;
		} else {
			rc = -1;
		}
		cleanup_state(&cs[i]);
	}

	free(cs);
	if (rc != 0)
		for (size_t i = 0; i < n; i++)
			image_free(&out[i]);
	return rc;
}

int grabit_wlr_capture_region(struct grabit_wl_state *s, struct grabit_output *o,
							  int32_t x, int32_t y, int32_t w, int32_t h,
							  bool overlay_cursor,
							  void *dst, int32_t dst_stride, int32_t dst_h,
							  uint32_t *out_format,
							  struct pixels_pool *cache) {
	if (!s || !s->screencopy_manager || !o || !dst) return -1;
	if (w <= 0 || h <= 0 || dst_stride <= 0 || dst_h <= 0) return -1;
	if (o->dead || !o->wl_output) return -1;

	struct sc_state c = {.wls = s, .pool = cache};
	c.frame = zwlr_screencopy_manager_v1_capture_output_region(
		s->screencopy_manager, overlay_cursor ? 1 : 0, o->wl_output, x, y, w, h);
	if (!c.frame) {
		log_error("zwlr_screencopy_manager_v1_capture_output_region: NULL");
		return -1;
	}
	zwlr_screencopy_frame_v1_add_listener(c.frame, &sc_listener, &c);

	int rc = -1;
	if (pixels_wl_wait(s->display, &c.status) == 0 && c.buf.map) {
		if (c.height != dst_h || c.width * 4 != dst_stride) {
			log_error("capture: size mismatch (got %dx%d, dst stride=%d h=%d)",
					  c.width, c.height, dst_stride, dst_h);
		} else {
			pixels_copy(dst, dst_stride, c.buf.map, c.stride,
						c.width, c.height, c.fmt.swap_rb, c.y_invert);
			if (out_format)
				*out_format = pixels_resolved_format(c.fmt.format, c.fmt.swap_rb);
			rc = 0;
		}
	}

	cleanup_state(&c);
	return rc;
}
