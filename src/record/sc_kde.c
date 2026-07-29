// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/sc_backend.h"

#include "log.h"
#include "region/region.h"
#include "wl/wl.h"

#include <stdlib.h>

#include <wayland-client.h>

#include "zkde-screencast-unstable-v1-client-protocol.h"

struct sc_kde {
	struct zkde_screencast_unstable_v1 *manager;
	struct zkde_screencast_stream_unstable_v1 *stream;
	uint32_t node;
	bool have_node;
	bool failed;
};

bool sc_kde_available(struct grabit_wl_state *s) {
	return s && s->screencast_version != 0;
}

static void on_created(void *data,
					   struct zkde_screencast_stream_unstable_v1 *stream,
					   uint32_t node) {
	(void)stream;
	struct sc_kde *k = data;
	k->node = node;
	k->have_node = true;
}

static void on_closed(void *data,
					  struct zkde_screencast_stream_unstable_v1 *stream) {
	(void)stream;
	((struct sc_kde *)data)->failed = true;
}

static void on_failed(void *data,
					  struct zkde_screencast_stream_unstable_v1 *stream,
					  const char *error) {
	(void)stream;
	struct sc_kde *k = data;
	k->failed = true;
	log_error("kde-screencast: %s", error ? error : "stream failed");
}

static void on_serial(void *data,
					  struct zkde_screencast_stream_unstable_v1 *stream,
					  uint32_t hi, uint32_t low) {
	(void)data;
	(void)stream;
	(void)hi;
	(void)low;
}

static const struct zkde_screencast_stream_unstable_v1_listener stream_listener = {
	.closed = on_closed,
	.created = on_created,
	.failed = on_failed,
	.serial = on_serial,
};

struct sc_kde *sc_kde_start(struct grabit_wl_state *s, struct rect r, bool cursor,
							uint32_t *out_node_id) {
	if (!sc_kde_available(s)) return NULL;
	if (r.w <= 0 || r.h <= 0) return NULL;
	if (s->screencast_version < ZKDE_SCREENCAST_UNSTABLE_V1_STREAM_REGION_SINCE_VERSION) {
		log_error("kde-screencast: compositor offers version %u, region capture "
				  "needs %u or newer",
				  s->screencast_version,
				  ZKDE_SCREENCAST_UNSTABLE_V1_STREAM_REGION_SINCE_VERSION);
		return NULL;
	}

	struct sc_kde *k = calloc(1, sizeof *k);
	if (!k) return NULL;

	k->manager = wl_registry_bind(s->registry, s->screencast_name,
								  &zkde_screencast_unstable_v1_interface,
								  s->screencast_version);
	if (!k->manager) {
		log_error("kde-screencast: could not bind the screencast manager");
		free(k);
		return NULL;
	}

	k->stream = zkde_screencast_unstable_v1_stream_region(
		k->manager, r.x, r.y, (uint32_t)r.w, (uint32_t)r.h,
		wl_fixed_from_int(1),
		cursor ? ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_EMBEDDED
			   : ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_HIDDEN);
	if (!k->stream) {
		log_error("kde-screencast: stream_region failed");
		sc_kde_stop(k);
		return NULL;
	}
	zkde_screencast_stream_unstable_v1_add_listener(k->stream, &stream_listener, k);

	while (!k->have_node && !k->failed) {
		if (wl_display_roundtrip(s->display) < 0) {
			log_error("kde-screencast: lost the wayland connection");
			k->failed = true;
		}
	}
	if (!k->have_node) {
		sc_kde_stop(k);
		return NULL;
	}

	log_debug("kde-screencast: streaming %dx%d at %d,%d (cursor %s) on pipewire node %u",
			  r.w, r.h, r.x, r.y, cursor ? "embedded" : "hidden", k->node);
	*out_node_id = k->node;
	return k;
}

void sc_kde_stop(struct sc_kde *k) {
	if (!k) return;
	if (k->stream) zkde_screencast_stream_unstable_v1_close(k->stream);
	if (k->manager) zkde_screencast_unstable_v1_destroy(k->manager);
	free(k);
}
