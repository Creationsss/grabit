// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/controls_internal.h"

#include "cairo_util.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <math.h>
#include <stdio.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

void ctl_btn_rect(int btn, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
	int32_t x0 = CB_PAD + CB_DOT_W + 4 + CB_TIME_W + CB_SEC_GAP;
	*x = x0 + btn * (CB_BTN + CB_GAP);
	*y = (CB_H - CB_BTN) / 2;
	*w = CB_BTN;
	*h = CB_BTN;
}

#define PLAY_FILLET 0.2

static void glyph_play(cairo_t *cr, double cx, double cy, double s) {
	const double vx[3] = {cx - s * 0.7, cx - s * 0.7, cx + s};
	const double vy[3] = {cy - s, cy + s, cy};
	for (int i = 0; i < 3; i++) {
		double ax = vx[(i + 2) % 3], ay = vy[(i + 2) % 3];
		double bx = vx[i], by = vy[i];
		double cx2 = vx[(i + 1) % 3], cy2 = vy[(i + 1) % 3];
		double v1x = bx - ax, v1y = by - ay;
		double v2x = cx2 - bx, v2y = cy2 - by;
		double l1 = hypot(v1x, v1y), l2 = hypot(v2x, v2y);
		double r = fmin(s * PLAY_FILLET, fmin(l1, l2) * 0.5);
		if (i == 0)
			cairo_move_to(cr, bx - v1x / l1 * r, by - v1y / l1 * r);
		else
			cairo_line_to(cr, bx - v1x / l1 * r, by - v1y / l1 * r);
		cairo_curve_to(cr, bx, by, bx, by,
					   bx + v2x / l2 * r, by + v2y / l2 * r);
	}
	cairo_close_path(cr);
}

static void draw_bar(cairo_t *cr, const struct rec_controls *c) {
	grabit_ui_panel(cr, 0, 0, c->bw, c->bh, 1.0);

	double cy = CB_H / 2.0;
	double dot_cx = CB_PAD + CB_DOT_W / 2.0;
	if (c->paused) {
		cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, 1.0);
	} else {
		cairo_set_source_rgba(cr, 0.85, 0.1, 0.1, 1.0);
	}
	cairo_arc(cr, dot_cx, cy, CB_DOT_W / 2.0 - 2.0, 0, 2.0 * M_PI);
	cairo_fill(cr);

	char tbuf[32];
	snprintf(tbuf, sizeof tbuf, "%lld:%02lld",
			 (long long)(c->secs / 60), (long long)(c->secs % 60));
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, tbuf, &ext);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
	cairo_move_to(cr, CB_PAD + CB_DOT_W + 4, cy - ext.height / 2.0 - ext.y_bearing);
	cairo_show_text(cr, tbuf);

	for (int btn = 0; btn < CB_BTN_COUNT; btn++) {
		int32_t bx, by, bw, bh;
		ctl_btn_rect(btn, &bx, &by, &bw, &bh);
		bool enabled = (btn == CB_BTN_START)   ? c->paused
					   : (btn == CB_BTN_PAUSE) ? !c->paused
											   : true;
		double pad = 3.0;
		double aa = enabled ? 0.96 : 0.35;
		if (btn == CB_BTN_START) {
			cairo_set_source_rgba(cr, 0.20, 0.58, 0.32, aa);
		} else if (btn == CB_BTN_PAUSE) {
			cairo_set_source_rgba(cr, 0.18, 0.18, 0.18, enabled ? 0.94 : 0.35);
		} else if (btn == CB_BTN_ABORT) {
			cairo_set_source_rgba(cr, 0.72, 0.15, 0.15, aa);
		} else {
			cairo_set_source_rgba(cr, 0.62, 0.22, 0.22, aa);
		}
		grabit_cairo_rect_r(cr, bx + pad, by + pad, bw - pad * 2, bh - pad * 2,
							grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);

		double ia = enabled ? 0.92 : 0.45;
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, ia);
		double bcx = bx + bw / 2.0;
		double bcy = by + bh / 2.0;
		double s = bh * 0.6 * 0.36;
		if (btn == CB_BTN_START) {
			glyph_play(cr, bcx, bcy, s);
			cairo_fill(cr);
		} else if (btn == CB_BTN_PAUSE) {
			double gr = grabit_ui_radius(GUI_R_GLYPH);
			grabit_cairo_rect_r(cr, bcx - s, bcy - s, s * 0.72, s * 2.0, gr);
			grabit_cairo_rect_r(cr, bcx + s * 0.28, bcy - s, s * 0.72, s * 2.0, gr);
			cairo_fill(cr);
		} else if (btn == CB_BTN_ABORT) {
			double arm = s * 0.75;
			cairo_set_line_width(cr, 2.0);
			cairo_move_to(cr, bcx - arm, bcy - arm);
			cairo_line_to(cr, bcx + arm, bcy + arm);
			cairo_move_to(cr, bcx + arm, bcy - arm);
			cairo_line_to(cr, bcx - arm, bcy + arm);
			cairo_stroke(cr);
		} else {
			grabit_cairo_rect_r(cr, bcx - s * 0.9, bcy - s * 0.9, s * 1.8, s * 1.8,
								grabit_ui_radius(GUI_R_GLYPH));
			cairo_fill(cr);
		}
	}
}

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)cb;
	(void)time;
	struct ctl_output *o = data;
	grabit_wl_callback_drop(&o->frame_cb);
	if (o->dirty) ctl_output_redraw(o);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

