// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_THEME_H
#define GRABIT_UI_THEME_H

#include <cairo/cairo.h>

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
void grabit_ui_panel(cairo_t *cr, double x, double y, double w, double h, double s);

#endif
