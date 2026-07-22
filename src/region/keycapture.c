// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "cursor.h"
#include "hyprland.h"
#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define KC_MAX_CAPS 12

#include "region/keycapture_internal.h"

int region_keybind_watch(struct grabit_wl_state *s, const char *action_key,
						 char *out, size_t out_size) {
	if (!s->layer_shell || !s->compositor || !s->shm || !s->seat) {
		log_error("watch: compositor lacks layer-shell/seat support");
		return -1;
	}
	if (!(s->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		log_error("watch: no keyboard on seat");
		return -1;
	}

	struct kc_state st = {.wls = s, .scale = 1};

	int32_t cpx = 0, cpy = 0;
	if (grabit_hyprland_cursorpos(&cpx, &cpy) == 0) st.go = grabit_wl_output_at(s, cpx, cpy);
	if (!st.go) st.go = grabit_wl_primary_output(s);
	if (!st.go) {
		log_error("watch: no output");
		return -1;
	}

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) return -1;

	st.surface = wl_compositor_create_surface(s->compositor);
	st.layer = grabit_wl_layer_fullscreen(
		s, st.surface, st.go->wl_output, "grabit-keycapture",
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE, &gkc_layer_listener, &st);
	if (!st.layer) {
		wl_surface_destroy(st.surface);
		xkb_context_unref(st.xkb_ctx);
		return -1;
	}
	wl_surface_commit(st.surface);

	st.keyboard = wl_seat_get_keyboard(s->seat);
	if (st.keyboard) wl_keyboard_add_listener(st.keyboard, &gkc_kb_listener, &st);
	if (s->seat_caps & WL_SEAT_CAPABILITY_POINTER) {
		st.pointer = wl_seat_get_pointer(s->seat);
		if (st.pointer) {
			wl_pointer_add_listener(st.pointer, &gkc_ptr_listener, &st);
			st.cursor_theme = grabit_cursor_theme_load(s->shm, st.go->scale > 0 ? st.go->scale : 1);
			if (st.cursor_theme) {
				st.cursor = grabit_cursor_load_default(st.cursor_theme);
				if (st.cursor)
					st.cursor_surface = wl_compositor_create_surface(s->compositor);
			}
		}
	}

	log_info("watching for `%s`: press keys/mouse buttons, Enter saves, Esc cancels",
			 action_key);

	while (!st.done && grabit_wl_pump(s, -1) >= 0)
		;

	int rc;
	if (st.cancelled || st.n_caps == 0) {
		rc = 1;
	} else {
		gkc_join(&st, out, out_size);
		rc = 0;
	}

	if (st.pointer) wl_pointer_release(st.pointer);
	if (st.keyboard) wl_keyboard_release(st.keyboard);
	if (st.cursor_surface) wl_surface_destroy(st.cursor_surface);
	if (st.cursor_theme) wl_cursor_theme_destroy(st.cursor_theme);
	if (st.xkb_state) xkb_state_unref(st.xkb_state);
	if (st.xkb_keymap) xkb_keymap_unref(st.xkb_keymap);
	xkb_context_unref(st.xkb_ctx);
	grabit_shm_release(&st.buffer, &st.buf_data, &st.buf_size);
	zwlr_layer_surface_v1_destroy(st.layer);
	wl_surface_destroy(st.surface);
	wl_display_roundtrip(s->display);
	return rc;
}
