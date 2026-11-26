// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_WIDGETS_H
#define GRABIT_UI_WIDGETS_H

#include <stdbool.h>

#include <cairo/cairo.h>

#define UI_TOGGLE_W 40
#define UI_TOGGLE_H 22

void ui_font_options(cairo_t *cr);
cairo_t *ui_measure_cr(void);
double ui_snap_y(cairo_t *cr, double y);
void ui_set_font(cairo_t *cr, double size, cairo_font_weight_t weight);
void ui_field_bg(cairo_t *cr, double x, double y, double w, double h, bool focused);
void ui_button_draw(cairo_t *cr, double x, double y, double w, double h,
					const char *label, bool hover, bool pressed);
void ui_button_draw_color(cairo_t *cr, double x, double y, double w, double h,
						  const char *label, const double rgb[3], bool hover, bool pressed);
void ui_toggle_draw(cairo_t *cr, double x, double y, bool on, bool hover, bool focused);
void ui_chevron_draw(cairo_t *cr, double cx, double cy, double size, bool up);
void ui_record_icon_draw(cairo_t *cr, double cx, double cy, double size, double alpha);
void ui_reset_icon_draw(cairo_t *cr, double cx, double cy, double size, double alpha);
void ui_warning_icon_draw(cairo_t *cr, double cx, double cy, double size);
void ui_eye_icon_draw(cairo_t *cr, double cx, double cy, double size, bool open,
					  double alpha);
double ui_text_width(cairo_t *cr, const char *text, double size,
					 cairo_font_weight_t weight);
void ui_draw_text_fit(cairo_t *cr, double x, double y, double max_w, const char *text,
					  double size, cairo_font_weight_t weight, double alpha);
double ui_text_ellipsize(cairo_t *cr, char *buf, double size, cairo_font_weight_t weight,
						 double max_w);
void ui_draw_text_fit_rgba(cairo_t *cr, double x, double y, double max_w,
						   const char *text, double size, cairo_font_weight_t weight,
						   double r, double g, double b, double a);

#endif
