// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard_internal.h"

#include "log.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stdint.h>

#include <wayland-client.h>

#include "wlr-data-control-unstable-v1-client-protocol.h"

static void source_send(void *data, struct zwlr_data_control_source_v1 *src,
						const char *mime, int32_t fd) {
	(void)src;
	(void)mime;
	struct clip_serve_state *c = data;
	clip_write_all(fd, c->pay->bytes, c->pay->size);
}

static void source_cancelled(void *data, struct zwlr_data_control_source_v1 *src) {
	(void)src;
	((struct clip_serve_state *)data)->cancelled = true;
}

static const struct zwlr_data_control_source_v1_listener source_listener_g = {
	.send = source_send,
	.cancelled = source_cancelled,
};

int clip_wlr_serve(struct grabit_wl_state *s, const struct clip_payload *p,
				   int *ready_fd) {
	struct zwlr_data_control_device_v1 *dev =
		zwlr_data_control_manager_v1_get_data_device(s->data_control_manager, s->seat);
	struct zwlr_data_control_source_v1 *src =
		zwlr_data_control_manager_v1_create_data_source(s->data_control_manager);
	if (!dev || !src) {
		log_error("clipboard: wlr-data-control device/source allocation failed");
		return -1;
	}

	struct clip_serve_state c = {.pay = p};
	zwlr_data_control_source_v1_add_listener(src, &source_listener_g, &c);

	for (size_t i = 0; i < p->n_mimes; i++)
		zwlr_data_control_source_v1_offer(src, p->mimes[i]);

	zwlr_data_control_device_v1_set_selection(dev, src);
	if (wl_display_roundtrip(s->display) < 0) {
		zwlr_data_control_source_v1_destroy(src);
		zwlr_data_control_device_v1_destroy(dev);
		return -1;
	}
	clip_signal_ready(ready_fd, 1);
	clip_mute_stderr();

	while (!c.cancelled) {
		if (wl_display_dispatch(s->display) < 0) break;
	}

	zwlr_data_control_source_v1_destroy(src);
	zwlr_data_control_device_v1_destroy(dev);
	return 0;
}
