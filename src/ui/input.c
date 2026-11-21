// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window.h"

#include "cursor.h"
#include "util/util.h"
#include "wl/wl.h"
#include "wl/xkb.h"

#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#define UI_DOUBLE_CLICK_MS 400

static const struct {
	uint32_t edge;
	enum grabit_cursor_kind cursor;
} HIT[] = {
	[UI_HIT_NONE] = {XDG_TOPLEVEL_RESIZE_EDGE_NONE, GCUR_DEFAULT},
	[UI_HIT_CONTENT] = {XDG_TOPLEVEL_RESIZE_EDGE_NONE, GCUR_DEFAULT},
	[UI_HIT_TITLE] = {XDG_TOPLEVEL_RESIZE_EDGE_NONE, GCUR_DEFAULT},
	[UI_HIT_CLOSE] = {XDG_TOPLEVEL_RESIZE_EDGE_NONE, GCUR_HAND},
	[UI_HIT_N] = {XDG_TOPLEVEL_RESIZE_EDGE_TOP, GCUR_RESIZE_N},
	[UI_HIT_S] = {XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM, GCUR_RESIZE_S},
	[UI_HIT_E] = {XDG_TOPLEVEL_RESIZE_EDGE_RIGHT, GCUR_RESIZE_E},
	[UI_HIT_W] = {XDG_TOPLEVEL_RESIZE_EDGE_LEFT, GCUR_RESIZE_W},
	[UI_HIT_NE] = {XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT, GCUR_RESIZE_NE},
	[UI_HIT_NW] = {XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT, GCUR_RESIZE_NW},
	[UI_HIT_SE] = {XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT, GCUR_RESIZE_SE},
	[UI_HIT_SW] = {XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT, GCUR_RESIZE_SW},
};

static void set_hover(struct ui_window *w, enum ui_hit hit) {
	bool was_close = w->hover == UI_HIT_CLOSE;
	w->hover = hit;
	if (was_close != (hit == UI_HIT_CLOSE)) ui_window_damage(w);
	grabit_cursor_set_apply(&w->cursor, w->enter_serial,
							hit == UI_HIT_CONTENT ? w->content_cursor : HIT[hit].cursor);
}

static void pointer_moved(struct ui_window *w, wl_fixed_t sx, wl_fixed_t sy) {
	w->cx = wl_fixed_to_double(sx);
	w->cy = wl_fixed_to_double(sy);
	if (w->on_motion) w->on_motion(w, w->cx, w->cy - UI_TITLE_H);
	set_hover(w, ui_frame_hit(w, w->cx, w->cy));
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	struct ui_window *w = data;
	w->ptr_focused = surface == w->surface;
	if (!w->ptr_focused) return;
	w->enter_serial = serial;
	grabit_cursor_set_invalidate(&w->cursor);
	pointer_moved(w, sx, sy);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ui_window *w = data;
	if (!w->ptr_focused) return;
	w->ptr_focused = false;
	w->close_armed = false;
	w->content_cursor = GCUR_DEFAULT;
	w->hover = UI_HIT_NONE;
	if (w->on_leave) w->on_leave(w);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ui_window *w = data;
	if (w->ptr_focused) pointer_moved(w, sx, sy);
}

static void title_press(struct ui_window *w, uint32_t serial, uint32_t button) {
	if (button == BTN_RIGHT) {
		xdg_toplevel_show_window_menu(w->toplevel, w->wls->seat, serial, (int32_t)w->cx,
									  (int32_t)w->cy);
		return;
	}
	int64_t now = grabit_now_ms();
	if (now - w->title_click_ms < UI_DOUBLE_CLICK_MS) {
		w->title_click_ms = 0;
		if (w->maximized)
			xdg_toplevel_unset_maximized(w->toplevel);
		else
			xdg_toplevel_set_maximized(w->toplevel);
		return;
	}
	w->title_click_ms = now;
	xdg_toplevel_move(w->toplevel, w->wls->seat, serial);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)time;
	struct ui_window *w = data;
	if (!w->ptr_focused || (button != BTN_LEFT && button != BTN_RIGHT)) return;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		if (w->on_button) w->on_button(w, w->cx, w->cy - UI_TITLE_H, button, false);
		if (!w->close_armed || button != BTN_LEFT) return;
		w->close_armed = false;
		if (w->hover == UI_HIT_CLOSE) w->closed = true;
		ui_window_damage(w);
		return;
	}

	enum ui_hit hit = ui_frame_hit(w, w->cx, w->cy);
	if (hit == UI_HIT_CONTENT) {
		if (w->on_button) w->on_button(w, w->cx, w->cy - UI_TITLE_H, button, true);
		return;
	}
	if (hit == UI_HIT_TITLE) {
		title_press(w, serial, button);
		return;
	}
	if (button != BTN_LEFT) return;
	uint32_t edge = HIT[hit].edge;
	if (edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
		xdg_toplevel_resize(w->toplevel, w->wls->seat, serial, edge);
		return;
	}
	if (hit == UI_HIT_CLOSE) {
		w->close_armed = true;
		ui_window_damage(w);
	}
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)p;
	(void)time;
	struct ui_window *w = data;
	if (!w->ptr_focused || axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !w->on_scroll) return;
	if (ui_frame_hit(w, w->cx, w->cy) != UI_HIT_CONTENT) return;
	w->on_scroll(w, w->cx, w->cy - UI_TITLE_H, wl_fixed_to_double(value));
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
					  int32_t fd, uint32_t size) {
	(void)kb;
	struct ui_window *w = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	grabit_xkb_keymap_from_fd(w->xkb, fd, size, &w->keymap, &w->xkb_state);
}

uint8_t ui_window_mods(const struct ui_window *w) {
	return grabit_xkb_mods(w->xkb_state);
}

