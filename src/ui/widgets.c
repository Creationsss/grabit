// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/widgets.h"

#include "cairo_util.h"
#include "ui_colors.h"
#include "ui_theme.h"
#include "util/util.h"

#include <math.h>
#include <string.h>

void ui_font_options(cairo_t *cr) {
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
	cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
	cairo_set_font_options(cr, fo);
	cairo_font_options_destroy(fo);
}

cairo_t *ui_measure_cr(void) {
	static cairo_t *cr;
	if (cr && cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		cairo_destroy(cr);
		cr = NULL;
	}
	if (!cr) {
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cr = cairo_create(s);
		cairo_surface_destroy(s);
		ui_font_options(cr);
	}
	return cr;
}

double ui_snap_y(cairo_t *cr, double y) {
	double x = 0;
	cairo_user_to_device(cr, &x, &y);
	y = round(y);
	cairo_device_to_user(cr, &x, &y);
	return y;
}

void ui_set_font(cairo_t *cr, double size, cairo_font_weight_t weight) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, weight);
	cairo_set_font_size(cr, size);
}

void ui_field_bg(cairo_t *cr, double x, double y, double w, double h, bool focused) {
	double r = grabit_ui_radius(GUI_R_BTN);
	cairo_set_source_rgba(cr, GUI_C_SURFACE, 1.0);
	grabit_cairo_rect_r(cr, x, y, w, h, r);
	cairo_fill(cr);
	if (focused)
		grabit_ui_accent(cr, 0.85);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, 0.10);
	cairo_set_line_width(cr, 1.0);
	grabit_cairo_rect_r_inset(cr, x, y, w, h, r, 1.0);
	cairo_stroke(cr);
}

void ui_button_draw(cairo_t *cr, double x, double y, double w, double h,
					const char *label, bool hover, bool pressed) {
	static const double RAISED[] = {GUI_C_RAISED};
	ui_button_draw_color(cr, x, y, w, h, label, RAISED, hover, pressed);
}

void ui_button_draw_color(cairo_t *cr, double x, double y, double w, double h,
						  const char *label, const double rgb[3], bool hover, bool pressed) {
	double r = grabit_ui_radius(GUI_R_BTN);
	grabit_ui_button_source(cr, rgb, 0.94, hover, pressed);
	grabit_cairo_rect_r(cr, x, y, w, h, r);
	cairo_fill(cr);
	if (!label) return;
	double tw = ui_text_width(cr, label, 13, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	cairo_set_source_rgba(cr, GUI_C_TEXT, 1.0);
	cairo_move_to(cr, x + (w - tw) / 2.0, ui_snap_y(cr, y + (h - (fe.ascent + fe.descent)) / 2.0 + fe.ascent));
	cairo_show_text(cr, label);
}

void ui_toggle_draw(cairo_t *cr, double x, double y, bool on, bool hover, bool focused) {
	double h = UI_TOGGLE_H, w = UI_TOGGLE_W;
	double r = h / 2.0;
	if (on)
		grabit_ui_accent(cr, hover ? 1.0 : 0.88);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, hover ? 0.22 : 0.15);
	grabit_cairo_rect_r(cr, x, y, w, h, r);
	cairo_fill(cr);
	if (focused) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.85);
		cairo_set_line_width(cr, 1.5);
		grabit_cairo_rect_r(cr, x - 2.5, y - 2.5, w + 5, h + 5, r + 2.5);
		cairo_stroke(cr);
	}
	double knob = h - 6;
	double kx = on ? x + w - 3 - knob : x + 3;
	cairo_set_source_rgba(cr, GUI_C_INK, on ? 1.0 : 0.80);
	cairo_arc(cr, kx + knob / 2.0, y + h / 2.0, knob / 2.0, 0, 2 * M_PI);
	cairo_fill(cr);
}

void ui_chevron_draw(cairo_t *cr, double cx, double cy, double size, bool up) {
	double d = up ? -1 : 1;
	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, cx - size / 2.0, cy - d * size / 4.0);
	cairo_line_to(cr, cx, cy + d * size / 4.0);
	cairo_line_to(cr, cx + size / 2.0, cy - d * size / 4.0);
	cairo_stroke(cr);
}

void ui_record_icon_draw(cairo_t *cr, double cx, double cy, double size, double alpha) {
	double w = round(size * 0.9), h = round(size * 0.65);
	double x = round(cx - w / 2.0), y = round(cy - h / 2.0);
	cairo_set_source_rgba(cr, GUI_C_INK, alpha);
	cairo_set_line_width(cr, 1.2);
	grabit_cairo_rect_r(cr, x + 0.6, y + 0.6, w - 1.2, h - 1.2, 2.0);
	cairo_stroke(cr);
	double kx = x + round((w - 11) / 2.0);
	for (int row = 0; row < 2; row++)
		for (int c = 0; c < 4; c++)
			cairo_rectangle(cr, kx + c * 3, y + 3 + row * 3, 2, 2);
	cairo_rectangle(cr, kx + 2, y + 9, 7, 1.5);
	cairo_fill(cr);
}