static void output_request_redraw(struct ctl_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	ctl_output_redraw(o);
}

void ctl_output_redraw(struct ctl_output *o) {
	if (!o->configured) return;
	struct rec_controls *c = o->st;
	o->dirty = false;

	struct rect cur = {0, 0, 0, 0};
	struct rect b = ctl_bar_rect(c);
	int32_t ix, iy, iw, ih;
	if (grabit_output_rect_intersect(o->go, &b, &ix, &iy, &iw, &ih)) {
		cur = (struct rect){(ix - o->go->x) * o->scale,
							(iy - o->go->y) * o->scale,
							iw * o->scale, ih * o->scale};
	}
	if (cur.w == 0 && o->shown.w == 0 && o->mapped) return;

	struct grabit_shm_slot *slot = grabit_shm_pool_next(
		c->wls->shm, "grabit-rec-controls", &o->pool, o->pixel_w, o->pixel_h);
	if (!slot) {
		o->dirty = true;
		return;
	}
	struct rect *sshown = &o->slot_shown[slot - o->pool.slots];

	if (cur.w > 0 || sshown->w > 0) {
		cairo_surface_t *surf = grabit_cairo_image_argb(slot->buf.map, o->pixel_w,
														o->pixel_h, o->pixel_w * 4);
		if (!surf) return;
		cairo_t *cr = cairo_create(surf);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		if (sshown->w > 0) {
			cairo_rectangle(cr, sshown->x, sshown->y, sshown->w, sshown->h);
			cairo_fill(cr);
		}
		if (cur.w > 0) {
			cairo_rectangle(cr, cur.x, cur.y, cur.w, cur.h);
			cairo_fill(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
			cairo_scale(cr, o->scale, o->scale);
			cairo_translate(cr, c->bx - o->go->x, c->by - o->go->y);
			draw_bar(cr, c);
		}
		cairo_destroy(cr);
		cairo_surface_flush(surf);
		cairo_surface_destroy(surf);
	}

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);
	grabit_shm_slot_attach(o->surface, slot);
	if (o->shown.w > 0)
		wl_surface_damage_buffer(o->surface, o->shown.x, o->shown.y,
								 o->shown.w, o->shown.h);
	if (cur.w > 0)
		wl_surface_damage_buffer(o->surface, cur.x, cur.y, cur.w, cur.h);
	wl_surface_commit(o->surface);
	wl_display_flush(c->wls->display);
	*sshown = cur;
	o->shown = cur;
	o->mapped = true;
}

void ctl_redraw_all(struct rec_controls *c) {
	for (size_t i = 0; i < c->n; i++)
		output_request_redraw(&c->outs[i]);
}
