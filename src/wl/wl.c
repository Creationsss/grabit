// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "wl/wl.h"

#include "capture/capture.h"
#include "log.h"
#include "region/region.h"
#include "wl/internal.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "ext-data-control-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
int grabit_wl_init(struct grabit_wl_state *s) {
	memset(s, 0, sizeof *s);

	s->display = wl_display_connect(NULL);
	if (!s->display) {
		const char *wd = getenv("WAYLAND_DISPLAY");
		log_error("could not connect to wayland (WAYLAND_DISPLAY=%s)",
				  wd ? wd : "(unset)");
		return -1;
	}

	s->registry = wl_display_get_registry(s->display);
	wl_registry_add_listener(s->registry, &grabit_wl_registry_listener, s);

	if (wl_display_roundtrip(s->display) < 0) goto fail;

	if (!s->shm) {
		log_error("compositor doesn't advertise wl_shm");
		goto fail;
	}
	if (!s->compositor) {
		log_error("compositor doesn't advertise wl_compositor");
		goto fail;
	}
	if (!capture_backend_available(s)) {
		const char *de = getenv("XDG_CURRENT_DESKTOP");
		char de_up[64] = {0};
		if (de) {
			for (size_t i = 0; i < sizeof de_up - 1 && de[i]; i++)
				de_up[i] = (char)toupper((unsigned char)de[i]);
		}
		const char *known = NULL;
		const char *alt = NULL;
		const char *note = NULL;
		if (strstr(de_up, "KDE")) {
			known = "KDE Plasma (KWin)";
			alt = "spectacle";
			note = "org.kde.KWin.ScreenShot2 is not on the session bus either";
		} else if (strstr(de_up, "GNOME")) {
			known = "GNOME (Mutter)";
			alt = "gnome-screenshot, flameshot, or ksnip";
		} else if (strstr(de_up, "COSMIC")) {
			known = "Cosmic";
		}
		log_error("compositor advertises no screen-capture protocol");
		log_error("  (wanted zwlr_screencopy_manager_v1 or ext_image_copy_capture_manager_v1)");
		if (known)
			log_error("  %s implements neither, so grabit cannot capture here", known);
		if (note) log_error("  %s", note);
		log_error("  works on: hyprland, sway, niri, river");
		if (alt) log_error("  on this desktop try: %s", alt);
		goto fail;
	}

	if (wl_display_roundtrip(s->display) < 0) goto fail;

	if (s->xdg_output_manager) {
		for (size_t i = 0; i < s->n_outputs; i++) {
			if (!s->outputs[i]->dead) gwl_output_attach_xdg(s, s->outputs[i]);
		}
		if (wl_display_roundtrip(s->display) < 0) goto fail;
	}

	for (size_t i = 0; i < s->n_outputs; i++)
		gwl_output_finalize(s->outputs[i]);

	if (s->n_outputs == 0) {
		log_error("no outputs reported by the compositor");
		goto fail;
	}
	return 0;

fail:
	grabit_wl_finish(s);
	return -1;
}

int grabit_wl_probe(struct grabit_wl_state *s) {
	memset(s, 0, sizeof *s);

	s->display = wl_display_connect(NULL);
	if (!s->display) return -1;

	s->registry = wl_display_get_registry(s->display);
	wl_registry_add_listener(s->registry, &grabit_wl_registry_listener, s);

	if (wl_display_roundtrip(s->display) < 0) {
		grabit_wl_finish(s);
		return -1;
	}
	return 0;
}

void grabit_wl_finish(struct grabit_wl_state *s) {
	if (!s) return;

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->xdg_output) zxdg_output_v1_destroy(o->xdg_output);
		if (o->wl_output) wl_output_destroy(o->wl_output);
		free(o->name);
		free(o);
	}
	free(s->outputs);
	s->outputs = NULL;
	s->n_outputs = s->cap_outputs = 0;

	if (s->viewporter) wp_viewporter_destroy(s->viewporter);
	if (s->fractional_scale_manager)
		wp_fractional_scale_manager_v1_destroy(s->fractional_scale_manager);
	if (s->cursor_shape_manager)
		wp_cursor_shape_manager_v1_destroy(s->cursor_shape_manager);
	if (s->xdg_output_manager) zxdg_output_manager_v1_destroy(s->xdg_output_manager);
	if (s->layer_shell) zwlr_layer_shell_v1_destroy(s->layer_shell);
	if (s->data_control_manager) zwlr_data_control_manager_v1_destroy(s->data_control_manager);
	if (s->ext_data_control_manager) ext_data_control_manager_v1_destroy(s->ext_data_control_manager);
	if (s->screencopy_manager) zwlr_screencopy_manager_v1_destroy(s->screencopy_manager);
	if (s->ext_copy_manager) ext_image_copy_capture_manager_v1_destroy(s->ext_copy_manager);
	if (s->ext_source_manager) ext_output_image_capture_source_manager_v1_destroy(s->ext_source_manager);
	if (s->seat) wl_seat_destroy(s->seat);
	if (s->compositor) wl_compositor_destroy(s->compositor);
	if (s->shm) wl_shm_destroy(s->shm);
	if (s->registry) wl_registry_destroy(s->registry);
	if (s->display) wl_display_disconnect(s->display);

	memset(s, 0, sizeof *s);
}

void grabit_wl_clear_input_region(struct wl_compositor *c, struct wl_surface *s) {
	if (!c || !s) return;
	struct wl_region *r = wl_compositor_create_region(c);
	wl_surface_set_input_region(s, r);
	wl_region_destroy(r);
}

void grabit_wl_callback_drop(struct wl_callback **cb) {
	if (!cb || !*cb) return;
	wl_callback_destroy(*cb);
	*cb = NULL;
}

struct zwlr_layer_surface_v1 *grabit_wl_layer_fullscreen(
	struct grabit_wl_state *s, struct wl_surface *surface,
	struct wl_output *output, const char *ns, uint32_t kb_interactivity,
	const struct zwlr_layer_surface_v1_listener *listener, void *data) {
	struct zwlr_layer_surface_v1 *ls = zwlr_layer_shell_v1_get_layer_surface(
		s->layer_shell, surface, output,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ns);
	if (!ls) return NULL;
	if (listener) zwlr_layer_surface_v1_add_listener(ls, listener, data);
	zwlr_layer_surface_v1_set_anchor(ls,
									 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(ls, 0, 0);
	zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(ls, kb_interactivity);
	return ls;
}

int grabit_wl_pump(struct grabit_wl_state *s, int timeout_ms) {
	while (wl_display_prepare_read(s->display) != 0) {
		if (wl_display_dispatch_pending(s->display) < 0) return -1;
	}
	if (wl_display_flush(s->display) < 0 && errno != EAGAIN) {
		wl_display_cancel_read(s->display);
		return -1;
	}
	struct pollfd pfd = {.fd = wl_display_get_fd(s->display), .events = POLLIN};
	int pr = poll(&pfd, 1, timeout_ms);
	if (pr > 0 && (pfd.revents & POLLIN)) {
		if (wl_display_read_events(s->display) < 0) return -1;
	} else {
		wl_display_cancel_read(s->display);
	}
	return wl_display_dispatch_pending(s->display) < 0 ? -1 : 0;
}
