// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "capture/capture.h"
#include "cursor.h"
#include "region/annotate.h"
#include "region/toolbar_internal.h"
#include "region/wlr_input_state.h"
#include "wl.h"

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#define DOUBLE_CLICK_MS 400

static void apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
						 struct ro_output *o, struct wl_cursor *c);

static bool eyedropper_sample(struct ro_state *st, uint32_t *out_color) {
	if (!st->cursor_on || !st->frozen) return false;
	struct ro_output *ro = st->cursor_on;
	const struct image *img = &st->frozen[ro->idx];
	if (!img->bytes || img->stride <= 0) return false;
	int32_t scale = ro->go->scale > 0 ? ro->go->scale : 1;
	int32_t px = (st->cursor_x - ro->go->x) * scale;
	int32_t py = (st->cursor_y - ro->go->y) * scale;
	if (px < 0 || py < 0 || px >= img->width || py >= img->height) return false;
	const uint8_t *row = (const uint8_t *)img->bytes + (size_t)py * (size_t)img->stride;
	uint32_t pixel = ((const uint32_t *)row)[px];
	*out_color = pixel & 0xFFFFFFu;
	return true;
}

static int anno_hit_index(const struct ro_state *st, int32_t x, int32_t y) {
	if (!st->out_annos) return -1;
	for (size_t i = st->out_annos->n; i > 0; i--) {
		if (annotation_hit(&st->out_annos->items[i - 1], x, y))
			return (int)(i - 1);
	}
	return -1;
}

static int anno_corner_at(const struct ro_state *st, int32_t x, int32_t y) {
	const struct annotation *a = region_anno_selected(st);
	if (!a) return -1;
	int mask = annotation_corner_mask(a);
	for (int c = 0; c < 4; c++) {
		if (!(mask & (1 << c))) continue;
		int32_t dx = x - annotation_corner_x(a, c);
		int32_t dy = y - annotation_corner_y(a, c);
		if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS) return c;
	}
	return -1;
}

static bool toolbar_reachable(const struct ro_state *st) {
	return region_editing(st) && !st->dragging && !st->tb_dragging &&
		   !st->drawing && !st->moving_region && !st->slider_dragging &&
		   !region_anno_dragging(st) &&
		   !st->text_input_active && st->handle_dragging == HANDLE_NONE;
}

static void mode_enter_region(struct ro_state *st) {
	st->region_locked = false;
	st->anno_edit_mode = false;
	st->sel_anno = -1;
	st->color_picker_open = false;
	st->eyedropper_mode = false;
}

static void mode_enter_anno_edit(struct ro_state *st) {
	st->region_locked = true;
	st->anno_edit_mode = true;
	st->color_picker_open = false;
	st->eyedropper_mode = false;
}

static void mode_select_tool(struct ro_state *st, enum tool_kind t) {
	st->current_tool = t;
	st->region_locked = true;
	st->anno_edit_mode = false;
	st->sel_anno = -1;
	st->edit_choices_dirty = true;
}

static struct wl_cursor *pick_cursor(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	if (st->tb_dragging && st->cursor_move) return st->cursor_move;
	if (toolbar_reachable(st) && region_toolbar_contains(st, abs_x, abs_y)) {
		enum tb_action a = region_toolbar_hit(st, abs_x, abs_y);
		if (a != TB_NONE && st->cursor_hand) return st->cursor_hand;
		if (st->cursor_move) return st->cursor_move;
		if (st->cursor_default) return st->cursor_default;
	}
	if (st->region_locked) {
		if (st->moving_region && st->cursor_move) return st->cursor_move;
		if (st->handle_dragging >= 0 && st->handle_dragging < 8 &&
			st->cursor_resize[st->handle_dragging])
			return st->cursor_resize[st->handle_dragging];
		if (st->eyedropper_mode) return st->cursor;
		int h = region_handle_at(st, abs_x, abs_y);
		if (h != HANDLE_NONE && st->cursor_resize[h]) return st->cursor_resize[h];
		if (h != HANDLE_NONE && st->cursor_default) return st->cursor_default;
		if (st->anno_edit_mode) {
			bool grab = region_anno_dragging(st) ||
						anno_corner_at(st, abs_x, abs_y) >= 0 ||
						anno_hit_index(st, abs_x, abs_y) >= 0;
			if (grab && st->cursor_move) return st->cursor_move;
			return st->cursor_default ? st->cursor_default : st->cursor;
		}
		if ((st->ctrl_held || !region_editing(st)) &&
			region_inside_selection(st, abs_x, abs_y) && st->cursor_move)
			return st->cursor_move;
		if (!region_editing(st)) return st->cursor;
		if (st->current_tool == TOOL_TEXT && st->cursor_text) return st->cursor_text;
		return st->cursor_default ? st->cursor_default : st->cursor;
	}
	return st->cursor;
}

