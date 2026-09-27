// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keycapture_internal.h"

#include "cairo_util.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <stdio.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#define KC_DIM_A 0.45
#define KC_LINE 38.0

void gkc_draw(cairo_t *cr, struct kc_state *s) {
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, KC_DIM_A);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	char asking[160];
	snprintf(asking, sizeof asking, "press the keys for %s",
			 s->action_key ? s->action_key : "this binding");
	char caps[512];
	gkc_join(s, caps, sizeof caps);

	double cx = s->w / 2.0;
	double mid = s->h / 2.0;
	grabit_ui_hint_pill(cr, 1.0, asking, cx, caps[0] ? mid - KC_LINE : mid, s->w);
	if (caps[0]) grabit_ui_hint_pill(cr, 1.0, caps, cx, mid, s->w);
	grabit_ui_hint_pill(cr, 1.0,
						"enter saves, esc cancels, backspace removes the last", cx,
						s->h - 24.0, s->w);
}

void gkc_render(struct kc_state *s) {
	if (!s->buf.map || s->w <= 0 || s->h <= 0) return;

	int32_t pw = s->w * s->scale;
	int32_t ph = s->h * s->scale;
	cairo_surface_t *dst = grabit_cairo_image_argb(s->buf.map, pw, ph, pw * 4);
	if (!dst) return;
	cairo_t *cr = cairo_create(dst);
	cairo_scale(cr, s->scale, s->scale);
	gkc_draw(cr, s);
	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	wl_surface_set_buffer_scale(s->surface, s->scale);
	wl_surface_attach(s->surface, s->buf.buffer, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, pw, ph);
	wl_surface_commit(s->surface);
	wl_display_flush(s->wls->display);
}
