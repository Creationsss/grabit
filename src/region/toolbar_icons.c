// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "cairo_util.h"

#include <math.h>

#include <cairo/cairo.h>

#define ICON_HALF 0.36

void toolbar_icon_region(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double hw = s * 0.42;
	double hh = s * 0.34;
	double arm = s * 0.16;
	double corners[4][2] = {
		{cx - hw, cy - hh},
		{cx + hw, cy - hh},
		{cx + hw, cy + hh},
		{cx - hw, cy + hh},
	};
	for (int i = 0; i < 4; i++) {
		double sx = corners[i][0] > cx ? -1.0 : 1.0;
		double sy = corners[i][1] > cy ? -1.0 : 1.0;
		cairo_move_to(cr, corners[i][0] + sx * arm, corners[i][1]);
		cairo_line_to(cr, corners[i][0], corners[i][1]);
		cairo_line_to(cr, corners[i][0], corners[i][1] + sy * arm);
	}
	cairo_stroke(cr);
	double dot = s * 0.05;
	cairo_arc(cr, cx, cy, dot, 0, 2.0 * M_PI);
	cairo_fill(cr);
}

void toolbar_icon_select(cairo_t *cr, double cx, double cy, double s) {
	double k = s / 20.0;
	double x = cx - 4.5 * k;
	double y = cy - 7.5 * k;
	cairo_move_to(cr, x, y);
	cairo_line_to(cr, x, y + 13.0 * k);
	cairo_line_to(cr, x + 3.2 * k, y + 10.2 * k);
	cairo_line_to(cr, x + 5.4 * k, y + 15.0 * k);
	cairo_line_to(cr, x + 7.6 * k, y + 14.0 * k);
	cairo_line_to(cr, x + 5.4 * k, y + 9.4 * k);
	cairo_line_to(cr, x + 9.6 * k, y + 9.4 * k);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void toolbar_icon_pen(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	double half = s * ICON_HALF;
	cairo_move_to(cr, cx - half, cy + half * 0.3);
	cairo_curve_to(cr,
				   cx - half * 0.3, cy - half * 1.2,
				   cx + half * 0.3, cy + half * 1.2,
				   cx + half, cy - half * 0.3);
	cairo_stroke(cr);
}

static void icon_source(cairo_t *cr, double *r, double *g, double *b, double *a) {
	*r = *g = *b = 0.92;
	*a = 1.0;
	cairo_pattern_get_rgba(cairo_get_source(cr), r, g, b, a);
}

void toolbar_icon_marker(cairo_t *cr, double cx, double cy, double s) {
	double half = s * ICON_HALF;
	double r, g, b, a;
	icon_source(cr, &r, &g, &b, &a);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, r, g, b, a * 0.45);
	cairo_set_line_width(cr, s * 0.30);
	cairo_move_to(cr, cx - half, cy + half * 0.6);
	cairo_line_to(cr, cx + half, cy - half * 0.6);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_set_line_width(cr, 2.4 * (s / 24.0));
	cairo_move_to(cr, cx - half, cy + half * 0.6);
	cairo_line_to(cr, cx + half, cy - half * 0.6);
	cairo_stroke(cr);
}

void toolbar_icon_line(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double half = s * 0.38;
	cairo_move_to(cr, cx - half, cy + half);
	cairo_line_to(cr, cx + half, cy - half);
	cairo_stroke(cr);
}

void toolbar_icon_rect(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	double half = s * ICON_HALF;
	cairo_rectangle(cr, cx - half, cy - half, half * 2, half * 2);
	cairo_stroke(cr);
}

void toolbar_icon_ellipse(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, s * ICON_HALF, 0, 2.0 * M_PI);
	cairo_stroke(cr);
}

void toolbar_icon_arrow(cairo_t *cr, double cx, double cy, double s) {
	double half = s * ICON_HALF;
	grabit_cairo_arrow(cr, cx - half, cy + half, cx + half, cy - half,
					   2.4 * (s / 24.0), 0.0);
}

void toolbar_icon_arrow_pen(cairo_t *cr, double cx, double cy, double s) {
	double half = s * ICON_HALF;
	double w = 2.4 * (s / 24.0);
	double x1 = cx + half, y1 = cy - half;
	double c2x = cx + half * 0.15, c2y = cy + half * 0.55;
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, cx - half, cy + half);
	cairo_curve_to(cr, cx - half, cy - half * 0.45, c2x, c2y, x1, y1);
	cairo_stroke(cr);
	grabit_cairo_arrow_head(cr, c2x, c2y, x1, y1, w, 0.0);
}

void toolbar_icon_pixelate(cairo_t *cr, double cx, double cy, double s) {
	double half = s * ICON_HALF;
	int n = 4;
	double cell = (half * 2) / n;
	for (int row = 0; row < n; row++) {
		for (int col = 0; col < n; col++) {
			double v = ((row * 7 + col * 3) % 5) / 5.0;
			cairo_set_source_rgba(cr, 0.5 + v * 0.4, 0.5 + v * 0.4, 0.5 + v * 0.4, 1);
			cairo_rectangle(cr, cx - half + col * cell, cy - half + row * cell, cell, cell);
			cairo_fill(cr);
		}
	}
}

