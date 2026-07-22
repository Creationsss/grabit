// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/ext_internal.h"

#include "capture/pixels.h"
#include "log.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-client.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

static void sess_buffer_size(void *data,
							 struct ext_image_copy_capture_session_v1 *sess,
							 uint32_t w, uint32_t h) {
	(void)sess;
	struct ec_session *es = data;
	es->width = (int32_t)w;
	es->height = (int32_t)h;
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

static int wait_session_done(struct grabit_wl_state *s, struct ec_session *es) {
	while (!es->done && es->status >= 0) {
		if (wl_display_dispatch(s->display) < 0) {
			log_error("wl_display_dispatch: lost connection");
			return -1;
		}
	}
	return es->status < 0 ? -1 : 0;
}

void gext_session_destroy(struct ec_session *es) {
	if (!es) return;
	if (es->session) ext_image_copy_capture_session_v1_destroy(es->session);
	if (es->source) ext_image_capture_source_v1_destroy(es->source);
	free(es);
}

static void ec_session_destroy_cb(void *priv) {
	gext_session_destroy(priv);
}

struct ec_session *gext_session_get(struct grabit_wl_state *s,
									struct grabit_output *o,
									bool overlay_cursor,
									struct pixels_pool *pool) {
	if (pool && pool->backend_priv) {
		struct ec_session *es = pool->backend_priv;
		if (es->status >= 0 && es->overlay_cursor == overlay_cursor) return es;
		gext_session_destroy(es);
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
		gext_session_destroy(es);
		return NULL;
	}
	ext_image_copy_capture_session_v1_add_listener(es->session, &sess_listener, es);

	if (wait_session_done(s, es) != 0) {
		gext_session_destroy(es);
		return NULL;
	}

	if (pool) {
		pool->backend_priv = es;
		pool->backend_priv_destroy = ec_session_destroy_cb;
	}
	return es;
}
