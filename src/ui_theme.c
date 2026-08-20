// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui_theme.h"

#include "cairo_util.h"
#include "config/config.h"
#include "wm/wm.h"

#include <string.h>

static const double RADII[GUI_R_COUNT][2] = {
	[GUI_R_PANEL] = {0.0, 1.0},
	[GUI_R_BTN] = {0.0, 0.625},
	[GUI_R_TIP] = {0.0, 0.75},
	[GUI_R_TOOLTIP] = {4.0, 0.75},
	[GUI_R_GLYPH] = {0.0, 0.1875},
};

#define GUI_RADIUS_AUTO (-1)

static int g_radius;

void grabit_ui_theme_init(struct config *cfg) {
	const char *v = config_get(cfg, "gui.radius");
	if (v && strcmp(v, "auto") == 0)
		g_radius = GUI_RADIUS_AUTO;
	else
		g_radius = config_get_int_clamp(cfg, "gui.radius", 0, 0, GUI_RADIUS_MAX);
}

static int radius_base(void) {
	if (g_radius == GUI_RADIUS_AUTO) {
		int r = grabit_wm_window_radius(NULL);
		g_radius = r > GUI_RADIUS_MAX ? GUI_RADIUS_MAX : (r < 0 ? 0 : r);
	}
	return g_radius;
}

double grabit_ui_radius(enum gui_radius token) {
	int base = radius_base();
	return base > 0 ? RADII[token][1] * base : RADII[token][0];
}

void grabit_ui_card_bg(cairo_t *cr) {
	cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.95);
}

void grabit_ui_panel(cairo_t *cr, double x, double y, double w, double h, double s) {
	double r = grabit_ui_radius(GUI_R_PANEL) * s;
	cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.94);
	grabit_cairo_rect_r(cr, x, y, w, h, r);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, s);
	grabit_cairo_rect_r_inset(cr, x, y, w, h, r, s);
	cairo_stroke(cr);
}
