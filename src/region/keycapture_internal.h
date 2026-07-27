// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_KEYCAPTURE_INTERNAL_H
#define GRABIT_REGION_KEYCAPTURE_INTERNAL_H

struct kc_state {
	struct grabit_wl_state *wls;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct grabit_shm_buf buf;
	int32_t scale;
	bool mapped;

	struct wl_keyboard *keyboard;
	struct wl_pointer *pointer;
	struct wp_cursor_shape_device_v1 *cursor_shape;
	struct wl_surface *cursor_surface;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor;
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct keybind caps[KC_MAX_CAPS];
	size_t n_caps;
	bool done;
	bool cancelled;
};

extern const struct zwlr_layer_surface_v1_listener gkc_layer_listener;
extern const struct wl_keyboard_listener gkc_kb_listener;
extern const struct wl_pointer_listener gkc_ptr_listener;

void gkc_join(struct kc_state *s, char *out, size_t n);
void gkc_add(struct kc_state *s, const struct keybind *b);

#endif
