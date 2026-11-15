// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_THEME_H
#define GRABIT_UI_THEME_H

#include <stdbool.h>

#include <cairo/cairo.h>

#include "ui_colors.h"

struct config;

#define GUI_RADIUS_MAX 100

enum gui_radius {
	GUI_R_PANEL,
	GUI_R_BTN,
	GUI_R_TIP,
	GUI_R_TOOLTIP,
	GUI_R_GLYPH,
	GUI_R_COUNT,
};

void grabit_ui_theme_init(struct config *cfg);
double grabit_ui_radius(enum gui_radius token);
void grabit_ui_card_bg(cairo_t *cr);
void grabit_ui_accent(cairo_t *cr, double alpha);
void grabit_ui_panel(cairo_t *cr, double x, double y, double w, double h, double s);
void grabit_ui_hint_pill(cairo_t *cr, double s, const char *text, double cx,
						 double baseline_y, double max_w);
void grabit_ui_window_panel(cairo_t *cr, double w, double h, double s, double radius);
void grabit_ui_button_source(cairo_t *cr, const double rgb[3], double alpha, bool hover,
							 bool pressed);
void grabit_ui_tooltip(cairo_t *cr, double s, const char *text, double cx, double top,
					   double bottom, double view_w);

#endif
