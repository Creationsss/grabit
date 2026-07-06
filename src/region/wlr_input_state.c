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

void region_handle_points(const struct ro_state *st, int32_t hx[8], int32_t hy[8]) {
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t mx = (l + r) / 2, my = (t + b) / 2;
	hx[HANDLE_NW] = l;
	hy[HANDLE_NW] = t;
	hx[HANDLE_N] = mx;
	hy[HANDLE_N] = t;
	hx[HANDLE_NE] = r;
	hy[HANDLE_NE] = t;
	hx[HANDLE_E] = r;
	hy[HANDLE_E] = my;
	hx[HANDLE_SE] = r;
	hy[HANDLE_SE] = b;
	hx[HANDLE_S] = mx;
	hy[HANDLE_S] = b;
	hx[HANDLE_SW] = l;
	hy[HANDLE_SW] = b;
	hx[HANDLE_W] = l;
	hy[HANDLE_W] = my;
}

int region_handle_at(const struct ro_state *st, int32_t x, int32_t y) {
	if (!st->region_locked) return HANDLE_NONE;
	int32_t hx[8], hy[8];
	region_handle_points(st, hx, hy);
	for (int i = 0; i < 8; i++) {
		int32_t dx = x - hx[i], dy = y - hy[i];
		if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS) return i;
	}
	return HANDLE_NONE;
}

static int flip_handle_x(int h) {
	switch (h) {
	case HANDLE_NW:
		return HANDLE_NE;
	case HANDLE_NE:
		return HANDLE_NW;
	case HANDLE_E:
		return HANDLE_W;
	case HANDLE_SE:
		return HANDLE_SW;
	case HANDLE_SW:
		return HANDLE_SE;
	case HANDLE_W:
		return HANDLE_E;
	default:
		return h;
	}
}

static int flip_handle_y(int h) {
	switch (h) {
	case HANDLE_NW:
		return HANDLE_SW;
	case HANDLE_N:
		return HANDLE_S;
	case HANDLE_NE:
		return HANDLE_SE;
	case HANDLE_SE:
		return HANDLE_NE;
	case HANDLE_S:
		return HANDLE_N;
	case HANDLE_SW:
		return HANDLE_NW;
	default:
		return h;
	}
}

void region_clamp_move(struct ro_state *st) {
	if (st->bounds.w <= 0 || st->bounds.h <= 0) return;
	if (st->sel_x < st->bounds.x) st->sel_x = st->bounds.x;
	if (st->sel_y < st->bounds.y) st->sel_y = st->bounds.y;
	if (st->sel_x + st->sel_w > st->bounds.x + st->bounds.w)
		st->sel_x = st->bounds.x + st->bounds.w - st->sel_w;
	if (st->sel_y + st->sel_h > st->bounds.y + st->bounds.h)
		st->sel_y = st->bounds.y + st->bounds.h - st->sel_h;
}

void region_apply_handle_drag(struct ro_state *st) {
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t cx = st->cursor_x, cy = st->cursor_y;
	if (st->bounds.w > 0 && st->bounds.h > 0) {
		if (cx < st->bounds.x) cx = st->bounds.x;
		if (cy < st->bounds.y) cy = st->bounds.y;
		if (cx > st->bounds.x + st->bounds.w) cx = st->bounds.x + st->bounds.w;
		if (cy > st->bounds.y + st->bounds.h) cy = st->bounds.y + st->bounds.h;
	}
	switch (st->handle_dragging) {
	case HANDLE_NW:
		l = cx;
		t = cy;
		break;
	case HANDLE_N:
		t = cy;
		break;
	case HANDLE_NE:
		r = cx;
		t = cy;
		break;
	case HANDLE_E:
		r = cx;
		break;
	case HANDLE_SE:
		r = cx;
		b = cy;
		break;
	case HANDLE_S:
		b = cy;
		break;
	case HANDLE_SW:
		l = cx;
		b = cy;
		break;
	case HANDLE_W:
		l = cx;
		break;
	default:
		return;
	}
	if (l > r) {
		int32_t tmp = l;
		l = r;
		r = tmp;
		st->handle_dragging = flip_handle_x(st->handle_dragging);
	}
	if (t > b) {
		int32_t tmp = t;
		t = b;
		b = tmp;
		st->handle_dragging = flip_handle_y(st->handle_dragging);
	}
	st->sel_x = l;
	st->sel_y = t;
	st->sel_w = r - l;
	st->sel_h = b - t;
}