static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
					 struct wl_surface *surface, struct wl_array *keys) {
	(void)kb;
	(void)serial;
	(void)keys;
	struct ui_window *w = data;
	w->kb_focused = surface == w->surface;
	if (w->kb_focused && w->on_focus_gained) w->on_focus_gained(w);
}

static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
					 struct wl_surface *surface) {
	(void)kb;
	(void)serial;
	(void)surface;
	struct ui_window *w = data;
	if (!w->kb_focused) return;
	w->kb_focused = false;
	w->repeat_key = 0;
	if (w->on_focus_lost) w->on_focus_lost(w);
	ui_window_damage(w);
}

static bool repeatable(xkb_keysym_t sym) {
	switch (sym) {
	case XKB_KEY_Escape:
	case XKB_KEY_Return:
	case XKB_KEY_space:
	case XKB_KEY_Menu:
		return false;
	default:
		return true;
	}
}

static bool inert(xkb_keysym_t sym) {
	return grabit_xkb_is_modifier(sym) ||
		   (sym >= XKB_KEY_dead_grave && sym <= XKB_KEY_dead_longsolidusoverlay) ||
		   sym == XKB_KEY_Mode_switch || sym == XKB_KEY_Num_Lock;
}

static void dispatch_key(struct ui_window *w, uint32_t key) {
	xkb_keysym_t sym = grabit_xkb_unkeypad(xkb_state_key_get_one_sym(w->xkb_state, key + 8));
	if (inert(sym)) return;
	if (w->on_key && w->on_key(w, sym, grabit_xkb_mods(w->xkb_state))) return;
	if (sym == XKB_KEY_Escape) w->closed = true;
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
				   uint32_t key, uint32_t state) {
	(void)kb;
	(void)time;
	(void)serial;
	struct ui_window *w = data;
	if (!w->kb_focused) return;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (key == w->repeat_key) w->repeat_key = 0;
		return;
	}
	if (!w->xkb_state) return;
	w->repeat_key = 0;
	xkb_keysym_t sym = grabit_xkb_unkeypad(xkb_state_key_get_one_sym(w->xkb_state, key + 8));
	if (w->repeat_rate > 0 && repeatable(sym) && !inert(sym) &&
		xkb_keymap_key_repeats(w->keymap, key + 8)) {
		w->repeat_key = key;
		w->repeat_next_ms = grabit_now_ms() + w->repeat_delay;
	}
	dispatch_key(w, key);
}

void ui_window_set_timer(struct ui_window *w, int delay_ms) {
	w->timer_ms = delay_ms < 0 ? 0 : grabit_now_ms() + delay_ms;
}

static int64_t next_deadline(const struct ui_window *w) {
	int64_t next = w->repeat_key ? w->repeat_next_ms : 0;
	if (w->timer_ms && (!next || w->timer_ms < next)) next = w->timer_ms;
	return next;
}

int ui_input_timeout(struct ui_window *w) {
	int64_t next = next_deadline(w);
	if (!next) return -1;
	int64_t left = next - grabit_now_ms();
	if (left < 0) return 0;
	return left > 1000 ? 1000 : (int)left;
}

void ui_input_fire_timers(struct ui_window *w) {
	if (w->closed) return;
	int64_t now = grabit_now_ms();
	if (w->timer_ms && now >= w->timer_ms) {
		w->timer_ms = 0;
		if (w->on_timer) w->on_timer(w);
	}
	if (!w->repeat_key || !w->xkb_state) return;
	if (now < w->repeat_next_ms) return;
	w->repeat_next_ms = now + (w->repeat_rate < 1000 ? 1000 / w->repeat_rate : 1);
	dispatch_key(w, w->repeat_key);
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t depressed, uint32_t latched, uint32_t locked,
						 uint32_t group) {
	(void)kb;
	(void)serial;
	struct ui_window *w = data;
	if (!w->xkb_state) return;
	xkb_state_update_mask(w->xkb_state, depressed, latched, locked, 0, 0, group);
}

static void kb_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate,
						   int32_t delay) {
	(void)kb;
	struct ui_window *w = data;
	w->repeat_rate = rate > 0 ? rate : 0;
	w->repeat_delay = delay > 0 ? delay : 0;
	if (w->repeat_rate == 0) w->repeat_key = 0;
}

static const struct wl_keyboard_listener keyboard_listener_g = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat_info,
};

void ui_input_attach(struct ui_window *w) {
	if (!w->wls->seat) return;
	if (w->wls->seat_caps & WL_SEAT_CAPABILITY_POINTER) {
		w->pointer = wl_seat_get_pointer(w->wls->seat);
		if (w->pointer) wl_pointer_add_listener(w->pointer, &pointer_listener_g, w);
		grabit_cursor_set_bind(&w->cursor, w->wls, w->pointer, w->scale120 / 120);
	}
	if (w->wls->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD) {
		w->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		w->keyboard = wl_seat_get_keyboard(w->wls->seat);
		w->repeat_rate = 25;
		w->repeat_delay = 600;
		if (w->keyboard && w->xkb)
			wl_keyboard_add_listener(w->keyboard, &keyboard_listener_g, w);
	}
}

void ui_input_detach(struct ui_window *w) {
	grabit_cursor_set_finish(&w->cursor);
	if (w->pointer) wl_pointer_release(w->pointer);
	if (w->keyboard) wl_keyboard_release(w->keyboard);
	xkb_state_unref(w->xkb_state);
	xkb_keymap_unref(w->keymap);
	xkb_context_unref(w->xkb);
	w->pointer = NULL;
	w->keyboard = NULL;
	w->xkb_state = NULL;
	w->keymap = NULL;
	w->xkb = NULL;
}