void toolbar_icon_spotlight(cairo_t *cr, double cx, double cy, double s) {
	double r, g, b, a;
	icon_source(cr, &r, &g, &b, &a);
	double hw = s * 0.42, hh = s * 0.34;
	double ix = s * 0.17, iy = s * 0.14;
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_rectangle(cr, cx - hw, cy - hh, hw * 2, hh * 2);
	cairo_rectangle(cr, cx - ix, cy - iy, ix * 2, iy * 2);
	cairo_set_source_rgba(cr, r, g, b, a * 0.35);
	cairo_fill(cr);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_set_line_width(cr, 2.2 * (s / 24.0));
	cairo_rectangle(cr, cx - ix, cy - iy, ix * 2, iy * 2);
	cairo_stroke(cr);
}

void toolbar_icon_blur(cairo_t *cr, double cx, double cy, double s) {
	double r = s * 0.42;
	cairo_pattern_t *p = cairo_pattern_create_radial(cx, cy, 0, cx, cy, r);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 0.85, 0.85, 0.85, 1.0);
	cairo_pattern_add_color_stop_rgba(p, 0.55, 0.6, 0.6, 0.6, 0.85);
	cairo_pattern_add_color_stop_rgba(p, 1.0, 0.5, 0.5, 0.5, 0.0);
	cairo_set_source(cr, p);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(p);
}

void toolbar_icon_text(cairo_t *cr, double cx, double cy, double s) {
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, s * 0.7);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, "Aa", &ext);
	cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing,
				  cy + ext.height / 2.0);
	cairo_show_text(cr, "Aa");
}

void toolbar_icon_counter(cairo_t *cr, double cx, double cy, double s) {
	double r = s * 0.42;
	cairo_set_line_width(cr, s * 0.12);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_stroke(cr);
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, s * 0.6);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, "1", &ext);
	cairo_move_to(cr, cx - ext.x_advance / 2.0,
				  cy - ext.height / 2.0 - ext.y_bearing);
	cairo_show_text(cr, "1");
}

void toolbar_icon_callout(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.2 * (s / 24.0);
	double hw = s * 0.38, hh = s * 0.25;
	double x0 = cx - hw, y0 = cy - hh - s * 0.10;
	double x1 = cx + hw, y1 = y0 + hh * 2;
	double r = s * 0.12;
	double tl = x0 + r + s * 0.04, tr = tl + s * 0.28;
	double tipx = tl + s * 0.07, tipy = y1 + s * 0.22;

	cairo_set_line_width(cr, w);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_new_path(cr);
	cairo_arc(cr, x1 - r, y0 + r, r, -M_PI / 2.0, 0.0);
	cairo_arc(cr, x1 - r, y1 - r, r, 0.0, M_PI / 2.0);
	cairo_line_to(cr, tr, y1);
	cairo_line_to(cr, tipx, tipy);
	cairo_line_to(cr, tl, y1);
	cairo_arc(cr, x0 + r, y1 - r, r, M_PI / 2.0, M_PI);
	cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3.0 * M_PI / 2.0);
	cairo_close_path(cr);
	cairo_stroke(cr);
}

void toolbar_icon_eraser(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.0 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_rotate(cr, -0.4);
	double hw = s * 0.42;
	double hh = s * 0.18;
	cairo_set_source_rgba(cr, 1.0, 0.65, 0.78, 1);
	cairo_rectangle(cr, -hw, -hh, hw * 2, hh * 2);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.75, 0.75, 0.75, 1);
	cairo_rectangle(cr, -hw, hh - hh * 0.4, hw * 2, hh * 0.4);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_rectangle(cr, -hw, -hh, hw * 2, hh * 2);
	cairo_stroke(cr);
	cairo_restore(cr);
}

void toolbar_icon_line_style(cairo_t *cr, double cx, double cy, double s,
							 enum stroke_style style) {
	double half = s * 0.40;
	double lw = 2.6 * (s / 24.0);
	double d[2];
	int nd = grabit_stroke_dash(style, lw, d);
	cairo_set_line_width(cr, lw);
	cairo_set_line_cap(cr, nd > 0 ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	cairo_set_dash(cr, d, nd, 0.0);
	cairo_move_to(cr, cx - half, cy);
	cairo_line_to(cr, cx + half, cy);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0.0);
}

void toolbar_icon_rrect(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	double half = s * ICON_HALF;
	grabit_cairo_rounded_rect(cr, cx - half, cy - half, half * 2, half * 2,
							  half * 0.55);
	cairo_stroke(cr);
}