void region_undo_arm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = true;
	struct itimerspec it = {
		.it_value = {.tv_sec = UNDO_HOLD_DELAY_MS / 1000,
					 .tv_nsec = (UNDO_HOLD_DELAY_MS % 1000) * 1000000L},
		.it_interval = {.tv_sec = UNDO_HOLD_REPEAT_MS / 1000,
						.tv_nsec = (UNDO_HOLD_REPEAT_MS % 1000) * 1000000L},
	};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

void region_undo_disarm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = false;
	struct itimerspec it = {0};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

static void nudge_apply(struct ro_state *st, int32_t dx, int32_t dy) {
	if (st->shift_held) {
		st->sel_w += dx;
		st->sel_h += dy;
		if (st->sel_w < 1) st->sel_w = 1;
		if (st->sel_h < 1) st->sel_h = 1;
		if (st->bounds.w > 0 && st->bounds.h > 0) {
			if (st->sel_x + st->sel_w > st->bounds.x + st->bounds.w)
				st->sel_w = st->bounds.x + st->bounds.w - st->sel_x;
			if (st->sel_y + st->sel_h > st->bounds.y + st->bounds.h)
				st->sel_h = st->bounds.y + st->bounds.h - st->sel_y;
		}
	} else {
		st->sel_x += dx;
		st->sel_y += dy;
		region_clamp_move(st);
	}
}

static int32_t nudge_dx(uint32_t held) {
	return ((held & NUDGE_RIGHT) ? 1 : 0) - ((held & NUDGE_LEFT) ? 1 : 0);
}

static int32_t nudge_dy(uint32_t held) {
	return ((held & NUDGE_DOWN) ? 1 : 0) - ((held & NUDGE_UP) ? 1 : 0);
}

void region_nudge_press(struct ro_state *st, uint32_t dir) {
	if (st->nudge_held & dir) return;
	nudge_apply(st, nudge_dx(dir), nudge_dy(dir));
	if (st->nudge_timer_fd < 0) return;
	if (st->nudge_held == 0) {
		st->nudge_ticks = 0;
		struct itimerspec it = {
			.it_value = {.tv_nsec = NUDGE_DELAY_MS * 1000000L},
			.it_interval = {.tv_nsec = NUDGE_REPEAT_MS * 1000000L},
		};
		timerfd_settime(st->nudge_timer_fd, 0, &it, NULL);
	}
	st->nudge_held |= dir;
}

void region_nudge_release(struct ro_state *st, uint32_t dir) {
	st->nudge_held &= ~dir;
	if (st->nudge_held == 0) region_nudge_disarm(st);
}

void region_nudge_disarm(struct ro_state *st) {
	if (st->nudge_timer_fd < 0) return;
	st->nudge_held = 0;
	st->nudge_ticks = 0;
	struct itimerspec it = {0};
	timerfd_settime(st->nudge_timer_fd, 0, &it, NULL);
}

void region_nudge_tick(struct ro_state *st) {
	uint64_t expirations = 0;
	ssize_t r = read(st->nudge_timer_fd, &expirations, sizeof expirations);
	(void)r;
	if (!st->region_locked || st->nudge_held == 0 || st->text_input_active ||
		region_drag_active(st)) {
		region_nudge_disarm(st);
		return;
	}
	st->nudge_ticks++;
	int32_t step = st->nudge_ticks / NUDGE_ACCEL_TICKS + 1;
	if (step > NUDGE_STEP_MAX) step = NUDGE_STEP_MAX;
	int32_t dx = nudge_dx(st->nudge_held);
	int32_t dy = nudge_dy(st->nudge_held);
	if (dx == 0 && dy == 0) return;
	nudge_apply(st, dx * step, dy * step);
	region_render_request_redraw_all(st);
}

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
		   st->handle_dragging != HANDLE_NONE || st->eyedropper_mode ||
		   st->color_picker_open || st->color_picker_dragging ||
		   st->color_input_active;
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
	st->handle_dragging = HANDLE_NONE;
	st->eyedropper_mode = false;
	st->color_picker_open = false;
	st->color_picker_dragging = false;
	st->color_input_active = false;
	st->color_input_len = 0;
	if (st->text_input_active) {
		st->text_input_active = false;
		st->text_len = 0;
	}
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
	if (tool == TOOL_RECT || tool == TOOL_ELLIPSE || tool == TOOL_BLUR) {
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

	if (annotation_list_push(st->out_annos, &a) != 0) annotation_free(&a);
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
	if (!a.text || annotation_list_push(st->out_annos, &a) != 0) {
		annotation_free(&a);
	}
	st->text_input_active = false;
	st->text_len = 0;
}
