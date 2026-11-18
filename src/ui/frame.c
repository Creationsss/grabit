// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/window.h"

#include "cairo_util.h"
#include "ui/widgets.h"
#include "ui_colors.h"
#include "ui_theme.h"

#define UI_CLOSE_SIZE 26

static void close_rect(const struct ui_window *w, double *x, double *y) {
	*x = w->width - UI_BORDER - UI_CLOSE_SIZE;
	*y = (UI_TITLE_H - UI_CLOSE_SIZE) / 2.0;
}

enum ui_hit ui_frame_hit(const struct ui_window *w, double x, double y) {
	if (x < 0 || y < 0 || x >= w->width || y >= w->height) return UI_HIT_NONE;

	if (!w->maximized && !w->fullscreen) {
		bool n = y < UI_BORDER && !(w->tiled & UI_TILED_TOP);
		bool s = y >= w->height - UI_BORDER && !(w->tiled & UI_TILED_BOTTOM);
		bool e = x >= w->width - UI_BORDER && !(w->tiled & UI_TILED_RIGHT);
		bool wst = x < UI_BORDER && !(w->tiled & UI_TILED_LEFT);
		if (n && wst) return UI_HIT_NW;
		if (n && e) return UI_HIT_NE;
		if (s && wst) return UI_HIT_SW;
		if (s && e) return UI_HIT_SE;
		if (n) return UI_HIT_N;
		if (s) return UI_HIT_S;
		if (e) return UI_HIT_E;
		if (wst) return UI_HIT_W;
	}

	double cx, cy;
	close_rect(w, &cx, &cy);
	if (x >= cx && x < cx + UI_CLOSE_SIZE && y >= cy && y < cy + UI_CLOSE_SIZE)
		return UI_HIT_CLOSE;
	if (y < UI_TITLE_H) return UI_HIT_TITLE;
	return UI_HIT_CONTENT;
}

static void draw_close(struct ui_window *w, cairo_t *cr) {
	double x, y, s = UI_CLOSE_SIZE;
	close_rect(w, &x, &y);
	bool hot = w->hover == UI_HIT_CLOSE;
	if (hot) {
		cairo_set_source_rgba(cr, GUI_C_DESTRUCTIVE, w->close_armed ? 0.95 : 0.75);
		grabit_cairo_rect_r(cr, x, y, s, s, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
	}
	double pad = s / 3.2;
	cairo_set_source_rgba(cr, GUI_C_INK, hot ? 1.0 : 0.70);
	grabit_cairo_cross(cr, x + s / 2, y + s / 2, s / 2 - pad, 1.4);
}

void ui_frame_draw(struct ui_window *w, cairo_t *cr) {
	grabit_ui_window_panel(cr, w->width, w->height, 1.0, ui_window_panel_radius(w));

	cairo_set_source_rgba(cr, GUI_C_INK, 0.05);
	cairo_rectangle(cr, 1, 1, w->width - 2, UI_TITLE_H - 1);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, GUI_C_INK, 0.10);
	cairo_rectangle(cr, 0, UI_TITLE_H, w->width, 1);
	cairo_fill(cr);

	ui_draw_text_fit(cr, UI_BORDER + 6, (UI_TITLE_H - 16) / 2.0,
					 w->width - UI_BORDER * 2 - UI_CLOSE_SIZE - 12,
					 w->title ? w->title : "grabit", 13, CAIRO_FONT_WEIGHT_BOLD,
					 w->activated ? 0.92 : 0.55);

	draw_close(w, cr);
}
