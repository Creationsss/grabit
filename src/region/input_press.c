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

void ginp_pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						 uint32_t time, uint32_t button, uint32_t state) {
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup) return;
	st->resizing_anno = false;

	if (button != BTN_LEFT) {
		if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
		if (region_button_action(&st->keys, KA_CANCEL, button)) {
			if (!ginp_region_abort_active(st, p)) {
				st->cancelled = true;
				st->finished = true;
			}
		} else if (region_button_action(&st->keys, KA_CONFIRM, button)) {
			if (!region_drag_active(st) && !st->text_input_active)
				ginp_region_do_confirm(st);
		}
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->tb_dragging) {
		st->tb_dragging = false;
		ginp_refresh_cursor(st, p);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->slider_dragging) {
		st->slider_dragging = false;
		region_render_request_redraw_all(st);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->color_picker_dragging) {
		st->color_picker_dragging = false;
		region_render_request_redraw_all(st);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
		(st->drawing || st->moving_region || region_anno_dragging(st) ||
		 st->handle_dragging != HANDLE_NONE)) {
		if (st->moving_region) {
			st->moving_region = false;
			region_undo_commit(st);
			if (!region_editing(st) && !st->region_moved) st->last_inside_press = time;
			ginp_refresh_cursor(st, p);
		} else if (st->handle_dragging != HANDLE_NONE) {
			st->handle_dragging = HANDLE_NONE;
			region_undo_commit(st);
			ginp_refresh_cursor(st, p);
		} else if (region_anno_dragging(st)) {
			bool was_move = st->anno_drag == ANNO_DRAG_MOVE;
			st->anno_drag = ANNO_DRAG_NONE;
			if (was_move) {
				int32_t dx = st->anno_last_x - st->anno_press_x;
				int32_t dy = st->anno_last_y - st->anno_press_y;
				region_undo_group_begin(st);
				if (st->out_annos)
					for (size_t i = 0; i < st->out_annos->n; i++)
						if (st->out_annos->items[i].selected)
							region_undo_record_anno_move(st, i, dx, dy);
				region_undo_group_end(st);
			} else if (st->sel_anno >= 0) {
				region_undo_record_anno_geom(st, (size_t)st->sel_anno,
											 st->anno_geom_snap);
			}
			if (st->out_annos) st->out_annos->gen++;
			ginp_refresh_cursor(st, p);
		} else if (st->drawing) {
			region_commit_drawing(st);
		}
		region_render_request_redraw_all(st);
		return;
	}

	if (st->color_picker_open && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		struct rect pr, ir, er;
		region_color_picker_rect(st, &pr.x, &pr.y, &pr.w, &pr.h);
		region_color_input_rect(st, &ir.x, &ir.y, &ir.w, &ir.h);
		region_color_eyedropper_rect(st, &er.x, &er.y, &er.w, &er.h);
		bool inside_grid = pr.w > 0 && pr.h > 0 &&
						   rect_contains(pr, st->cursor_x, st->cursor_y);
		bool inside_input = ir.w > 0 && ir.h > 0 &&
							rect_contains(ir, st->cursor_x, st->cursor_y);
		bool inside_eyedropper = er.w > 0 && er.h > 0 &&
								 rect_contains(er, st->cursor_x, st->cursor_y);
		if (inside_eyedropper) {
			st->eyedropper_mode = !st->eyedropper_mode;
			st->color_input_active = false;
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_grid) {
			st->color_input_active = false;
			uint32_t picked = 0;
			if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked))
				region_apply_color(st, picked, true);
			st->color_picker_dragging = true;
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_input) {
			st->color_input_active = true;
			snprintf(st->color_input_buf, sizeof st->color_input_buf,
					 "%06X", region_active_color(st) & 0xFFFFFFu);
			st->color_input_len = 6;
			region_render_request_redraw_all(st);
			return;
		}
		if (st->color_input_active) {
			uint32_t parsed = 0;
			if (region_parse_hex_color(st->color_input_buf, &parsed))
				region_apply_color(st, parsed, true);
			st->color_input_active = false;
			st->color_input_len = 0;
		}
		if (st->eyedropper_mode) {
			/* fall through to eyedropper sample below; keep panel open */
		} else if (!region_toolbar_contains(st, st->cursor_x, st->cursor_y)) {
			st->color_picker_open = false;
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (st->picker_group != TB_NONE && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		int val = 0;
		enum tool_picker_kind k =
			region_tool_picker_hit(st, st->cursor_x, st->cursor_y, &val);
		if (k == TP_TOOL) {
			ginp_mode_select_tool(st, (enum tool_kind)val);
			st->picker_group = TB_NONE;
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (k == TP_STYLE) {
			st->current_style = (enum stroke_style)val;
			region_render_request_redraw_all(st);
			return;
		}
		struct rect pr;
		region_tool_picker_rect(st, &pr.x, &pr.y, &pr.w, &pr.h);
		if (pr.w > 0 && rect_contains(pr, st->cursor_x, st->cursor_y))
			return;
		if (!region_toolbar_contains(st, st->cursor_x, st->cursor_y)) {
			st->picker_group = TB_NONE;
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (ginp_toolbar_button_event(st, p, state)) return;

	if (region_editing(st) && st->eyedropper_mode &&
		state == WL_POINTER_BUTTON_STATE_PRESSED) {
		uint32_t picked = 0;
		if (ginp_eyedropper_sample(st, &picked))
			region_apply_color(st, picked, true);
		st->eyedropper_mode = false;
		ginp_refresh_cursor(st, p);
		region_render_request_redraw_all(st);
		return;
	}

	if (!st->region_locked) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
			region_undo_begin(st);
			st->dragging = true;
			st->drag_x0 = st->cursor_x;
			st->drag_y0 = st->cursor_y;
			region_update_selection(st);
		} else {
			st->dragging = false;
			if (st->has_selection && (st->sel_w < 8 || st->sel_h < 8)) {
				st->has_selection = false;
				st->sel_w = st->sel_h = 0;
			}
			if (!st->has_selection) {
				int hit = region_snap_hit(st, st->cursor_x, st->cursor_y);
				if (hit >= 0 && (size_t)hit < st->n_snap_windows) {
					const struct rect *w = &st->snap_windows[hit];
					st->sel_x = w->x;
					st->sel_y = w->y;
					st->sel_w = w->w;
					st->sel_h = w->h;
					st->has_selection = true;
					st->snap_hover = -1;
				}
			}
			region_undo_commit(st);
			if (st->has_selection) ginp_lock_or_finish(st);
		}
		region_render_request_redraw_all(st);
		return;
	}

	if (st->text_input_active) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) region_commit_text(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		int h = region_handle_at(st, st->cursor_x, st->cursor_y);
		if (h != HANDLE_NONE) {
			region_undo_begin(st);
			st->handle_dragging = h;
			region_drag_start(st);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->anno_edit_mode) {
			const struct annotation *a = region_anno_selected(st);
			int c = ginp_anno_corner_at(st, st->cursor_x, st->cursor_y);
			if (a && c >= 0 && region_single_selection(st)) {
				st->anno_geom_snap[0] = a->x0;
				st->anno_geom_snap[1] = a->y0;
				st->anno_geom_snap[2] = a->x1;
				st->anno_geom_snap[3] = a->y1;
				st->anno_drag = c;
				st->out_annos->gen++;
				region_drag_start(st);
			} else {
				int hit = ginp_anno_hit_index(st, st->cursor_x, st->cursor_y);
				if (region_multi_select_held(st) && hit >= 0) {
					region_select_toggle(st, (size_t)hit);
					st->out_annos->gen++;
				} else if (hit >= 0) {
					if (!st->out_annos->items[hit].selected)
						region_select_one(st, (size_t)hit);
					else
						st->sel_anno = hit;
					st->anno_drag = ANNO_DRAG_MOVE;
					st->anno_press_x = st->anno_last_x = st->cursor_x;
					st->anno_press_y = st->anno_last_y = st->cursor_y;
					st->out_annos->gen++;
					region_drag_start(st);
				} else {
					region_clear_selection(st);
				}
			}
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->has_selection && !region_editing(st) &&
			!region_inside_selection(st, st->cursor_x, st->cursor_y)) {
			st->region_locked = false;
			region_undo_begin(st);
			st->dragging = true;
			st->drag_x0 = st->cursor_x;
			st->drag_y0 = st->cursor_y;
			region_update_selection(st);
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (!region_editing(st)) {
			if (st->last_inside_press != 0 &&
				time - st->last_inside_press <= DOUBLE_CLICK_MS) {
				st->finished = true;
				return;
			}
			st->last_inside_press = 0;
		}
		if ((st->ctrl_held || !region_editing(st)) && st->has_selection) {
			region_undo_begin(st);
			st->moving_region = true;
			st->region_moved = false;
			st->move_grab_dx = st->cursor_x - st->sel_x;
			st->move_grab_dy = st->cursor_y - st->sel_y;
			region_drag_start(st);
			ginp_refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->current_tool == TOOL_TEXT) {
			st->text_input_active = true;
			st->text_x = st->cursor_x;
			st->text_y = st->cursor_y;
			st->text_len = 0;
			st->text_buf[0] = '\0';
			region_drag_start(st);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->current_tool == TOOL_COUNTER) {
			region_place_counter(st);
			region_render_request_redraw_all(st);
			return;
		}
		st->drawing = true;
		st->draw_x0 = st->cursor_x;
		st->draw_y0 = st->cursor_y;
		if (tool_uses_points(st->current_tool)) {
			st->pen_n = 0;
			region_pen_append(st, st->draw_x0, st->draw_y0);
		}
		region_drag_start(st);
	} else {
		if (st->moving_region) {
			st->moving_region = false;
			ginp_refresh_cursor(st, p);
		} else if (st->handle_dragging != HANDLE_NONE) {
			st->handle_dragging = HANDLE_NONE;
			ginp_refresh_cursor(st, p);
		} else if (st->drawing) {
			region_commit_drawing(st);
		}
	}
	region_render_request_redraw_all(st);
}
