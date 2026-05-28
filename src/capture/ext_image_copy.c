// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/backend.h"
#include "capture/capture.h"
#include "capture/pixels.h"

#include "log.h"
#include "wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

struct ec_state {
	struct grabit_wl_state *wls;
	struct ext_image_copy_capture_session_v1 *session;
	struct ext_image_copy_capture_frame_v1 *frame;
	struct ext_image_capture_source_v1 *source;
	struct pixels_shm_buf buf;

	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
	uint32_t transform;
	bool swap_rb;
	bool have_size;
	bool session_done;

	struct pixels_pool *pool;

	uint32_t advertised[16];
	size_t n_advertised;

	int status;
};

static void sess_buffer_size(void *data,
							 struct ext_image_copy_capture_session_v1 *sess,
							 uint32_t w, uint32_t h) {
	(void)sess;
	struct ec_state *c = data;
	c->width = (int32_t)w;
	c->height = (int32_t)h;
	c->stride = (int32_t)w * 4;
	c->have_size = true;
}

static void sess_shm_format(void *data,
							struct ext_image_copy_capture_session_v1 *sess,
							uint32_t format) {
	(void)sess;
	struct ec_state *c = data;
	if (c->n_advertised < sizeof c->advertised / sizeof c->advertised[0]) {
		c->advertised[c->n_advertised++] = format;
	}
	if (c->format) return;
	uint32_t use = 0;
	bool swap = false;
	if (pixels_accept_format(format, &use, &swap)) {
		c->format = use;
		c->swap_rb = swap;
	}
}

static void sess_dmabuf_device(void *data,
							   struct ext_image_copy_capture_session_v1 *sess,
							   struct wl_array *dev) {
	(void)data;
	(void)sess;
	(void)dev;
}

static void sess_dmabuf_format(void *data,
							   struct ext_image_copy_capture_session_v1 *sess,
							   uint32_t format, struct wl_array *modifiers) {
	(void)data;
	(void)sess;
	(void)format;
	(void)modifiers;
}

static void sess_done(void *data, struct ext_image_copy_capture_session_v1 *sess) {
	(void)sess;
	((struct ec_state *)data)->session_done = true;
}

static void sess_stopped(void *data, struct ext_image_copy_capture_session_v1 *sess) {
	(void)sess;
	struct ec_state *c = data;
	c->status = -1;
	log_error("ext-image-copy: session stopped before capture completed");
}

static const struct ext_image_copy_capture_session_v1_listener sess_listener = {
	.buffer_size = sess_buffer_size,
	.shm_format = sess_shm_format,
	.dmabuf_device = sess_dmabuf_device,
	.dmabuf_format = sess_dmabuf_format,
	.done = sess_done,
	.stopped = sess_stopped,
};

static void frame_transform(void *data,
							struct ext_image_copy_capture_frame_v1 *f,
							uint32_t transform) {
	(void)f;
	((struct ec_state *)data)->transform = transform;
}

static void frame_damage(void *data,
						 struct ext_image_copy_capture_frame_v1 *f,
						 int32_t x, int32_t y, int32_t w, int32_t h) {
	(void)data;
	(void)f;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void frame_presentation_time(void *data,
									struct ext_image_copy_capture_frame_v1 *f,
									uint32_t hi, uint32_t lo, uint32_t nsec) {
	(void)data;
	(void)f;
	(void)hi;
	(void)lo;
	(void)nsec;
}

static void frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *f) {
	(void)f;
	((struct ec_state *)data)->status = 1;
}

static void frame_failed(void *data,
						 struct ext_image_copy_capture_frame_v1 *f,
						 uint32_t reason) {
	(void)f;
	struct ec_state *c = data;
	c->status = -1;
	log_error("ext-image-copy: frame failed (reason %u)", reason);
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = frame_transform,
	.damage = frame_damage,
	.presentation_time = frame_presentation_time,
	.ready = frame_ready,
	.failed = frame_failed,
};

static int wait_session_done(struct grabit_wl_state *s, struct ec_state *c) {
	while (!c->session_done && c->status >= 0) {
		if (wl_display_dispatch(s->display) < 0) {
			log_error("wl_display_dispatch: lost connection");
			return -1;
		}
	}
	return c->status < 0 ? -1 : 0;
}

static int alloc_buffer(struct ec_state *c) {
	if (!c->have_size || !c->format) {
		pixels_log_advertised("ext-image-copy", c->advertised, c->n_advertised);
		return -1;
	}
	return pixels_pool_acquire(c->wls->shm, "grabit-ext-image-copy", c->pool,
							   c->width, c->height, c->stride, c->format,
							   &c->buf);
}

