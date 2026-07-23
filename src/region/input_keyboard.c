// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "capture/capture.h"
#include "cursor.h"
#include "region/annotate.h"
#include "region/toolbar_internal.h"
#include "region/wlr_input_state.h"
#include "wl/wl.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#define DOUBLE_CLICK_MS 400

#include "region/input_internal.h"

static void keyboard_keymap(void *data, struct wl_keyboard *kb,
							uint32_t format, int32_t fd, uint32_t size) {
	(void)kb;
	struct ro_state *st = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	region_xkb_keymap_from_fd(st->xkb_ctx, fd, size, &st->xkb_keymap, &st->xkb_state);
}

static void keyboard_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
						   struct wl_surface *surface, struct wl_array *keys) {
	(void)data;
	(void)kb;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
						   struct wl_surface *surface) {
	(void)kb;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	region_nudge_disarm(st);
	region_undo_disarm(st);
}

static void handle_text_input(struct ro_state *st, xkb_keysym_t sym, uint32_t key) {
	if (sym == XKB_KEY_BackSpace) {
		if (st->text_len > 0) {
			st->text_len--;
			st->text_buf[st->text_len] = '\0';
		}
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		region_commit_text(st);
		return;
	}
	if (sym == XKB_KEY_Escape) {
		st->text_input_active = false;
		st->text_len = 0;
		return;
	}
	char buf[8];
	int n = xkb_state_key_get_utf8(st->xkb_state, key + 8, buf, sizeof buf);
	if (n <= 0) return;
	if ((unsigned char)buf[0] < 0x20) return;
	if (st->text_len + (size_t)n + 1 > sizeof st->text_buf) return;
	memcpy(st->text_buf + st->text_len, buf, (size_t)n);
	st->text_len += (size_t)n;
	st->text_buf[st->text_len] = '\0';
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t time, uint32_t key, uint32_t state) {
	(void)kb;
	(void)serial;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!st->xkb_state) return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(st->xkb_state, key + 8);
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		uint8_t rmods = region_xkb_mods(st->xkb_state);
		if (region_key_action(&st->keys, KA_UNDO, sym, rmods))
			region_undo_disarm(st);
		uint32_t dir = ginp_region_nudge_for_key(st, sym, rmods);
		if (dir) region_nudge_release(st, dir);
		return;
	}

	if (st->text_input_active) {
		handle_text_input(st, sym, key);
		region_render_request_redraw_all(st);
		return;
	}

	if (st->color_input_active) {
		if (sym == XKB_KEY_Escape) {
			st->color_input_active = false;
			st->color_input_len = 0;
		} else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
			uint32_t parsed = 0;
			if (region_parse_hex_color(st->color_input_buf, &parsed)) {
				st->current_color = parsed;
				st->edit_choices_dirty = true;
			}
			st->color_input_active = false;
			st->color_input_len = 0;
		} else if (sym == XKB_KEY_BackSpace) {
			if (st->color_input_len > 0) {
				st->color_input_len--;
				st->color_input_buf[st->color_input_len] = '\0';
			}
		} else {
			char buf[8];
			int n = xkb_state_key_get_utf8(st->xkb_state, key + 8, buf, sizeof buf);
			if (n == 1) {
				char c = buf[0];
				bool is_hex = (c >= '0' && c <= '9') ||
							  (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
				if (is_hex && st->color_input_len < 6) {
					st->color_input_buf[st->color_input_len++] =
						(char)((c >= 'a' && c <= 'f') ? c - 32 : c);
					st->color_input_buf[st->color_input_len] = '\0';
				}
			}
		}
		region_render_request_redraw_all(st);
		return;
	}

	uint8_t mods = region_xkb_mods(st->xkb_state);

	if (region_key_action(&st->keys, KA_CANCEL, sym, mods)) {
		if (!ginp_region_abort_active(st, st->pointer)) {
			st->cancelled = true;
			st->finished = true;
		}
		return;
	}
	if (region_key_action(&st->keys, KA_CONFIRM, sym, mods)) {
		ginp_region_do_confirm(st);
		return;
	}
	if (region_key_action(&st->keys, KA_SELECT_ALL, sym, mods)) {
		if (region_drag_active(st) || st->n_outs == 0) return;
		struct rect mon;
		grabit_output_rect(st->cursor_on ? st->cursor_on->go : st->outs[0].go, &mon);
		region_undo_begin(st);
		st->sel_x = mon.x;
		st->sel_y = mon.y;
		st->sel_w = mon.w;
		st->sel_h = mon.h;
		st->has_selection = true;
		st->snap_hover = -1;
		st->dragging = false;
		region_undo_commit(st);
		ginp_lock_or_finish(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (st->region_locked && st->has_selection && !region_drag_active(st)) {
		uint32_t dir = ginp_region_nudge_for_key(st, sym, mods);
		if (dir) {
			region_nudge_press(st, dir);
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (!region_editing(st)) return;

	if (region_key_action(&st->keys, KA_UNDO, sym, mods)) {
		if (region_drag_active(st)) return;
		region_undo_pop(st);
		region_undo_arm(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (region_drag_active(st)) return;

	if (region_key_action(&st->keys, KA_EDIT_MODE, sym, mods)) {
		ginp_mode_enter_anno_edit(st);
		if (st->pointer) ginp_refresh_cursor(st, st->pointer);
		region_render_request_redraw_all(st);
		return;
	}

	if (region_key_action(&st->keys, KA_REGION_MODE, sym, mods)) {
		ginp_mode_enter_region(st);
		if (st->pointer) ginp_refresh_cursor(st, st->pointer);
		region_render_request_redraw_all(st);
		return;
	}

	int32_t pick = region_key_tool(&st->keys, sym, mods);
	if (pick >= 0 && pick < TOOL_COUNT) {
		ginp_mode_select_tool(st, (enum tool_kind)pick);
		if (st->pointer) ginp_refresh_cursor(st, st->pointer);
		region_render_request_redraw_all(st);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
							   uint32_t mods_depressed, uint32_t mods_latched,
							   uint32_t mods_locked, uint32_t group) {
	(void)kb;
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (st->xkb_state) {
		xkb_state_update_mask(st->xkb_state, mods_depressed, mods_latched,
							  mods_locked, 0, 0, group);
		st->shift_held = xkb_state_mod_name_is_active(
							 st->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
		st->ctrl_held = xkb_state_mod_name_is_active(
							st->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
		bool alt_now = xkb_state_mod_name_is_active(
						   st->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
		if (alt_now != st->alt_held) {
			st->alt_held = alt_now;
			region_render_request_redraw_all(st);
		}
		if (st->pointer) ginp_refresh_cursor(st, st->pointer);
	}
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
								 int32_t rate, int32_t delay) {
	(void)data;
	(void)kb;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener_g = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

void region_input_attach(struct ro_state *st) {
	wl_pointer_add_listener(st->pointer, &ginp_pointer_listener_g, st);
	wl_keyboard_add_listener(st->keyboard, &keyboard_listener_g, st);
}
