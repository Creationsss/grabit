// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_SCROLL_H
#define GRABIT_UI_SCROLL_H

#include <stdbool.h>
#include <stddef.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

struct ui_window;

#define UI_BAR_W 5
#define UI_BAR_GRAB 14

struct ui_scroll {
	double offset;
	double content_h;
	double view_h;
	bool hover;
	bool drag;
	double grab_dy;
};

void ui_scroll_set(struct ui_scroll *s, double content_h, double view_h);
double ui_scroll_max(const struct ui_scroll *s);
bool ui_scroll_thumb(const struct ui_scroll *s, double *y, double *h);

void ui_scroll_to(struct ui_window *w, struct ui_scroll *s, double want);
void ui_scroll_wheel(struct ui_window *w, struct ui_scroll *s, double units,
					 double step);
void ui_scroll_reveal(struct ui_window *w, struct ui_scroll *s, double top,
					  double height);

bool ui_scroll_press(struct ui_window *w, struct ui_scroll *s, double width,
					 double x, double y);
bool ui_scroll_motion(struct ui_window *w, struct ui_scroll *s, double width,
					  double x, double y);
void ui_scroll_release(struct ui_window *w, struct ui_scroll *s);

void ui_scroll_draw(cairo_t *cr, const struct ui_scroll *s, double width);

bool ui_list_key(xkb_keysym_t sym, size_t count, size_t page, size_t *selected);

#endif