static void cleanup_state(struct ec_state *c) {
	if (c->frame) ext_image_copy_capture_frame_v1_destroy(c->frame);
	if (c->session) ext_image_copy_capture_session_v1_destroy(c->session);
	if (c->source) ext_image_capture_source_v1_destroy(c->source);
	pixels_shm_buf_destroy(&c->buf);
}

static int do_capture(struct grabit_wl_state *s, struct grabit_output *o,
					  bool overlay_cursor, struct pixels_pool *pool,
					  struct ec_state *out_state) {
	memset(out_state, 0, sizeof *out_state);
	out_state->wls = s;
	out_state->pool = pool;

	out_state->source = ext_output_image_capture_source_manager_v1_create_source(
		s->ext_source_manager, o->wl_output);
	if (!out_state->source) {
		log_error("ext-image-copy: create_source failed");
		return -1;
	}

	uint32_t opts = overlay_cursor
						? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
						: 0;
	out_state->session = ext_image_copy_capture_manager_v1_create_session(
		s->ext_copy_manager, out_state->source, opts);
	if (!out_state->session) {
		log_error("ext-image-copy: create_session failed");
		return -1;
	}
	ext_image_copy_capture_session_v1_add_listener(out_state->session,
												   &sess_listener, out_state);

	if (wait_session_done(s, out_state) != 0) return -1;
	if (alloc_buffer(out_state) != 0) return -1;

	out_state->frame = ext_image_copy_capture_session_v1_create_frame(out_state->session);
	if (!out_state->frame) {
		log_error("ext-image-copy: create_frame failed");
		return -1;
	}
	ext_image_copy_capture_frame_v1_add_listener(out_state->frame,
												 &frame_listener, out_state);
	ext_image_copy_capture_frame_v1_attach_buffer(out_state->frame, out_state->buf.buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(out_state->frame, 0, 0,
												  out_state->width, out_state->height);
	ext_image_copy_capture_frame_v1_capture(out_state->frame);

	return pixels_wl_wait(s->display, &out_state->status);
}

int grabit_ext_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							struct image *out) {
	if (!s || !s->ext_copy_manager || !s->ext_source_manager) return -1;
	if (!o || o->dead || !o->wl_output || !out) return -1;
	memset(out, 0, sizeof *out);

	struct ec_state c;
	int rc = -1;
	if (do_capture(s, o, false, NULL, &c) == 0) {
		if (c.transform != 0) {
			log_warn("ext-image-copy: compositor applied transform=%u; "
					 "rotated outputs may render incorrectly",
					 c.transform);
		}
		out->width = c.width;
		out->height = c.height;
		out->stride = c.stride;
		out->format = pixels_resolved_format(c.format, c.swap_rb);
		out->size = c.buf.map_size;
		out->bytes = malloc(c.buf.map_size);
		if (out->bytes) {
			pixels_copy(out->bytes, c.stride, c.buf.map, c.stride,
						c.width, c.height, c.swap_rb, false);
			rc = 0;
		}
	}

	cleanup_state(&c);
	if (rc != 0) image_free(out);
	return rc;
}

int grabit_ext_capture_region(struct grabit_wl_state *s, struct grabit_output *o,
							  int32_t x, int32_t y, int32_t w, int32_t h,
							  bool overlay_cursor,
							  void *dst, int32_t dst_stride, int32_t dst_h,
							  uint32_t *out_format,
							  struct pixels_pool *cache) {
	if (!s || !s->ext_copy_manager || !s->ext_source_manager || !o || !dst) return -1;
	if (w <= 0 || h <= 0 || dst_stride <= 0 || dst_h <= 0) return -1;
	if (o->dead || !o->wl_output) return -1;

	struct ec_state c;
	int rc = -1;
	if (do_capture(s, o, overlay_cursor, cache, &c) == 0) {
		if (x < 0 || y < 0 || w > c.width - x || h > c.height - y) {
			log_error("ext-image-copy: region %d,%d %dx%d out of frame %dx%d",
					  x, y, w, h, c.width, c.height);
		} else if (h != dst_h || w * 4 != dst_stride) {
			log_error("ext-image-copy: size mismatch (got %dx%d, dst stride=%d h=%d)",
					  w, h, dst_stride, dst_h);
		} else {
			const uint8_t *src = (const uint8_t *)c.buf.map +
								 (size_t)y * (size_t)c.stride + (size_t)x * 4;
			pixels_copy(dst, dst_stride, src, c.stride, w, h, c.swap_rb, false);
			if (out_format) *out_format = pixels_resolved_format(c.format, c.swap_rb);
			rc = 0;
		}
	}

	cleanup_state(&c);
	return rc;
}
