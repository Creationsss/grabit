// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/region.h"

#include "config/config.h"
#include "hyprland.h"
#include "log.h"
#include "region/edit_persist.h"
#include "region/wlr_input_state.h"
#include "region/wlr_state.h"
#include "wl/wl.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void gregion_apply_config(struct ro_state *st, struct config *cfg, bool annotate_mode,
						  struct grabit_wl_state *s, const struct rect *snap_rects,
						  size_t n_snap_rects) {
	bool snap_enabled = true;
	if (cfg) {
		const char *v = config_get(cfg, "region.window_snap");
		if (v && strcmp(v, "false") == 0) snap_enabled = false;
		v = config_get(cfg, "region.confirm");
		if (v && strcmp(v, "true") == 0) st->confirm_mode = true;
		v = config_get(cfg, "region.show_coords");
		if (v && strcmp(v, "true") == 0) st->show_coords = true;
		v = config_get(cfg, "edit.instant_capture");
		if (v && strcmp(v, "true") == 0) st->edit_instant = true;
		v = config_get(cfg, "edit.start_with_tool");
		if (annotate_mode && v && strcmp(v, "true") == 0) st->region_locked = true;
		v = config_get(cfg, "edit.toolbar_output");
		if (annotate_mode && v && v[0]) {
			st->tb_out = grabit_wl_output_by_name(s, v);
			st->tb_lock = st->tb_out;
			if (!st->tb_out)
				log_warn("edit.toolbar_output: output `%s` not found; using primary", v);
		}
		v = config_get(cfg, "edit.toolbar_pos");
		if (annotate_mode && v && v[0]) {
			char oname[64];
			int32_t rx, ry;
			if (edit_toolbar_pos_parse(v, oname, sizeof oname, &rx, &ry)) {
				struct grabit_output *go = grabit_wl_output_by_name(s, oname);
				if (go) {
					st->tb_out = go;
					st->tb_x = go->x + rx;
					st->tb_y = go->y + ry;
					st->tb_moved = true;
				}
			}
		}
	}
	if (snap_rects && n_snap_rects > 0) {
		st->snap_windows = malloc(n_snap_rects * sizeof *st->snap_windows);
		if (st->snap_windows) {
			memcpy(st->snap_windows, snap_rects, n_snap_rects * sizeof *st->snap_windows);
			st->n_snap_windows = n_snap_rects;
		}
	} else if (snap_enabled) {
		if (grabit_hyprland_clients(&st->snap_windows, &st->n_snap_windows) != 0) {
			log_debug("region: window snap disabled (no hyprland ipc)");
		}
	}
}

void gregion_create_surfaces(struct ro_state *st, struct grabit_wl_state *s) {
	for (size_t i = 0; i < st->n_outs; i++) {
		struct ro_output *o = &st->outs[i];
		o->st = st;
		o->go = s->outputs[i];
		o->idx = i;

		o->surface = wl_compositor_create_surface(s->compositor);
		o->layer_surface = grabit_wl_layer_fullscreen(
			s, o->surface, o->go->wl_output, "grabit-region",
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE,
			NULL, NULL);
		if (!o->layer_surface) {
			log_error("region: layer_surface creation failed for output %zu", i);
			st->cancelled = true;
			st->finished = true;
			break;
		}
		region_render_attach_layer(o);

		wl_surface_commit(o->surface);
	}
}

void gregion_select_teardown(struct ro_state *st, struct grabit_wl_state *s) {
	st->cleanup = true;
	if (st->pointer) wl_pointer_release(st->pointer);
	if (st->keyboard) wl_keyboard_release(st->keyboard);
	st->pointer = NULL;
	st->keyboard = NULL;
	wl_display_roundtrip(s->display);

	for (size_t i = 0; i < st->n_outs; i++) {
		struct ro_output *o = &st->outs[i];
		grabit_wl_callback_drop(&o->frame_cb);
		if (o->surface && o->buffer) {
			wl_surface_attach(o->surface, NULL, 0, 0);
			wl_surface_commit(o->surface);
		}
	}
	wl_display_roundtrip(s->display);

	for (size_t i = 0; i < st->n_outs; i++) {
		struct ro_output *o = &st->outs[i];
		region_render_free_buffer(o);
		if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(st->outs);
	free(st->snap_windows);

	region_color_picker_release_cache(st);

	if (st->cursor_surface) wl_surface_destroy(st->cursor_surface);
	if (st->cursor_theme) wl_cursor_theme_destroy(st->cursor_theme);

	if (st->xkb_state) xkb_state_unref(st->xkb_state);
	if (st->xkb_keymap) xkb_keymap_unref(st->xkb_keymap);
	if (st->xkb_ctx) xkb_context_unref(st->xkb_ctx);

	free(st->pen_points);
	region_undo_free(st);
	if (st->undo_timer_fd >= 0) close(st->undo_timer_fd);
	if (st->tooltip_timer_fd >= 0) close(st->tooltip_timer_fd);
	if (st->nudge_timer_fd >= 0) close(st->nudge_timer_fd);
}