void toolbar_icon_for_tool(cairo_t *cr, enum tool_kind t,
						   double cx, double cy, double s) {
	switch (t) {
	case TOOL_PEN:
		toolbar_icon_pen(cr, cx, cy, s);
		break;
	case TOOL_MARKER:
		toolbar_icon_marker(cr, cx, cy, s);
		break;
	case TOOL_LINE:
		toolbar_icon_line(cr, cx, cy, s);
		break;
	case TOOL_RECT:
		toolbar_icon_rect(cr, cx, cy, s);
		break;
	case TOOL_RRECT:
		toolbar_icon_rrect(cr, cx, cy, s);
		break;
	case TOOL_ELLIPSE:
		toolbar_icon_ellipse(cr, cx, cy, s);
		break;
	case TOOL_ARROW:
		toolbar_icon_arrow(cr, cx, cy, s);
		break;
	case TOOL_ARROW_PEN:
		toolbar_icon_arrow_pen(cr, cx, cy, s);
		break;
	case TOOL_BLUR:
		toolbar_icon_blur(cr, cx, cy, s);
		break;
	case TOOL_PIXELATE:
		toolbar_icon_pixelate(cr, cx, cy, s);
		break;
	case TOOL_SPOTLIGHT:
		toolbar_icon_spotlight(cr, cx, cy, s);
		break;
	case TOOL_TEXT:
		toolbar_icon_text(cr, cx, cy, s);
		break;
	case TOOL_COUNTER:
		toolbar_icon_counter(cr, cx, cy, s);
		break;
	case TOOL_CALLOUT:
		toolbar_icon_callout(cr, cx, cy, s);
		break;
	default:
		toolbar_icon_eraser(cr, cx, cy, s);
		break;
	}
}

void toolbar_icon_undo(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.8 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	double r = s * 0.30;
	double yc = cy + s * 0.05;
	cairo_arc_negative(cr, cx, yc, r, 0.0, -M_PI);
	cairo_stroke(cr);
	double end_x = cx - r;
	double end_y = yc;
	double head_len = s * 0.24;
	double wing_w = s * 0.16;
	cairo_move_to(cr, end_x, end_y + head_len);
	cairo_line_to(cr, end_x - wing_w, end_y);
	cairo_line_to(cr, end_x + wing_w, end_y);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void toolbar_icon_redo(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.8 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	double r = s * 0.30;
	double yc = cy + s * 0.05;
	cairo_arc(cr, cx, yc, r, -M_PI, 0.0);
	cairo_stroke(cr);
	double end_x = cx + r;
	double end_y = yc;
	double head_len = s * 0.24;
	double wing_w = s * 0.16;
	cairo_move_to(cr, end_x, end_y + head_len);
	cairo_line_to(cr, end_x - wing_w, end_y);
	cairo_line_to(cr, end_x + wing_w, end_y);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void toolbar_icon_save(cairo_t *cr, double cx, double cy, double s) {
	double w = 3.2 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, cx - s * 0.36, cy);
	cairo_line_to(cr, cx - s * 0.08, cy + s * 0.28);
	cairo_line_to(cr, cx + s * 0.40, cy - s * 0.30);
	cairo_stroke(cr);
}

void toolbar_icon_cancel(cairo_t *cr, double cx, double cy, double s) {
	double w = 3.0 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double half = s * 0.34;
	cairo_move_to(cr, cx - half, cy - half);
	cairo_line_to(cr, cx + half, cy + half);
	cairo_move_to(cr, cx + half, cy - half);
	cairo_line_to(cr, cx - half, cy + half);
	cairo_stroke(cr);
}

void toolbar_icon_color_picker(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.0 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

	double r = s * 0.40;
	double inner = r * 0.45;

	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_stroke(cr);

	cairo_move_to(cr, cx, cy - r);
	cairo_line_to(cr, cx, cy - inner);
	cairo_move_to(cr, cx, cy + inner);
	cairo_line_to(cr, cx, cy + r);
	cairo_move_to(cr, cx - r, cy);
	cairo_line_to(cr, cx - inner, cy);
	cairo_move_to(cr, cx + inner, cy);
	cairo_line_to(cr, cx + r, cy);
	cairo_stroke(cr);

	cairo_arc(cr, cx, cy, w * 1.4, 0, 2.0 * M_PI);
	cairo_fill(cr);
}

void toolbar_color_swatch(cairo_t *cr, double cx, double cy, double s,
						  uint32_t color, bool active) {
	double r = s * 0.42;
	grabit_cairo_set_source_argb(cr, color, 1);
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, active ? 1.0 : 0.0, active ? 1.0 : 0.0, active ? 1.0 : 0.0, active ? 1.0 : 0.45);
	cairo_set_line_width(cr, active ? 2.2 * (s / 24.0) : 1.2 * (s / 24.0));
	cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
	cairo_stroke(cr);
}

void toolbar_color_current(cairo_t *cr, double cx, double cy, double s,
						   uint32_t color, bool active) {
	double half = s * 0.46;
	double radius = s * 0.10;
	double x0 = cx - half;
	double y0 = cy - half;
	double w = half * 2;
	double h = half * 2;

	grabit_cairo_rounded_rect(cr, x0, y0, w, h, radius);

	grabit_cairo_set_source_argb(cr, color, 1);
	cairo_fill_preserve(cr);

	if (active) {
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
		cairo_set_line_width(cr, 2.4 * (s / 24.0));
	} else {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
		cairo_set_line_width(cr, 1.8 * (s / 24.0));
	}
	cairo_stroke(cr);
}
