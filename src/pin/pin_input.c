// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "log.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <errno.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

void pin_input_apply_region(struct pin_output *o) {
	struct pin_state *st = o->st;
	if (!st->wls->compositor || !o->surface) return;
	struct wl_region *reg = wl_compositor_create_region(st->wls->compositor);
	if (!reg) return;
	struct rect want = {0, 0, 0, 0};
	if (st->input_grabbed || st->clickable) {
		struct rect pr = pin_rect(st);
		int32_t ix, iy, iw, ih;
		if (grabit_output_rect_intersect(o->go, &pr, &ix, &iy, &iw, &ih))
			want = (struct rect){ix - o->go->x, iy - o->go->y, iw, ih};
	}
	if (want.x == o->region.x && want.y == o->region.y &&
		want.w == o->region.w && want.h == o->region.h) {
		wl_region_destroy(reg);
		return;
	}
	if (want.w > 0)
		grabit_wl_region_add_rounded(reg, want.x, want.y, want.w, want.h,
									 st->transient ? (int32_t)grabit_ui_radius(GUI_R_PANEL) : 0);
	wl_surface_set_input_region(o->surface, reg);
	wl_region_destroy(reg);
	o->region = want;
}

static void pin_move_to(struct pin_state *st, int32_t x, int32_t y) {
	struct rect r = rect_clamp_into((struct rect){x, y, st->width, st->height},
									st->bounds);
	x = r.x;
	y = r.y;
	if (x == st->px && y == st->py) return;
	st->px = x;
	st->py = y;
	pin_render_redraw_all(st);
}

static struct pin_output *output_for_surface(struct pin_state *st,
											 struct wl_surface *surface) {
	for (size_t i = 0; i < st->n; i++) {
		if (st->outs[i]->surface == surface) return st->outs[i];
	}
	return NULL;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	struct pin_state *st = data;
	struct pin_output *o = output_for_surface(st, surface);
	if (!o) return;
	st->ptr_on = o;
	st->cx = o->go->x + wl_fixed_to_int(sx);
	st->cy = o->go->y + wl_fixed_to_int(sy);
	st->last_pointer_serial = serial;
	if (st->hover_caption && !st->hover_active) {
		st->hover_active = true;
		pin_render_redraw_all(st);
	}
	if (st->transient && st->dismiss_timer_fd >= 0 && st->dismiss_secs > 0) {
		struct itimerspec it = {.it_value = {.tv_sec = st->dismiss_secs}};
		timerfd_settime(st->dismiss_timer_fd, 0, &it, NULL);
	}
	pin_cursor_refresh(st);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	struct pin_state *st = data;
	if (st->ptr_on && st->ptr_on->surface != surface) return;
	st->ptr_on = NULL;
	if (st->hover_caption && st->hover_active) {
		st->hover_active = false;
		pin_render_redraw_all(st);
	}
	st->cursor_kind = PIN_CUR_NONE;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct pin_state *st = data;
	if (!st->ptr_on) return;
	st->cx = st->ptr_on->go->x + wl_fixed_to_int(sx);
	st->cy = st->ptr_on->go->y + wl_fixed_to_int(sy);
	if (st->dragging)
		pin_move_to(st, st->cx - st->grab_dx, st->cy - st->grab_dy);
	pin_cursor_update(st);
}

static void drag_end(struct pin_state *st) {
	st->dragging = false;
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)time;
	struct pin_state *st = data;
	st->last_pointer_serial = serial;
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
		drag_end(st);
		pin_cursor_update(st);
		return;
	}

	if (!st->ptr_on || !rect_contains(pin_rect(st), st->cx, st->cy)) return;

	if (st->clickable) {
		if (st->click_open && st->click_open[0]) {
			pid_t cpid = fork();
			if (cpid < 0) {
				log_warn("pin: fork for xdg-open failed (%s)", strerror(errno));
			} else if (cpid == 0) {
				setsid();
				execlp("xdg-open", "xdg-open", st->click_open, (char *)NULL);
				_exit(127);
			}
		}
		st->finished = true;
		return;
	}
	if (!st->input_grabbed) return;
	if (pin_in_close_button(st)) {
		st->finished = true;
		return;
	}

	st->dragging = true;
	st->grab_dx = st->cx - st->px;
	st->grab_dy = st->cy - st->py;
	pin_cursor_update(st);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}
static void pointer_frame(void *data, struct wl_pointer *p) {
	(void)data;
	(void)p;
}
static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
	(void)data;
	(void)p;
	(void)source;
}
static void pointer_axis_stop(void *data, struct wl_pointer *p,
							  uint32_t time, uint32_t axis) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *p,
								  uint32_t axis, int32_t discrete) {
	(void)data;
	(void)p;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

void pin_input_attach(struct pin_state *st) {
	if (!st->wls->seat || !(st->wls->seat_caps & WL_SEAT_CAPABILITY_POINTER)) {
		log_warn("pin: no pointer on seat; click-to-close disabled (dismiss with "
				 "`grabit --close-all`)");
		return;
	}
	st->pointer = wl_seat_get_pointer(st->wls->seat);
	if (!st->pointer) return;
	wl_pointer_add_listener(st->pointer, &pointer_listener_g, st);
}
