// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "wl/wl.h"

#include "log.h"
#include "wl/internal.h"

#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "cursor-shape-v1-client-protocol.h"
#include "ext-data-control-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	(void)seat;
	struct grabit_wl_state *s = data;
	s->seat_caps = caps;
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener_g = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
							const char *interface, uint32_t version) {
	struct grabit_wl_state *s = data;

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		s->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
		return;
	}

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t v = version > 4 ? 4 : version;
		s->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
		return;
	}

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		if (s->seat) return;
		uint32_t v = version > 3 ? 3 : version;
		s->seat = wl_registry_bind(reg, name, &wl_seat_interface, v);
		wl_seat_add_listener(s->seat, &seat_listener_g, s);
		return;
	}

	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct grabit_output *o = calloc(1, sizeof *o);
		if (!o) {
			log_warn("wl: oom allocating output %u; skipping", name);
			return;
		}
		o->state = s;
		o->scale = 1;
		o->global_name = name;
		uint32_t v = version > 4 ? 4 : version;
		o->wl_output = wl_registry_bind(reg, name, &wl_output_interface, v);
		wl_output_add_listener(o->wl_output, &grabit_wl_output_listener, o);
		if (gwl_outputs_push(s, o) != 0) {
			wl_output_destroy(o->wl_output);
			free(o);
			return;
		}
		gwl_output_attach_xdg(s, o);
		return;
	}

	if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		uint32_t v = version > 3 ? 3 : version;
		s->screencopy_manager = wl_registry_bind(
			reg, name, &zwlr_screencopy_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->ext_copy_manager = wl_registry_bind(
			reg, name, &ext_image_copy_capture_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->ext_source_manager = wl_registry_bind(
			reg, name, &ext_output_image_capture_source_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		s->toplevel_manager_name = name;
		s->toplevel_manager_version = version > 3 ? 3 : version;
		return;
	}

	if (strcmp(interface, ext_data_control_manager_v1_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->ext_data_control_manager = wl_registry_bind(
			reg, name, &ext_data_control_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
		uint32_t v = version > 2 ? 2 : version;
		s->data_control_manager = wl_registry_bind(
			reg, name, &zwlr_data_control_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		uint32_t v = version > 4 ? 4 : version;
		s->layer_shell = wl_registry_bind(
			reg, name, &zwlr_layer_shell_v1_interface, v);
		return;
	}

	if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		uint32_t v = version > 3 ? 3 : version;
		s->xdg_output_manager = wl_registry_bind(
			reg, name, &zxdg_output_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, wp_viewporter_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, v);
		return;
	}

	if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->fractional_scale_manager = wl_registry_bind(
			reg, name, &wp_fractional_scale_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
		uint32_t v = version > 1 ? 1 : version;
		s->cursor_shape_manager = wl_registry_bind(
			reg, name, &wp_cursor_shape_manager_v1_interface, v);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)reg;
	struct grabit_wl_state *s = data;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->dead || o->global_name != name) continue;
		o->dead = true;
		log_warn("wl: output %s removed mid-session", o->name ? o->name : "?");
		if (o->xdg_output) {
			zxdg_output_v1_destroy(o->xdg_output);
			o->xdg_output = NULL;
		}
		if (o->wl_output) {
			wl_output_destroy(o->wl_output);
			o->wl_output = NULL;
		}
		s->outputs_serial++;
		return;
	}
}

const struct wl_registry_listener grabit_wl_registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};