static void refresh_cursor(struct ro_state *st, struct wl_pointer *p) {
	if (!st->cursor_on) return;
	struct wl_cursor *want = pick_cursor(st, st->cursor_x, st->cursor_y);
	if (want == st->current_cursor) return;
	st->current_cursor = want;
	if (st->last_cursor_serial == 0) return;
	apply_cursor(st, p, st->last_cursor_serial, st->cursor_on, want);
}

static void lock_or_finish(struct ro_state *st) {
	if (region_editing(st) || st->confirm_mode) {
		st->region_locked = true;
		if (st->pointer) refresh_cursor(st, st->pointer);
	} else {
		st->finished = true;
	}
}

static void apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
						 struct ro_output *o, struct wl_cursor *c) {
	grabit_cursor_apply(p, serial, st->cursor_surface, c, o->scale);
}

static void slider_set_width_from_cursor(struct ro_state *st) {
	int32_t sx, sy, sw, sh;
	region_toolbar_slider_rect(st, &sx, &sy, &sw, &sh);
	(void)sy;
	(void)sh;
	double frac = sw > 0 ? (double)(st->cursor_x - sx) / (double)sw : 0;
	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	st->current_width = WIDTH_MIN + (int32_t)(frac * (WIDTH_MAX - WIDTH_MIN) + 0.5);
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_state *st = data;
	if (st->cleanup) return;
	struct ro_output *o = region_render_find_by_surface(st, surface);
	if (!o) return;
	st->cursor_on = o;
	st->cursor_x = o->go->x + wl_fixed_to_int(sx);
	st->cursor_y = o->go->y + wl_fixed_to_int(sy);
	st->last_cursor_serial = serial;
	apply_cursor(st, p, serial, o, pick_cursor(st, st->cursor_x, st->cursor_y));
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	st->cursor_on = NULL;
	if (region_set_hover(st, -1)) region_render_request_redraw_all(st);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!st->cursor_on) return;
	st->cursor_x = st->cursor_on->go->x + wl_fixed_to_int(sx);
	st->cursor_y = st->cursor_on->go->y + wl_fixed_to_int(sy);

	if (st->tb_dragging) {
		st->tb_out = st->cursor_on->go;
		st->tb_x = st->cursor_x - st->tb_grab_dx;
		st->tb_y = st->cursor_y - st->tb_grab_dy;
		st->tb_moved = true;
	} else if (st->slider_dragging) {
		slider_set_width_from_cursor(st);
	} else if (st->color_picker_dragging) {
		uint32_t picked = 0;
		if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked)) {
			st->current_color = picked;
			st->edit_choices_dirty = true;
		}
	} else if (st->region_locked) {
		if (st->moving_region) {
			int32_t px = st->sel_x, py = st->sel_y;
			st->sel_x = st->cursor_x - st->move_grab_dx;
			st->sel_y = st->cursor_y - st->move_grab_dy;
			region_clamp_move(st);
			if (st->sel_x != px || st->sel_y != py) st->region_moved = true;
		} else if (st->handle_dragging != HANDLE_NONE) {
			region_apply_handle_drag(st);
		} else if (st->anno_drag == ANNO_DRAG_MOVE) {
			struct annotation *a = region_anno_selected(st);
			if (a) {
				annotation_translate(a, st->cursor_x - st->anno_last_x,
									 st->cursor_y - st->anno_last_y);
			}
			st->anno_last_x = st->cursor_x;
			st->anno_last_y = st->cursor_y;
		} else if (st->anno_drag >= 0) {
			struct annotation *a = region_anno_selected(st);
			if (a) {
				if (st->anno_drag & 1)
					a->x1 = st->cursor_x;
				else
					a->x0 = st->cursor_x;
				if (st->anno_drag & 2)
					a->y1 = st->cursor_y;
				else
					a->y0 = st->cursor_y;
				annotation_update_bbox(a);
			}
		} else if (st->drawing && tool_uses_points(st->current_tool)) {
			region_pen_append(st, st->cursor_x, st->cursor_y);
		}
	} else {
		if (st->dragging) region_update_selection(st);
		int32_t h = st->dragging ? -1 : region_snap_hit(st, st->cursor_x, st->cursor_y);
		if (h != st->snap_hover) st->snap_hover = h;
	}

	int hover = -1;
	if (toolbar_reachable(st)) {
		enum tb_action a = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
		if (a != TB_NONE) hover = (int)a;
	}
	region_set_hover(st, hover);

	refresh_cursor(st, p);
	region_render_request_redraw_all(st);
}

