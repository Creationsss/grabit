// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keycapture_internal.h"

#include "cairo_util.h"
#include "ui/widgets.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <stdio.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

void gkc_draw(cairo_t *cr, struct kc_state *s) {
	grabit_ui_window_panel(cr, s->w, s->h, 1.0, grabit_ui_radius(GUI_R_PANEL));
	double tw = s->w - KC_PAD * 2;

	char asking[160];
	snprintf(asking, sizeof asking, "Press the keys for %s",
			 s->action_key ? s->action_key : "this binding");
	ui_draw_text_fit(cr, KC_PAD, KC_PAD, tw, asking, 15, CAIRO_FONT_WEIGHT_BOLD, 0.95);

	char caps[512];
	region_keybind_list_join(&s->caps, caps, sizeof caps);
	if (caps[0])
		ui_draw_text_fit(cr, KC_PAD, KC_KEYS_Y, tw, caps, 16, CAIRO_FONT_WEIGHT_NORMAL, 0.95);
	else
		ui_draw_text_fit(cr, KC_PAD, KC_KEYS_Y, tw, "waiting for keys", 14,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.45);

	ui_draw_text_fit(cr, KC_PAD, KC_PAD + KC_FOOT_Y, tw,
					 "Enter saves, Esc cancels, Backspace removes the last; click here to add "
					 "a mouse button",
					 KC_FOOT_SIZE, CAIRO_FONT_WEIGHT_NORMAL, 0.55);
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
