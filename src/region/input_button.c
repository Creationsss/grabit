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

bool ginp_toolbar_button_event(struct ro_state *st,
							   uint32_t state) {
	if (!region_editing(st) || st->dragging) return false;
	int32_t tx, ty, tw, th;
	const struct grabit_output *to;
	region_toolbar_rect(st, &to, &tx, &ty, &tw, &th);
	if (!to || !rect_contains((struct rect){tx, ty, tw, th},
							  st->cursor_x, st->cursor_y))
		return false;

	enum tb_action act = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
	if (act == TB_NONE) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
			st->tb_dragging = true;
			st->tb_grab_dx = st->cursor_x - tx;
			st->tb_grab_dy = st->cursor_y - ty;
			region_drag_start(st);
			ginp_refresh_cursor(st);
		}
		return true;
	}

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		if (act == TB_UNDO) region_undo_disarm(st);
		region_render_request_redraw_all(st);
		return true;
	}

	const struct tool_group *grp = toolbar_tool_group(act);
	int32_t stool = toolbar_standalone_tool(act);
	st->tooltip_visible = false;
	if (act != TB_WIDTH_SLIDER) region_tooltip_arm(st);
	if (!grp || grp->btn != st->picker_group) st->picker_group = TB_NONE;
	if (st->text_input_active) region_commit_text(st);
	if (act == TB_REGION) {
		ginp_mode_enter_region(st);
		ginp_refresh_cursor(st);
	} else if (act == TB_EDIT) {
		ginp_mode_enter_anno_edit(st);
		ginp_refresh_cursor(st);
	} else if (grp) {
		bool was_open = st->picker_group == act;
		ginp_mode_select_tool(st, st->group_tool[toolbar_group_index(grp)]);
		st->picker_group = was_open ? TB_NONE : act;
		st->color_picker_open = false;
		st->eyedropper_mode = false;
		ginp_refresh_cursor(st);
	} else if (stool >= 0) {
		ginp_mode_select_tool(st, (enum tool_kind)stool);
		ginp_refresh_cursor(st);
	} else if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE) {
		region_apply_color(st, TOOLBAR_COLORS[act - TB_COLOR_RED], true);
		st->eyedropper_mode = false;
		st->color_picker_open = false;
	} else if (act == TB_COLOR_CURRENT) {
		st->color_picker_open = !st->color_picker_open;
		st->eyedropper_mode = false;
		ginp_refresh_cursor(st);
	} else if (act == TB_WIDTH_SLIDER) {
		ginp_slider_set_width_from_cursor(st, true);
		st->slider_dragging = true;
		region_drag_start(st);
	} else if (act == TB_UNDO) {
		region_undo_pop(st);
		region_undo_arm(st);
	} else if (act == TB_REDO) {
		region_redo_pop(st);
	} else if (act == TB_SAVE) {
		if (st->has_selection) st->finished = true;
	} else if (act == TB_CANCEL) {
		st->cancelled = true;
		st->finished = true;
	}
	region_render_request_redraw_all(st);
	return true;
}

bool ginp_region_abort_active(struct ro_state *st) {
	if (region_drag_active(st) || st->text_input_active) {
		region_drag_abort(st);
		ginp_refresh_cursor(st);
		region_render_request_redraw_all(st);
		return true;
	}
	return false;
}

void ginp_region_do_confirm(struct ro_state *st) {
	if (!st->has_selection && region_editing(st) &&
		st->bounds.w > 0 && st->bounds.h > 0) {
		st->sel_x = st->bounds.x;
		st->sel_y = st->bounds.y;
		st->sel_w = st->bounds.w;
		st->sel_h = st->bounds.h;
		st->has_selection = true;
	}
	if (st->has_selection) {
		st->finished = true;
	} else if (!region_editing(st)) {
		st->cancelled = true;
		st->finished = true;
	}
}

uint32_t ginp_region_nudge_for_key(struct ro_state *st, xkb_keysym_t sym,
								   uint8_t mods) {
	if (region_key_action(&st->keys, KA_NUDGE_LEFT, sym, mods)) return NUDGE_LEFT;
	if (region_key_action(&st->keys, KA_NUDGE_RIGHT, sym, mods)) return NUDGE_RIGHT;
	if (region_key_action(&st->keys, KA_NUDGE_UP, sym, mods)) return NUDGE_UP;
	if (region_key_action(&st->keys, KA_NUDGE_DOWN, sym, mods)) return NUDGE_DOWN;
	return 0;
}