void ui_reset_icon_draw(cairo_t *cr, double cx, double cy, double size, double alpha) {
	double r = size * 0.27, s = -M_PI / 2 - 0.5, e = s - 2 * M_PI + 1.2;
	cairo_set_source_rgba(cr, GUI_C_INK, alpha);
	cairo_set_line_width(cr, 1.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_new_sub_path(cr);
	cairo_arc_negative(cr, cx, cy, r, s, e);
	cairo_stroke(cr);
	double px = cx + r * cos(e), py = cy + r * sin(e);
	double tx = sin(e), ty = -cos(e), nx = cos(e), ny = sin(e), h = size * 0.2;
	cairo_move_to(cr, px + tx * h, py + ty * h);
	cairo_line_to(cr, px + nx * h * 0.8, py + ny * h * 0.8);
	cairo_line_to(cr, px - nx * h * 0.8, py - ny * h * 0.8);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void ui_warning_icon_draw(cairo_t *cr, double cx, double cy, double size) {
	double h = size * 0.5;
	cairo_set_source_rgba(cr, GUI_C_ERROR, 0.95);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, 1.5);
	cairo_move_to(cr, cx, cy - h);
	cairo_line_to(cr, cx + h * 1.1, cy + h * 0.8);
	cairo_line_to(cr, cx - h * 1.1, cy + h * 0.8);
	cairo_close_path(cr);
	cairo_fill_preserve(cr);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, GUI_C_PANEL, 1);
	cairo_set_line_width(cr, 1.4);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, cx, cy - h * 0.35);
	cairo_line_to(cr, cx, cy + h * 0.2);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy + h * 0.52, 0.9, 0, 2 * M_PI);
	cairo_fill(cr);
}

void ui_eye_icon_draw(cairo_t *cr, double cx, double cy, double size, bool open,
					  double alpha) {
	cairo_set_source_rgba(cr, GUI_C_INK, alpha);
	cairo_set_line_width(cr, 1.4);
	double w = size * 0.45, h = size * 0.26;
	cairo_new_path(cr);
	cairo_move_to(cr, cx - w, cy);
	cairo_curve_to(cr, cx - w * 0.5, cy - h * 1.6, cx + w * 0.5, cy - h * 1.6, cx + w, cy);
	cairo_curve_to(cr, cx + w * 0.5, cy + h * 1.6, cx - w * 0.5, cy + h * 1.6, cx - w, cy);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy, size * 0.11, 0, 2 * M_PI);
	cairo_fill(cr);
	if (open) return;
	cairo_move_to(cr, cx - w * 0.8, cy + h * 1.4);
	cairo_line_to(cr, cx + w * 0.8, cy - h * 1.4);
	cairo_stroke(cr);
}

double ui_text_width(cairo_t *cr, const char *text, double size,
					 cairo_font_weight_t weight) {
	ui_set_font(cr, size, weight);
	char buf[1024];
	size_t n = strlen(text), ok = grabit_utf8_valid_prefix(text, n);
	if (ok != n) {
		ok = grabit_utf8_valid_prefix(text, ok < sizeof buf ? ok : sizeof buf - 1);
		memcpy(buf, text, ok);
		buf[ok] = '\0';
		text = buf;
	}
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);
	return ext.x_advance;
}

void ui_draw_text_fit(cairo_t *cr, double x, double y, double max_w, const char *text,
					  double size, cairo_font_weight_t weight, double alpha) {
	ui_draw_text_fit_rgba(cr, x, y, max_w, text, size, weight, 1, 1, 1, alpha);
}

#define FIT_SLACK 0.01

double ui_text_ellipsize(cairo_t *cr, char *buf, double size, cairo_font_weight_t weight,
						 double max_w) {
	size_t n = strlen(buf);
	double w = ui_text_width(cr, buf, size, weight);
	while (w > max_w + FIT_SLACK && n > 0) {
		n = grabit_utf8_prev(buf, n);
		memcpy(buf + n, "\xe2\x80\xa6", 4);
		w = ui_text_width(cr, buf, size, weight);
	}
	return w;
}

void ui_draw_text_fit_rgba(cairo_t *cr, double x, double y, double max_w,
						   const char *text, double size, cairo_font_weight_t weight,
						   double r, double g, double b, double a) {
	char buf[512];
	size_t n = strlen(text);
	n = grabit_utf8_valid_prefix(text, n < sizeof buf - 4 ? n : sizeof buf - 4);
	memcpy(buf, text, n);
	buf[n] = '\0';
	if (ui_text_ellipsize(cr, buf, size, weight, max_w) > max_w + FIT_SLACK) return;
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x, ui_snap_y(cr, y + fe.ascent));
	cairo_show_text(cr, buf);
}
