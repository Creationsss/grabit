// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/pw.h"

#include "log.h"

#include <stddef.h>

#ifdef HAVE_PIPEWIRE

#include "capture/pixels.h"
#include "record/ring.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#define PW_NEGOTIATE_TIMEOUT_MS 5000

struct pw_capture {
	struct pw_thread_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_stream *stream;
	struct spa_hook stream_hook;

	bool have_format;
	atomic_int failed;
	atomic_int paused;

	int32_t width;
	int32_t height;
	int32_t stride;
	enum pixels_conv conv;

	struct buf_pool *pool;
	struct ring *ring;
};

static bool format_is_supported(uint32_t fmt, enum pixels_conv *out) {
	switch (fmt) {
	case SPA_VIDEO_FORMAT_BGRx:
	case SPA_VIDEO_FORMAT_BGRA:
		*out = PIX_COPY;
		return true;
	case SPA_VIDEO_FORMAT_RGBx:
	case SPA_VIDEO_FORMAT_RGBA:
		*out = PIX_SWAP_RB;
		return true;
	default:
		return false;
	}
}

static void on_param_changed(void *data, uint32_t id, const struct spa_pod *param) {
	struct pw_capture *c = data;
	if (!param || id != SPA_PARAM_Format) return;

	uint32_t media_type = 0, media_subtype = 0;
	if (spa_format_parse(param, &media_type, &media_subtype) < 0) return;
	if (media_type != SPA_MEDIA_TYPE_video ||
		media_subtype != SPA_MEDIA_SUBTYPE_raw) return;
	struct spa_video_info_raw info;
	if (spa_format_video_raw_parse(param, &info) < 0) return;

	enum pixels_conv conv = PIX_COPY;
	if (!format_is_supported(info.format, &conv)) {
		log_error("pipewire: unsupported pixel format %u", info.format);
		atomic_store(&c->failed, 1);
		pw_thread_loop_signal(c->loop, false);
		return;
	}

	size_t need = (size_t)info.size.width * 4u * (size_t)info.size.height;
	if (c->pool && need > c->pool->buf_size) {
		log_error("pipewire: stream renegotiated to %ux%u, larger than the "
				  "frame pool allocated for %dx%d",
				  info.size.width, info.size.height, c->width, c->height);
		atomic_store(&c->failed, 1);
		pw_thread_loop_signal(c->loop, false);
		return;
	}

	c->width = (int32_t)info.size.width;
	c->height = (int32_t)info.size.height;
	c->stride = c->width * 4;
	c->conv = conv;
	c->have_format = true;

	uint8_t buf[512];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
	const struct spa_pod *params[1];
	params[0] = spa_pod_builder_add_object(
		&b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, 16),
		SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
		SPA_PARAM_BUFFERS_stride, SPA_POD_CHOICE_RANGE_Int(c->stride, c->stride, INT32_MAX),
		SPA_PARAM_BUFFERS_dataType,
		SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)));
	pw_stream_update_params(c->stream, params, 1);

	log_debug("pipewire: negotiated %dx%d format=%u", c->width, c->height,
			  info.format);
	pw_thread_loop_signal(c->loop, false);
}

static void on_state_changed(void *data, enum pw_stream_state old,
							 enum pw_stream_state state, const char *error) {
	struct pw_capture *c = data;
	log_debug("pipewire: stream %s -> %s", pw_stream_state_as_string(old),
			  pw_stream_state_as_string(state));
	if (state == PW_STREAM_STATE_ERROR) {
		log_error("pipewire: stream error: %s", error ? error : "unknown");
		atomic_store(&c->failed, 1);
		pw_thread_loop_signal(c->loop, false);
	} else if (state == PW_STREAM_STATE_UNCONNECTED) {
		atomic_store(&c->failed, 1);
		pw_thread_loop_signal(c->loop, false);
	}
}

static void on_process(void *data) {
	struct pw_capture *c = data;
	struct pw_buffer *pb = pw_stream_dequeue_buffer(c->stream);
	if (!pb) return;

	struct spa_buffer *buf = pb->buffer;
	struct spa_data *d = buf->n_datas ? &buf->datas[0] : NULL;
	bool has_pixels = d && d->data && d->chunk->size > 0;
	void *frame_buf = NULL;

	if (c->ring && has_pixels && !atomic_load(&c->paused)) {
		if ((size_t)c->stride * (size_t)c->height > c->pool->buf_size) {
			ring_record_drop(c->ring);
			pw_stream_queue_buffer(c->stream, pb);
			return;
		}
		frame_buf = pool_try_acquire(c->pool);
		if (!frame_buf) {
			ring_record_drop(c->ring);
		} else {
			int32_t src_stride = d->chunk->stride > 0 ? d->chunk->stride : c->stride;
			pixels_copy(frame_buf, c->stride,
						(const uint8_t *)d->data + d->chunk->offset, src_stride,
						c->width, c->height, c->conv, false);
		}
	}

	pw_stream_queue_buffer(c->stream, pb);

	if (frame_buf) {
		struct frame f = {
			.data = frame_buf,
			.width = c->width,
			.height = c->height,
			.stride = c->stride,
			.pool = c->pool,
		};
		ring_push(c->ring, &f);
	}
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = on_state_changed,
	.param_changed = on_param_changed,
	.process = on_process,
};