static bool toolbar_button_event(struct ro_state *st, struct wl_pointer *p,
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
			refresh_cursor(st, p);
		}
		return true;
	}

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		if (act == TB_UNDO) region_undo_disarm(st);
		region_render_request_redraw_all(st);
		return true;
	}

	st->tooltip_visible = false;
	if (act != TB_WIDTH_SLIDER) region_tooltip_arm(st);
	if (st->text_input_active) region_commit_text(st);
	if (act == TB_REGION) {
		mode_enter_region(st);
		refresh_cursor(st, p);
	} else if (act == TB_EDIT) {
		mode_enter_anno_edit(st);
		refresh_cursor(st, p);
	} else if (act >= TB_TOOL_PEN && act <= TB_TOOL_ERASER) {
		mode_select_tool(st, (enum tool_kind)(act - TB_TOOL_PEN));
		refresh_cursor(st, p);
	} else if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE) {
		st->current_color = TOOLBAR_COLORS[act - TB_COLOR_RED];
		st->edit_choices_dirty = true;
		st->eyedropper_mode = false;
		st->color_picker_open = false;
	} else if (act == TB_COLOR_CURRENT) {
		st->color_picker_open = !st->color_picker_open;
		st->eyedropper_mode = false;
		refresh_cursor(st, p);
	} else if (act == TB_WIDTH_SLIDER) {
		slider_set_width_from_cursor(st);
		st->slider_dragging = true;
		st->edit_choices_dirty = true;
		region_drag_start(st);
	} else if (act == TB_UNDO) {
		region_undo_pop(st);
		region_undo_arm(st);
	} else if (act == TB_SAVE) {
		if (st->has_selection) st->finished = true;
	} else if (act == TB_CANCEL) {
		st->cancelled = true;
		st->finished = true;
	}
	region_render_request_redraw_all(st);
	return true;
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup) return;

	if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (region_drag_active(st) || st->text_input_active) {
			region_drag_abort(st);
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		st->cancelled = true;
		st->finished = true;
		return;
	}
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->tb_dragging) {
		st->tb_dragging = false;
		refresh_cursor(st, p);
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
			refresh_cursor(st, p);
		} else if (st->handle_dragging != HANDLE_NONE) {
			st->handle_dragging = HANDLE_NONE;
			region_undo_commit(st);
			refresh_cursor(st, p);
		} else if (region_anno_dragging(st)) {
			bool was_move = st->anno_drag == ANNO_DRAG_MOVE;
			st->anno_drag = ANNO_DRAG_NONE;
			if (st->sel_anno >= 0) {
				if (was_move)
					region_undo_record_anno_move(st, (size_t)st->sel_anno,
												 st->anno_last_x - st->anno_press_x,
												 st->anno_last_y - st->anno_press_y);
				else
					region_undo_record_anno_geom(st, (size_t)st->sel_anno,
												 st->anno_geom_snap);
			}
			if (st->out_annos) st->out_annos->gen++;
			refresh_cursor(st, p);
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
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_grid) {
			st->color_input_active = false;
			uint32_t picked = 0;
			if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked)) {
				st->current_color = picked;
				st->edit_choices_dirty = true;
			}
			st->color_picker_dragging = true;
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_input) {
			st->color_input_active = true;
			snprintf(st->color_input_buf, sizeof st->color_input_buf,
					 "%06X", st->current_color & 0xFFFFFFu);
			st->color_input_len = 6;
			region_render_request_redraw_all(st);
			return;
		}
		if (st->color_input_active) {
			uint32_t parsed = 0;
			if (region_parse_hex_color(st->color_input_buf, &parsed)) {
				st->current_color = parsed;
				st->edit_choices_dirty = true;
			}
			st->color_input_active = false;
			st->color_input_len = 0;
		}
		if (st->eyedropper_mode) {
			/* fall through to eyedropper sample below; keep panel open */
		} else if (!region_toolbar_contains(st, st->cursor_x, st->cursor_y)) {
			st->color_picker_open = false;
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (toolbar_button_event(st, p, state)) return;

	if (region_editing(st) && st->eyedropper_mode &&
		state == WL_POINTER_BUTTON_STATE_PRESSED) {
		uint32_t picked = 0;
		if (eyedropper_sample(st, &picked)) {
			st->current_color = picked;
			st->edit_choices_dirty = true;
		}
		st->eyedropper_mode = false;
		refresh_cursor(st, p);
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
			if (st->has_selection) lock_or_finish(st);
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
			int c = anno_corner_at(st, st->cursor_x, st->cursor_y);
			if (a && c >= 0) {
				st->anno_geom_snap[0] = a->x0;
				st->anno_geom_snap[1] = a->y0;
				st->anno_geom_snap[2] = a->x1;
				st->anno_geom_snap[3] = a->y1;
				st->anno_drag = c;
				st->out_annos->gen++;
				region_drag_start(st);
			} else {
				st->sel_anno = anno_hit_index(st, st->cursor_x, st->cursor_y);
				if (st->sel_anno >= 0) {
					st->anno_drag = ANNO_DRAG_MOVE;
					st->anno_press_x = st->anno_last_x = st->cursor_x;
					st->anno_press_y = st->anno_last_y = st->cursor_y;
					st->out_annos->gen++;
					region_drag_start(st);
				}
			}
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->has_selection &&
			!region_inside_selection(st, st->cursor_x, st->cursor_y)) {
			if (region_editing(st)) return;
			st->region_locked = false;
			region_undo_begin(st);
			st->dragging = true;
			st->drag_x0 = st->cursor_x;
			st->drag_y0 = st->cursor_y;
			region_update_selection(st);
			refresh_cursor(st, p);
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
			refresh_cursor(st, p);
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
			refresh_cursor(st, p);
		} else if (st->handle_dragging != HANDLE_NONE) {
			st->handle_dragging = HANDLE_NONE;
			refresh_cursor(st, p);
		} else if (st->drawing) {
			region_commit_drawing(st);
		}
	}
	region_render_request_redraw_all(st);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
	if (!region_editing(st)) return;
	st->scroll_accum += wl_fixed_to_double(value);
	int32_t n = (int32_t)(st->scroll_accum / 10.0);
	if (n == 0) return;
	st->scroll_accum -= n * 10.0;
	if (st->current_tool == TOOL_TEXT || st->text_input_active) {
		int32_t f = st->current_font - n * 2;
		if (f < FONT_MIN) f = FONT_MIN;
		if (f > FONT_MAX) f = FONT_MAX;
		if (f == st->current_font) return;
		st->current_font = f;
		region_render_request_redraw_all(st);
		return;
	}
	int32_t w = st->current_width - n;
	if (w < WIDTH_MIN) w = WIDTH_MIN;
	if (w > WIDTH_MAX) w = WIDTH_MAX;
	if (w == st->current_width) return;
	st->current_width = w;
	st->edit_choices_dirty = true;
	region_render_request_redraw_all(st);
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

