// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/backend.h"
#include "capture/capture.h"
#include "capture/ext_internal.h"
#include "capture/pixels.h"

#include "log.h"

#include "wl/wl.h"
#include <math.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

struct ec_state {
	struct grabit_wl_state *wls;
	struct ec_session *sess;
	bool owns_session;
	struct ext_image_copy_capture_frame_v1 *frame;
	struct pixels_shm_buf buf;
	struct pixels_pool *pool;
	int status;
};

static void frame_transform(void *data,
							struct ext_image_copy_capture_frame_v1 *f,
							uint32_t transform) {
	(void)data;
	(void)f;
	(void)transform;
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
	if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS)
		c->sess->status = -1;
	log_error("ext-image-copy: frame failed (reason %u)", reason);
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
	.transform = frame_transform,
	.damage = frame_damage,
	.presentation_time = frame_presentation_time,
	.ready = frame_ready,
	.failed = frame_failed,
};

static int alloc_buffer(struct ec_state *c) {
	struct ec_session *es = c->sess;
	if (!es->have_size || !es->fmt.chosen) {
		pixels_log_advertised("ext-image-copy", es->fmt.advertised, es->fmt.n);
		return -1;
	}
	es->stride = es->width * pixels_conv_src_bpp(es->fmt.conv);
	return pixels_pool_acquire(c->wls->shm, "grabit-ext-image-copy", c->pool,
							   es->width, es->height, es->stride, es->fmt.format,
							   &c->buf);
}

static void cleanup_state(struct ec_state *c) {
	if (c->sess) c->sess->frame_status = NULL;
	if (c->frame) ext_image_copy_capture_frame_v1_destroy(c->frame);
	if (c->owns_session) gext_session_destroy(c->sess);
	pixels_shm_buf_destroy(&c->buf);
}

static int do_capture(struct grabit_wl_state *s, struct grabit_output *o,
					  bool overlay_cursor, struct pixels_pool *pool,
					  struct ec_state *out_state) {
	memset(out_state, 0, sizeof *out_state);
	out_state->wls = s;
	out_state->pool = pool;

	out_state->sess = gext_session_get(s, o, overlay_cursor, pool);
	if (!out_state->sess) return -1;
	out_state->owns_session = pool == NULL;

	if (alloc_buffer(out_state) != 0) return -1;

	out_state->frame = ext_image_copy_capture_session_v1_create_frame(out_state->sess->session);
	if (!out_state->frame) {
		log_error("ext-image-copy: create_frame failed");
		return -1;
	}
	ext_image_copy_capture_frame_v1_add_listener(out_state->frame,
												 &frame_listener, out_state);
	ext_image_copy_capture_frame_v1_attach_buffer(out_state->frame, out_state->buf.buffer);
	ext_image_copy_capture_frame_v1_damage_buffer(out_state->frame, 0, 0,
												  out_state->sess->width,
												  out_state->sess->height);
	out_state->sess->frame_status = &out_state->status;
	ext_image_copy_capture_frame_v1_capture(out_state->frame);

	int rc = pixels_wl_wait(s->display, &out_state->status);
	out_state->sess->frame_status = NULL;
	return rc;
}

int grabit_ext_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							bool overlay_cursor, struct image *out) {
	if (!s || !s->ext_copy_manager || !s->ext_source_manager) return -1;
	if (!o || o->dead || !o->wl_output || !out) return -1;
	memset(out, 0, sizeof *out);

	struct ec_state c;
	int rc = -1;
	if (do_capture(s, o, overlay_cursor, NULL, &c) == 0) {
		rc = pixels_image_from_buf(out, c.buf.map, c.buf.map_size,
								   c.sess->width, c.sess->height, c.sess->stride,
								   c.sess->fmt.format, c.sess->fmt.conv, false);
	}

	cleanup_state(&c);
	if (rc != 0) image_free(out);
	return rc;
}

int grabit_ext_capture_region(struct grabit_wl_state *s, struct grabit_output *o,
							  int32_t x, int32_t y, int32_t w, int32_t h,
							  bool overlay_cursor,
							  void *dst, size_t dst_size, int32_t dst_stride,
							  int32_t *out_w, int32_t *out_h,
							  uint32_t *out_format,
							  struct pixels_pool *cache) {
	if (!s || !s->ext_copy_manager || !s->ext_source_manager || !o || !dst ||
		!out_w || !out_h)
		return -1;
	if (w <= 0 || h <= 0 || dst_size == 0 || dst_stride < 0) return -1;
	if (o->dead || !o->wl_output) return -1;

	struct ec_state c;
	int rc = -1;
	if (do_capture(s, o, overlay_cursor, cache, &c) == 0) {
		int32_t px, py, pw, ph;
		grabit_output_region_pixels(o, x, y, &px, &py);
		grabit_output_region_pixels(o, w, h, &pw, &ph);
		grabit_wl_transform_map_rect(o->transform, c.sess->width, c.sess->height,
									 &px, &py, &pw, &ph);
		int32_t stride = dst_stride > 0 ? dst_stride : pw * 4;
		if (px < 0 || py < 0 || pw > c.sess->width - px || ph > c.sess->height - py) {
			log_error("ext-image-copy: region %d,%d %dx%d out of frame %dx%d",
					  px, py, pw, ph, c.sess->width, c.sess->height);
		} else if (pw * 4 > stride || (size_t)stride * (size_t)ph > dst_size) {
			log_error("ext-image-copy: frame %dx%d does not fit dst (stride=%d size=%zu)",
					  pw, ph, stride, dst_size);
		} else {
			const uint8_t *src = (const uint8_t *)c.buf.map +
								 (size_t)py * (size_t)c.sess->stride + (size_t)px * 4;
			pixels_copy(dst, stride, src, c.sess->stride, pw, ph,
						c.sess->fmt.conv, false);
			*out_w = pw;
			*out_h = ph;
			if (out_format)
				*out_format = pixels_resolved_format(c.sess->fmt.format, c.sess->fmt.conv);
			rc = 0;
		}
	}

	cleanup_state(&c);
	return rc;
}
