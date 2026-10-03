// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard_internal.h"

#include "log.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

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

struct wlr_recv {
	struct zwlr_data_control_offer_v1 *offer;
	int best;
	char mime[128];
	bool got_selection;
};

static void wlr_offer_mime(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
	(void)offer;
	struct wlr_recv *r = data;
	int rank = clip_rank_mime(mime);
	if (rank <= r->best) return;
	r->best = rank;
	snprintf(r->mime, sizeof r->mime, "%s", mime);
}

static const struct zwlr_data_control_offer_v1_listener wlr_offer_listener_g = {
	.offer = wlr_offer_mime,
};

static void wlr_data_offer(void *data, struct zwlr_data_control_device_v1 *dev,
						   struct zwlr_data_control_offer_v1 *offer) {
	(void)dev;
	zwlr_data_control_offer_v1_add_listener(offer, &wlr_offer_listener_g, data);
}

static void wlr_selection(void *data, struct zwlr_data_control_device_v1 *dev,
						  struct zwlr_data_control_offer_v1 *offer) {
	(void)dev;
	struct wlr_recv *r = data;
	r->offer = offer;
	r->got_selection = true;
}

static void wlr_finished(void *data, struct zwlr_data_control_device_v1 *dev) {
	(void)dev;
	((struct wlr_recv *)data)->got_selection = true;
}

static void wlr_primary(void *data, struct zwlr_data_control_device_v1 *dev, struct zwlr_data_control_offer_v1 *offer) {
	(void)data;
	(void)dev;
	if (offer) zwlr_data_control_offer_v1_destroy(offer);
}

static const struct zwlr_data_control_device_v1_listener wlr_recv_listener_g = {
	.data_offer = wlr_data_offer,
	.selection = wlr_selection,
	.finished = wlr_finished,
	.primary_selection = wlr_primary,
};

int clip_wlr_recv(struct grabit_wl_state *s, char **out) {
	struct zwlr_data_control_device_v1 *dev = zwlr_data_control_manager_v1_get_data_device(s->data_control_manager, s->seat);
	if (!dev) return -1;

	struct wlr_recv r = {0};
	zwlr_data_control_device_v1_add_listener(dev, &wlr_recv_listener_g, &r);
	if (wl_display_roundtrip(s->display) < 0 || !r.got_selection || !r.offer ||
		r.best == 0) {
		zwlr_data_control_device_v1_destroy(dev);
		return -1;
	}

	int fds[2];
	if (pipe(fds) != 0) {
		zwlr_data_control_device_v1_destroy(dev);
		return -1;
	}
	zwlr_data_control_offer_v1_receive(r.offer, r.mime, fds[1]);
	close(fds[1]);
	wl_display_flush(s->display);

	int rc = clip_read_fd(fds[0], out);
	zwlr_data_control_device_v1_destroy(dev);
	return rc;
}