static void keyboard_keymap(void *data, struct wl_keyboard *kb,
							uint32_t format, int32_t fd, uint32_t size) {
	(void)kb;
	struct ro_state *st = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	void *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map_str == MAP_FAILED) return;

	if (st->xkb_state) xkb_state_unref(st->xkb_state);
	if (st->xkb_keymap) xkb_keymap_unref(st->xkb_keymap);

	size_t klen = size > 0 ? size - 1 : 0;
	st->xkb_keymap = xkb_keymap_new_from_buffer(
		st->xkb_ctx, map_str, klen, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_str, size);
	st->xkb_state = st->xkb_keymap ? xkb_state_new(st->xkb_keymap) : NULL;
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

static int32_t tool_for_letter(xkb_keysym_t sym) {
	static const char keys[] = "pmlroabte";
	if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) sym += XKB_KEY_a - XKB_KEY_A;
	if (sym < XKB_KEY_a || sym > XKB_KEY_z) return -1;
	const char *p = strchr(keys, (int)sym);
	return p ? (int32_t)(p - keys) : -1;
}

static uint32_t nudge_dir_for_sym(xkb_keysym_t sym) {
	switch (sym) {
	case XKB_KEY_Left:
	case XKB_KEY_KP_Left:
		return NUDGE_LEFT;
	case XKB_KEY_Right:
	case XKB_KEY_KP_Right:
		return NUDGE_RIGHT;
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		return NUDGE_UP;
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		return NUDGE_DOWN;
	default:
		return 0;
	}
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
		if (sym == XKB_KEY_u || sym == XKB_KEY_U ||
			sym == XKB_KEY_z || sym == XKB_KEY_Z)
			region_undo_disarm(st);
		uint32_t dir = nudge_dir_for_sym(sym);
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
						(c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
					st->color_input_buf[st->color_input_len] = '\0';
				}
			}
		}
		region_render_request_redraw_all(st);
		return;
	}

	if (sym == XKB_KEY_Escape) {
		if (region_drag_active(st)) {
			region_drag_abort(st);
			if (st->pointer) refresh_cursor(st, st->pointer);
			region_render_request_redraw_all(st);
			return;
		}
		st->cancelled = true;
		st->finished = true;
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (st->has_selection) {
			st->finished = true;
		} else if (!region_editing(st)) {
			st->cancelled = true;
			st->finished = true;
		}
		return;
	}
	if (st->ctrl_held && (sym == XKB_KEY_c || sym == XKB_KEY_C)) {
		if (st->has_selection) st->finished = true;
		return;
	}
	if (st->ctrl_held && (sym == XKB_KEY_a || sym == XKB_KEY_A)) {
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
		lock_or_finish(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (st->region_locked && st->has_selection && !region_drag_active(st)) {
		uint32_t dir = nudge_dir_for_sym(sym);
		if (dir) {
			region_nudge_press(st, dir);
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (!region_editing(st)) return;

	if (sym == XKB_KEY_u || sym == XKB_KEY_U ||
		(st->ctrl_held && (sym == XKB_KEY_z || sym == XKB_KEY_Z))) {
		if (region_drag_active(st)) return;
		region_undo_pop(st);
		region_undo_arm(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (region_drag_active(st)) return;

	if (!st->ctrl_held && (sym == XKB_KEY_s || sym == XKB_KEY_S)) {
		mode_enter_anno_edit(st);
		if (st->pointer) refresh_cursor(st, st->pointer);
		region_render_request_redraw_all(st);
		return;
	}

	int32_t pick = -1;
	if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9)
		pick = (int32_t)(sym - XKB_KEY_1);
	else if (sym >= XKB_KEY_KP_1 && sym <= XKB_KEY_KP_9)
		pick = (int32_t)(sym - XKB_KEY_KP_1);
	if (pick < 0 && !st->ctrl_held) pick = tool_for_letter(sym);
	if (pick >= 0 && pick < TOOL_COUNT) {
		mode_select_tool(st, (enum tool_kind)pick);
		if (st->pointer) refresh_cursor(st, st->pointer);
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
		if (st->pointer) refresh_cursor(st, st->pointer);
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
	wl_pointer_add_listener(st->pointer, &pointer_listener_g, st);
	wl_keyboard_add_listener(st->keyboard, &keyboard_listener_g, st);
}
