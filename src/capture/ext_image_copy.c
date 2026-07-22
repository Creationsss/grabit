// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/backend.h"
#include "capture/capture.h"
#include "capture/pixels.h"

#include "log.h"
#include "notify/notify.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

struct ec_session {
	struct ext_image_capture_source_v1 *source;
	struct ext_image_copy_capture_session_v1 *session;

	int32_t width;
	int32_t height;
	int32_t stride;
	struct pixels_fmt_pick fmt;
	bool have_size;
	bool done;
	bool overlay_cursor;

	int status;
	int *frame_status;
};

struct ec_state {
	struct grabit_wl_state *wls;
	struct ec_session *sess;
	bool owns_session;
	struct ext_image_copy_capture_frame_v1 *frame;
	struct pixels_shm_buf buf;
	struct pixels_pool *pool;
	uint32_t transform;
	int status;
};

static void sess_buffer_size(void *data,
							 struct ext_image_copy_capture_session_v1 *sess,
							 uint32_t w, uint32_t h) {
	(void)sess;
	struct ec_session *es = data;
	es->width = (int32_t)w;
	es->height = (int32_t)h;
	es->stride = (int32_t)w * 4;
	es->have_size = true;
}

static void sess_shm_format(void *data,
							struct ext_image_copy_capture_session_v1 *sess,
							uint32_t format) {
	(void)sess;
	pixels_fmt_offer(&((struct ec_session *)data)->fmt, format);
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
	((struct ec_session *)data)->done = true;
}

static void sess_stopped(void *data, struct ext_image_copy_capture_session_v1 *sess) {
	(void)sess;
	struct ec_session *es = data;
	es->status = -1;
	if (es->frame_status) *es->frame_status = -1;
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

static void warn_rotation_once(uint32_t transform) {
	static bool warned;
	if (transform == 0 || warned) return;
	warned = true;
	log_warn("ext-image-copy: compositor applied transform=%u; "
			 "rotated outputs may render incorrectly",
			 transform);
	notify_send(&(struct notify_opts){
		.summary = "grabit: rotated output",
		.body = "the ext-image-copy backend doesn't yet apply output rotation; "
				"the screenshot may be skewed",
	});
}

static int wait_session_done(struct grabit_wl_state *s, struct ec_session *es) {
	while (!es->done && es->status >= 0) {
		if (wl_display_dispatch(s->display) < 0) {
			log_error("wl_display_dispatch: lost connection");
			return -1;
		}
	}
	return es->status < 0 ? -1 : 0;
}

static void ec_session_destroy(struct ec_session *es) {
	if (!es) return;
	if (es->session) ext_image_copy_capture_session_v1_destroy(es->session);
	if (es->source) ext_image_capture_source_v1_destroy(es->source);
	free(es);
}

static void ec_session_destroy_cb(void *priv) {
	ec_session_destroy(priv);
}

static struct ec_session *ec_session_get(struct grabit_wl_state *s,
										 struct grabit_output *o,
										 bool overlay_cursor,
										 struct pixels_pool *pool) {
	if (pool && pool->backend_priv) {
		struct ec_session *es = pool->backend_priv;
		if (es->status >= 0 && es->overlay_cursor == overlay_cursor) return es;
		ec_session_destroy(es);
		pool->backend_priv = NULL;
	}

	struct ec_session *es = calloc(1, sizeof *es);
	if (!es) return NULL;
	es->overlay_cursor = overlay_cursor;

	es->source = ext_output_image_capture_source_manager_v1_create_source(
		s->ext_source_manager, o->wl_output);
	if (!es->source) {
		log_error("ext-image-copy: create_source failed");
		free(es);
		return NULL;
	}

	uint32_t opts = overlay_cursor
						? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
						: 0;
	es->session = ext_image_copy_capture_manager_v1_create_session(
		s->ext_copy_manager, es->source, opts);
	if (!es->session) {
		log_error("ext-image-copy: create_session failed");
		ec_session_destroy(es);
		return NULL;
	}
	ext_image_copy_capture_session_v1_add_listener(es->session, &sess_listener, es);

	if (wait_session_done(s, es) != 0) {
		ec_session_destroy(es);
		return NULL;
	}

	if (pool) {
		pool->backend_priv = es;
		pool->backend_priv_destroy = ec_session_destroy_cb;
	}
	return es;
}

static int alloc_buffer(struct ec_state *c) {
	struct ec_session *es = c->sess;
	if (!es->have_size || !es->fmt.chosen) {
		pixels_log_advertised("ext-image-copy", es->fmt.advertised, es->fmt.n);
		return -1;
	}
	return pixels_pool_acquire(c->wls->shm, "grabit-ext-image-copy", c->pool,
							   es->width, es->height, es->stride, es->fmt.format,
							   &c->buf);
}

static void cleanup_state(struct ec_state *c) {
	if (c->sess) c->sess->frame_status = NULL;
	if (c->frame) ext_image_copy_capture_frame_v1_destroy(c->frame);
	if (c->owns_session) ec_session_destroy(c->sess);
	pixels_shm_buf_destroy(&c->buf);
}

static int do_capture(struct grabit_wl_state *s, struct grabit_output *o,
					  bool overlay_cursor, struct pixels_pool *pool,
					  struct ec_state *out_state) {
	memset(out_state, 0, sizeof *out_state);
	out_state->wls = s;
	out_state->pool = pool;

	out_state->sess = ec_session_get(s, o, overlay_cursor, pool);
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
	if (rc == 0) warn_rotation_once(out_state->transform);
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
								   c.sess->fmt.format, c.sess->fmt.swap_rb, false);
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
		if (x < 0 || y < 0 || w > c.sess->width - x || h > c.sess->height - y) {
			log_error("ext-image-copy: region %d,%d %dx%d out of frame %dx%d",
					  x, y, w, h, c.sess->width, c.sess->height);
		} else if (h != dst_h || w * 4 != dst_stride) {
			log_error("ext-image-copy: size mismatch (got %dx%d, dst stride=%d h=%d)",
					  w, h, dst_stride, dst_h);
		} else {
			const uint8_t *src = (const uint8_t *)c.buf.map +
								 (size_t)y * (size_t)c.sess->stride + (size_t)x * 4;
			pixels_copy(dst, dst_stride, src, c.sess->stride, w, h,
						c.sess->fmt.swap_rb, false);
			if (out_format)
				*out_format = pixels_resolved_format(c.sess->fmt.format, c.sess->fmt.swap_rb);
			rc = 0;
		}
	}

	cleanup_state(&c);
	return rc;
}
