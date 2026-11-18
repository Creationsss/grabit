// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/window.h"

#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>

#define URI_MIME "text/uri-list"
#define DROP_MAX (64 * 1024)
#define DROP_TIMEOUT_MS 2000

static void offer_offer(void *data, struct wl_data_offer *offer, const char *mime) {
	(void)offer;
	bool *uri = data;
	if (strcmp(mime, URI_MIME) == 0) *uri = true;
}

static void offer_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions) {
	(void)data;
	(void)offer;
	(void)actions;
}

static void offer_action(void *data, struct wl_data_offer *offer, uint32_t action) {
	(void)data;
	(void)offer;
	(void)action;
}

static const struct wl_data_offer_listener offer_listener_g = {
	.offer = offer_offer,
	.source_actions = offer_source_actions,
	.action = offer_action,
};

static void offer_destroy(struct wl_data_offer *offer) {
	if (!offer) return;
	free(wl_data_offer_get_user_data(offer));
	wl_data_offer_destroy(offer);
}

static bool offer_has_uri(struct wl_data_offer *offer) {
	bool *uri = offer ? wl_data_offer_get_user_data(offer) : NULL;
	return uri && *uri;
}

static void drag_end(struct ui_window *w) {
	offer_destroy(w->drag_offer);
	w->drag_offer = NULL;
	if (!w->drag_inside) return;
	w->drag_inside = false;
	if (w->on_drag) w->on_drag(w, false);
}

static void dev_data_offer(void *data, struct wl_data_device *dev, struct wl_data_offer *offer) {
	(void)data;
	(void)dev;
	bool *uri = calloc(1, sizeof *uri);
	wl_data_offer_add_listener(offer, &offer_listener_g, uri);
}

static void dev_enter(void *data, struct wl_data_device *dev, uint32_t serial,
					  struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
					  struct wl_data_offer *offer) {
	(void)dev;
	(void)x;
	(void)y;
	struct ui_window *w = data;
	drag_end(w);
	if (surface != w->surface || !offer) {
		offer_destroy(offer);
		return;
	}
	w->drag_offer = offer;
	bool ok = offer_has_uri(offer) && w->on_drop;
	wl_data_offer_accept(offer, serial, ok ? URI_MIME : NULL);
	if (wl_data_offer_get_version(offer) >= 3) {
		uint32_t a = ok ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : 0;
		wl_data_offer_set_actions(offer, a, a);
	}
	if (!ok) return;
	w->drag_inside = true;
	if (w->on_drag) w->on_drag(w, true);
}

static void dev_leave(void *data, struct wl_data_device *dev) {
	(void)dev;
	drag_end(data);
}

static void dev_motion(void *data, struct wl_data_device *dev, uint32_t time, wl_fixed_t x,
					   wl_fixed_t y) {
	(void)data;
	(void)dev;
	(void)time;
	(void)x;
	(void)y;
}

static char *receive_uris(struct ui_window *w, struct wl_data_offer *offer) {
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) return NULL;
	wl_data_offer_receive(offer, URI_MIME, fds[1]);
	close(fds[1]);
	wl_display_flush(w->wls->display);
	char *uris = NULL;
	return grabit_read_fd_all(fds[0], DROP_MAX, DROP_TIMEOUT_MS, &uris) == 0 ? uris : NULL;
}

static void dev_drop(void *data, struct wl_data_device *dev) {
	(void)dev;
	struct ui_window *w = data;
	struct wl_data_offer *offer = w->drag_offer;
	if (!offer || !w->drag_inside) {
		drag_end(w);
		return;
	}
	char *uris = receive_uris(w, offer);
	if (wl_data_offer_get_version(offer) >= 3) wl_data_offer_finish(offer);
	drag_end(w);
	if (uris && uris[0])
		w->on_drop(w, uris);
	else
		log_warn("settings: the dropped item sent no file list");
	free(uris);
	ui_window_damage(w);
}

static void dev_selection(void *data, struct wl_data_device *dev, struct wl_data_offer *offer) {
	(void)data;
	(void)dev;
	offer_destroy(offer);
}

static const struct wl_data_device_listener device_listener_g = {
	.data_offer = dev_data_offer,
	.enter = dev_enter,
	.leave = dev_leave,
	.motion = dev_motion,
	.drop = dev_drop,
	.selection = dev_selection,
};

void ui_dnd_attach(struct ui_window *w) {
	if (!w->on_drop || !w->wls->data_device_manager || !w->wls->seat) return;
	w->data_device = wl_data_device_manager_get_data_device(w->wls->data_device_manager,
															w->wls->seat);
	if (w->data_device) wl_data_device_add_listener(w->data_device, &device_listener_g, w);
}

void ui_dnd_detach(struct ui_window *w) {
	offer_destroy(w->drag_offer);
	w->drag_offer = NULL;
	w->drag_inside = false;
	if (!w->data_device) return;
	if (wl_data_device_get_version(w->data_device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
		wl_data_device_release(w->data_device);
	else
		wl_data_device_destroy(w->data_device);
	w->data_device = NULL;
}
