// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window_internal.h"
#include "xkb_util.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)surf;
	struct ui_window *w = data;
	w->enter_serial = serial;
	w->pointer_x = wl_fixed_to_int(sx);
	w->pointer_y = wl_fixed_to_int(sy);
	if (!w->cursor_active) w->cursor_active = w->cursor_default;
	uiw_apply_cursor(w);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surf) {
	(void)p;
	(void)serial;
	(void)surf;
	struct ui_window *w = data;
	struct ui_pointer_event e = {.kind = UI_PTR_LEAVE};
	if (w->on_pointer) w->on_pointer(w, &e, w->user);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ui_window *w = data;
	w->pointer_x = wl_fixed_to_int(sx);
	w->pointer_y = wl_fixed_to_int(sy);
	struct ui_pointer_event e = {.kind = UI_PTR_MOTION, .x = w->pointer_x, .y = w->pointer_y};
	if (w->on_pointer) w->on_pointer(w, &e, w->user);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	struct ui_window *w = data;
	struct ui_pointer_event e = {
		.kind = UI_PTR_BUTTON,
		.x = w->pointer_x,
		.y = w->pointer_y,
		.button = button,
		.pressed = state == WL_POINTER_BUTTON_STATE_PRESSED,
	};
	if (w->on_pointer) w->on_pointer(w, &e, w->user);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)p;
	(void)time;
	struct ui_window *w = data;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
	struct ui_pointer_event e = {
		.kind = UI_PTR_AXIS,
		.x = w->pointer_x,
		.y = w->pointer_y,
		.axis = wl_fixed_to_double(value),
	};
	if (w->on_pointer) w->on_pointer(w, &e, w->user);
}

static void pointer_noop_frame(void *d, struct wl_pointer *p) {
	(void)d;
	(void)p;
}
static void pointer_noop_axis_source(void *d, struct wl_pointer *p, uint32_t s) {
	(void)d;
	(void)p;
	(void)s;
}
static void pointer_noop_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {
	(void)d;
	(void)p;
	(void)t;
	(void)a;
}
static void pointer_noop_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {
	(void)d;
	(void)p;
	(void)a;
	(void)v;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_noop_frame,
	.axis_source = pointer_noop_axis_source,
	.axis_stop = pointer_noop_axis_stop,
	.axis_discrete = pointer_noop_axis_discrete,
};

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
					  int32_t fd, uint32_t size) {
	(void)kb;
	struct ui_window *w = data;
	grabit_xkb_load(w->xkb_ctx, format, fd, size, &w->xkb_keymap, &w->xkb_state);
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
				   uint32_t key, uint32_t state) {
	(void)kb;
	(void)serial;
	(void)time;
	struct ui_window *w = data;
	if (!w->xkb_state || state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
	struct ui_key_event e = {
		.sym = xkb_state_key_get_one_sym(w->xkb_state, key + 8),
		.ctrl = w->ctrl_held,
		.shift = w->shift_held,
	};
	int n = xkb_state_key_get_utf8(w->xkb_state, key + 8, e.utf8, sizeof e.utf8);
	if (n <= 0 || (unsigned char)e.utf8[0] < 0x20) e.utf8[0] = '\0';
	if (w->on_key) w->on_key(w, &e, w->user);
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t dep, uint32_t lat, uint32_t lck, uint32_t group) {
	(void)kb;
	(void)serial;
	struct ui_window *w = data;
	if (!w->xkb_state) return;
	xkb_state_update_mask(w->xkb_state, dep, lat, lck, 0, 0, group);
	w->ctrl_held = xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_CTRL,
												XKB_STATE_MODS_EFFECTIVE) > 0;
	w->shift_held = xkb_state_mod_name_is_active(w->xkb_state, XKB_MOD_NAME_SHIFT,
												 XKB_STATE_MODS_EFFECTIVE) > 0;
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su,
					 struct wl_array *keys) {
	(void)d;
	(void)k;
	(void)s;
	(void)su;
	(void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *su) {
	(void)d;
	(void)k;
	(void)s;
	(void)su;
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t delay) {
	(void)d;
	(void)k;
	(void)r;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener_g = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat,
};

void uiw_input_attach(struct ui_window *w) {
	if (w->pointer) wl_pointer_add_listener(w->pointer, &pointer_listener_g, w);
	if (w->keyboard) wl_keyboard_add_listener(w->keyboard, &keyboard_listener_g, w);
}