static const struct spa_pod *build_format(struct spa_pod_builder *b, int fps) {
	return spa_pod_builder_add_object(
		b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
		SPA_FORMAT_VIDEO_format,
		SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
							   SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx,
							   SPA_VIDEO_FORMAT_RGBA),
		SPA_FORMAT_VIDEO_size,
		SPA_POD_CHOICE_RANGE_Rectangle(&SPA_RECTANGLE(1920, 1080),
									   &SPA_RECTANGLE(1, 1),
									   &SPA_RECTANGLE(8192, 8192)),
		SPA_FORMAT_VIDEO_framerate,
		SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION((uint32_t)fps, 1),
									  &SPA_FRACTION(0, 1),
									  &SPA_FRACTION(240, 1)));
}

struct pw_capture *pw_capture_open(uint32_t node_id, int fps) {
	pw_init(NULL, NULL);

	struct pw_capture *c = calloc(1, sizeof *c);
	if (!c) return NULL;

	c->loop = pw_thread_loop_new("grabit-record", NULL);
	if (!c->loop) {
		log_error("pipewire: could not create the thread loop");
		free(c);
		return NULL;
	}

	pw_thread_loop_lock(c->loop);
	if (pw_thread_loop_start(c->loop) != 0) {
		log_error("pipewire: could not start the thread loop");
		goto fail;
	}

	c->context = pw_context_new(pw_thread_loop_get_loop(c->loop), NULL, 0);
	if (!c->context) {
		log_error("pipewire: could not create a context");
		goto fail;
	}
	c->core = pw_context_connect(c->context, NULL, 0);
	if (!c->core) {
		log_error("pipewire: could not connect to the pipewire daemon");
		goto fail;
	}

	c->stream = pw_stream_new(
		c->core, "grabit-record",
		pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
						  "Capture", PW_KEY_MEDIA_ROLE, "Screen", NULL));
	if (!c->stream) {
		log_error("pipewire: could not create the stream");
		goto fail;
	}
	pw_stream_add_listener(c->stream, &c->stream_hook, &stream_events, c);

	uint8_t buf[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
	const struct spa_pod *params[1] = {build_format(&b, fps)};

	if (pw_stream_connect(c->stream, PW_DIRECTION_INPUT, node_id,
						  PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
						  params, 1) < 0) {
		log_error("pipewire: could not connect to node %u", node_id);
		goto fail;
	}

	struct timespec abstime;
	pw_thread_loop_get_time(c->loop, &abstime,
							PW_NEGOTIATE_TIMEOUT_MS * SPA_NSEC_PER_MSEC);
	while (!c->have_format && !atomic_load(&c->failed)) {
		if (pw_thread_loop_timed_wait_full(c->loop, &abstime) < 0) {
			log_error("pipewire: timed out negotiating a video format");
			break;
		}
	}
	if (!c->have_format) goto fail;

	pw_thread_loop_unlock(c->loop);
	return c;

fail:
	pw_thread_loop_unlock(c->loop);
	pw_capture_close(c);
	return NULL;
}

void pw_capture_size(const struct pw_capture *c, int32_t *w, int32_t *h,
					 int32_t *stride) {
	*w = c->width;
	*h = c->height;
	*stride = c->stride;
}

void pw_capture_bind(struct pw_capture *c, struct buf_pool *pool, struct ring *ring) {
	pw_thread_loop_lock(c->loop);
	c->pool = pool;
	c->ring = ring;
	pw_thread_loop_unlock(c->loop);
}

void pw_capture_set_paused(struct pw_capture *c, bool paused) {
	atomic_store(&c->paused, paused ? 1 : 0);
}

bool pw_available(void) {
	return true;
}

bool pw_capture_failed(const struct pw_capture *c) {
	return atomic_load(&c->failed) != 0;
}

void pw_capture_close(struct pw_capture *c) {
	if (!c) return;
	if (c->loop) pw_thread_loop_stop(c->loop);
	if (c->stream) {
		spa_hook_remove(&c->stream_hook);
		pw_stream_destroy(c->stream);
	}
	if (c->core) pw_core_disconnect(c->core);
	if (c->context) pw_context_destroy(c->context);
	if (c->loop) pw_thread_loop_destroy(c->loop);
	free(c);
}

#else

bool pw_available(void) {
	return false;
}

struct pw_capture *pw_capture_open(uint32_t node_id, int fps) {
	(void)node_id;
	(void)fps;
	return NULL;
}

void pw_capture_size(const struct pw_capture *c, int32_t *w, int32_t *h,
					 int32_t *stride) {
	(void)c;
	*w = *h = *stride = 0;
}

void pw_capture_bind(struct pw_capture *c, struct buf_pool *pool, struct ring *ring) {
	(void)c;
	(void)pool;
	(void)ring;
}

void pw_capture_set_paused(struct pw_capture *c, bool paused) {
	(void)c;
	(void)paused;
}

bool pw_capture_failed(const struct pw_capture *c) {
	(void)c;
	return true;
}

void pw_capture_close(struct pw_capture *c) {
	(void)c;
}

#endif
