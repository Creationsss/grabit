// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_input_state.h"

#include "region/annotate.h"
#include "region/wlr_state.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define UNDO_HOLD_DELAY_MS 600
#define UNDO_HOLD_REPEAT_MS 80
#define TOOLTIP_DELAY_MS 1000
#define PEN_POINTS_MAX (1u << 18)
#define NUDGE_DELAY_MS 300
#define NUDGE_REPEAT_MS 30
#define NUDGE_STEP_MAX 10
#define NUDGE_ACCEL_TICKS 4

#include "region/input_state_internal.h"

void region_tooltip_arm(struct ro_state *st) {
	if (st->tooltip_timer_fd < 0) return;
	struct itimerspec it = {
		.it_value = {.tv_sec = TOOLTIP_DELAY_MS / 1000,
					 .tv_nsec = (TOOLTIP_DELAY_MS % 1000) * 1000000L},
	};
	timerfd_settime(st->tooltip_timer_fd, 0, &it, NULL);
}

void region_tooltip_disarm(struct ro_state *st) {
	if (st->tooltip_timer_fd < 0) return;
	struct itimerspec it = {0};
	timerfd_settime(st->tooltip_timer_fd, 0, &it, NULL);
}

void region_drag_start(struct ro_state *st) {
	st->hovered_button = -1;
	st->tooltip_visible = false;
	region_tooltip_disarm(st);
}

bool region_drag_active(const struct ro_state *st) {
	return st->drawing || st->slider_dragging || st->moving_region ||
		   st->tb_dragging || st->handle_dragging != HANDLE_NONE ||
		   region_anno_dragging(st) ||
		   st->eyedropper_mode || st->color_picker_open ||
		   st->color_picker_dragging || st->color_input_active;
}

void region_drag_abort(struct ro_state *st) {
	if (st->drawing) {
		free(st->pen_points);
		st->pen_points = NULL;
		st->pen_n = st->pen_cap = 0;
		st->drawing = false;
	}
	st->slider_dragging = false;
	st->moving_region = false;
	st->tb_dragging = false;
	st->handle_dragging = HANDLE_NONE;
	if (region_anno_dragging(st) && region_anno_selected(st)) {
		if (st->anno_drag == ANNO_DRAG_MOVE) {
			gist_undo_apply(st, &(struct undo_item){
									.kind = UNDO_ANNO_MOVE,
									.u.move = {.idx = (size_t)st->sel_anno,
											   .dx = st->anno_last_x - st->anno_press_x,
											   .dy = st->anno_last_y - st->anno_press_y}});
		} else {
			struct undo_item it = {.kind = UNDO_ANNO_GEOM,
								   .u.geom = {.idx = (size_t)st->sel_anno}};
			memcpy(it.u.geom.g, st->anno_geom_snap, sizeof it.u.geom.g);
			gist_undo_apply(st, &it);
		}
	}
	st->anno_drag = ANNO_DRAG_NONE;
	st->eyedropper_mode = false;
	st->color_picker_open = false;
	st->color_picker_dragging = false;
	st->color_input_active = false;
	st->color_input_len = 0;
	if (st->text_input_active) {
		st->text_input_active = false;
		st->text_len = 0;
	}
	region_undo_commit(st);
	region_undo_disarm(st);
}

bool region_set_hover(struct ro_state *st, int btn) {
	if (btn == st->hovered_button) return false;
	st->hovered_button = btn;
	bool was_visible = st->tooltip_visible;
	st->tooltip_visible = false;
	if (btn >= 0)
		region_tooltip_arm(st);
	else
		region_tooltip_disarm(st);
	return was_visible;
}

int region_snap_hit(const struct ro_state *st, int32_t x, int32_t y) {
	if (st->snap_hover >= 0 && (size_t)st->snap_hover < st->n_snap_windows &&
		rect_contains(st->snap_windows[st->snap_hover], x, y))
		return st->snap_hover;
	for (size_t i = st->n_snap_windows; i > 0; i--) {
		if (rect_contains(st->snap_windows[i - 1], x, y)) return (int)(i - 1);
	}
	return -1;
}

void region_update_selection(struct ro_state *st) {
	if (!st->dragging) {
		st->has_selection = false;
		return;
	}
	int32_t x0 = st->drag_x0, y0 = st->drag_y0;
	int32_t x1 = st->cursor_x, y1 = st->cursor_y;
	int32_t l = x0 < x1 ? x0 : x1;
	int32_t t = y0 < y1 ? y0 : y1;
	int32_t r = x0 > x1 ? x0 : x1;
	int32_t b = y0 > y1 ? y0 : y1;
	st->sel_x = l;
	st->sel_y = t;
	st->sel_w = r - l;
	st->sel_h = b - t;
	st->has_selection = (st->sel_w > 0 && st->sel_h > 0);
}

bool region_inside_selection(const struct ro_state *st, int32_t x, int32_t y) {
	return rect_contains((struct rect){st->sel_x, st->sel_y, st->sel_w, st->sel_h},
						 x, y);
}

void region_pen_append(struct ro_state *st, int32_t x, int32_t y) {
	if (st->pen_n >= PEN_POINTS_MAX) return;
	if (st->pen_n == st->pen_cap) {
		size_t cap = st->pen_cap ? st->pen_cap * 2 : 256;
		if (cap > PEN_POINTS_MAX) cap = PEN_POINTS_MAX;
		int32_t *p = realloc(st->pen_points, cap * 2 * sizeof(int32_t));
		if (!p) return;
		st->pen_points = p;
		st->pen_cap = cap;
	}
	st->pen_points[st->pen_n * 2] = x;
	st->pen_points[st->pen_n * 2 + 1] = y;
	st->pen_n++;
}

void region_apply_shape_snap(int tool, bool shift, int32_t x0, int32_t y0,
							 int32_t *x1, int32_t *y1) {
	if (!shift) return;
	if (tool_is_rect_region(tool)) {
		int32_t dx = *x1 - x0, dy = *y1 - y0;
		int32_t adx = dx < 0 ? -dx : dx;
		int32_t ady = dy < 0 ? -dy : dy;
		int32_t side = adx > ady ? adx : ady;
		*x1 = x0 + (dx < 0 ? -side : side);
		*y1 = y0 + (dy < 0 ? -side : side);
	} else if (tool == TOOL_ARROW || tool == TOOL_LINE) {
		double angle = atan2((double)(*y1 - y0), (double)(*x1 - x0));
		double snap = round(angle / (M_PI / 4.0)) * (M_PI / 4.0);
		double dx = *x1 - x0, dy = *y1 - y0;
		double len = sqrt(dx * dx + dy * dy);
		*x1 = x0 + (int32_t)(len * cos(snap));
		*y1 = y0 + (int32_t)(len * sin(snap));
	}
}

static void region_annotation_commit(struct ro_state *st, struct annotation *a) {
	if (annotation_list_push(st->out_annos, a) != 0)
		annotation_free(a);
	else
		gist_undo_record_anno(st);
}

void region_commit_drawing(struct ro_state *st) {
	struct annotation a = {0};
	a.tool = st->current_tool;
	a.color = st->current_color;
	a.width = st->current_width;
	a.font_size = ANNO_DEFAULT_FONT;

	if (tool_uses_points(st->current_tool)) {
		if (st->pen_n == 0) {
			st->drawing = false;
			return;
		}
		a.points = st->pen_points;
		a.n_points = st->pen_n;
		st->pen_points = NULL;
		st->pen_n = st->pen_cap = 0;
	} else {
		int32_t x1 = st->cursor_x;
		int32_t y1 = st->cursor_y;
		region_apply_shape_snap(st->current_tool, st->shift_held,
								st->draw_x0, st->draw_y0, &x1, &y1);
		a.x0 = st->draw_x0;
		a.y0 = st->draw_y0;
		a.x1 = x1;
		a.y1 = y1;
	}

	region_annotation_commit(st, &a);
	st->drawing = false;
}

void region_commit_text(struct ro_state *st) {
	if (!st->text_input_active || st->text_len == 0) {
		st->text_input_active = false;
		st->text_len = 0;
		return;
	}
	struct annotation a = {0};
	a.tool = TOOL_TEXT;
	a.color = st->current_color;
	a.font_size = st->current_font;
	a.x0 = st->text_x;
	a.y0 = st->text_y;
	st->text_buf[st->text_len] = '\0';
	a.text = strdup(st->text_buf);
	if (!a.text)
		annotation_free(&a);
	else
		region_annotation_commit(st, &a);
	st->text_input_active = false;
	st->text_len = 0;
}

void region_place_counter(struct ro_state *st) {
	if (!st->out_annos) return;
	int n = 0;
	for (size_t i = 0; i < st->out_annos->n; i++)
		if (st->out_annos->items[i].tool == TOOL_COUNTER) n++;
	char buf[16];
	snprintf(buf, sizeof buf, "%d", n + 1);
	struct annotation a = {0};
	a.tool = TOOL_COUNTER;
	a.color = st->current_color;
	a.font_size = st->current_font;
	a.x0 = st->cursor_x;
	a.y0 = st->cursor_y;
	a.text = strdup(buf);
	if (!a.text)
		annotation_free(&a);
	else
		region_annotation_commit(st, &a);
}
