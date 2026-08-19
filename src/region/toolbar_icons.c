// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "cairo_util.h"

#include <math.h>

#include <cairo/cairo.h>

void toolbar_icon_region(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, rounded ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	cairo_set_line_join(cr, rounded ? CAIRO_LINE_JOIN_ROUND : CAIRO_LINE_JOIN_MITER);
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
	double half = s * 0.36;
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
	double half = s * 0.36;
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
	double half = s * 0.36;
	cairo_rectangle(cr, cx - half, cy - half, half * 2, half * 2);
	cairo_stroke(cr);
}

void toolbar_icon_ellipse(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_new_sub_path(cr);
	cairo_arc(cr, cx, cy, s * 0.36, 0, 2.0 * M_PI);
	cairo_stroke(cr);
}

void toolbar_icon_arrow(cairo_t *cr, double cx, double cy, double s) {
	double half = s * 0.40;
	double x0 = cx - half, y0 = cy + half;
	double x1 = cx + half, y1 = cy - half;
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	double ux = dx / len, uy = dy / len;
	double px = -uy, py = ux;
	double body = s * 0.075;
	double head_w = s * 0.18;
	double head_len = s * 0.30;
	double bx = x1 - ux * head_len;
	double by = y1 - uy * head_len;
	cairo_move_to(cr, x0 + px * body, y0 + py * body);
	cairo_line_to(cr, bx + px * body, by + py * body);
	cairo_line_to(cr, bx + px * head_w, by + py * head_w);
	cairo_line_to(cr, x1, y1);
	cairo_line_to(cr, bx - px * head_w, by - py * head_w);
	cairo_line_to(cr, bx - px * body, by - py * body);
	cairo_line_to(cr, x0 - px * body, y0 - py * body);
	cairo_close_path(cr);
	cairo_fill(cr);
}

void toolbar_icon_pixelate(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double half = s * 0.36;
	int n = 4;
	double cell = (half * 2) / n;
	double rad = rounded ? (cell * 0.22) : 0.0;
	for (int row = 0; row < n; row++) {
		for (int col = 0; col < n; col++) {
			double v = ((row * 7 + col * 3) % 5) / 5.0;
			cairo_set_source_rgba(cr, 0.5 + v * 0.4, 0.5 + v * 0.4, 0.5 + v * 0.4, 1);
			grabit_cairo_rounded_rect(cr, cx - half + col * cell, cy - half + row * cell, cell, cell, rad);
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
	double hw = s * 0.40, hh = s * 0.28;
	double bx = cx - hw, by = cy - hh - s * 0.08;
	cairo_set_line_width(cr, w);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	grabit_cairo_rounded_rect(cr, bx, by, hw * 2, hh * 2, s * 0.12);
	cairo_stroke(cr);
	double edge = by + hh * 2 - w * 0.5;
	cairo_move_to(cr, cx - hw * 0.52, edge);
	cairo_line_to(cr, cx - hw * 0.62, edge + s * 0.26);
	cairo_line_to(cr, cx - hw * 0.10, edge);
	cairo_close_path(cr);
	cairo_fill(cr);
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
							 enum stroke_style style, bool rounded) {
	double half = s * 0.40;
	double lw = 2.6 * (s / 24.0);
	double d[2];
	int nd = grabit_stroke_dash(style, lw, d);
	cairo_set_line_width(cr, lw);
	cairo_set_line_cap(cr, (rounded || nd > 0) ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	cairo_set_dash(cr, d, nd, 0.0);
	cairo_move_to(cr, cx - half, cy);
	cairo_line_to(cr, cx + half, cy);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0.0);
}

void toolbar_icon_rrect(cairo_t *cr, double cx, double cy, double s) {
	double w = 2.4 * (s / 24.0);
	cairo_set_line_width(cr, w);
	double half = s * 0.36;
	grabit_cairo_rounded_rect(cr, cx - half, cy - half, half * 2, half * 2,
							  half * 0.55);
	cairo_stroke(cr);
}

void toolbar_icon_rarrow(cairo_t *cr, double cx, double cy, double s) {
	double half = s * 0.32;
	double x0 = cx - half, y0 = cy + half;
	double x1 = cx + half, y1 = cy - half;
	double dx = x1 - x0, dy = y1 - y0;
	double len = sqrt(dx * dx + dy * dy);
	double ux = dx / len, uy = dy / len;
	double px = -uy, py = ux;
	double width = s * 0.09;
	double head_len = s * 0.22;

	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, width);

	// Shaft
	cairo_move_to(cr, x0, y0);
	cairo_line_to(cr, x1, y1);

	// Symmetrical swept chevron wings (45 degrees)
	cairo_move_to(cr, x1 - (ux - px) * head_len, y1 - (uy - py) * head_len);
	cairo_line_to(cr, x1, y1);
	cairo_line_to(cr, x1 - (ux + px) * head_len, y1 - (uy + py) * head_len);

	cairo_stroke(cr);
	cairo_restore(cr);
}

void toolbar_icon_rtext(cairo_t *cr, double cx, double cy, double s) {
	cairo_save(cr);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, s * 0.10);

	// Capital 'A' with rounded legs and crossbar
	double ax_apex = cx - s * 0.18;
	double ay_top = cy - s * 0.25;
	double ay_bot = cy + s * 0.25;
	cairo_move_to(cr, cx - s * 0.36, ay_bot);
	cairo_line_to(cr, ax_apex, ay_top);
	cairo_line_to(cr, cx + s * 0.00, ay_bot);
	cairo_move_to(cr, cx - s * 0.28, cy + s * 0.05);
	cairo_line_to(cr, cx - s * 0.08, cy + s * 0.05);

	// Lowercase 'a' with round loop and stem
	double acx = cx + s * 0.21, acy = cy + s * 0.08;
	double ar = s * 0.16;
	cairo_new_sub_path(cr);
	cairo_arc(cr, acx, acy, ar, 0, 2.0 * M_PI);
	cairo_move_to(cr, acx + ar, cy - s * 0.08);
	cairo_line_to(cr, acx + ar, ay_bot);

	cairo_stroke(cr);
	cairo_restore(cr);
}

void toolbar_icon_for_tool(cairo_t *cr, enum tool_kind t,
						   double cx, double cy, double s, bool rounded) {
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
	case TOOL_RARROW:
		toolbar_icon_rarrow(cr, cx, cy, s);
		break;
	case TOOL_BLUR:
		toolbar_icon_blur(cr, cx, cy, s);
		break;
	case TOOL_PIXELATE:
		toolbar_icon_pixelate(cr, cx, cy, s, rounded);
		break;
	case TOOL_SPOTLIGHT:
		toolbar_icon_spotlight(cr, cx, cy, s);
		break;
	case TOOL_TEXT:
		toolbar_icon_text(cr, cx, cy, s);
		break;
	case TOOL_RTEXT:
		toolbar_icon_rtext(cr, cx, cy, s);
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

void toolbar_icon_undo(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double w = 2.8 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, rounded ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	double r = s * 0.30;
	double yc = cy + s * 0.05;
	cairo_arc_negative(cr, cx, yc, r, 0.0, -M_PI);
	cairo_stroke(cr);
	double end_x = cx - r;
	double end_y = yc;
	double head_len = s * 0.24;
	double wing_w = s * 0.16;
	if (rounded) {
		cairo_save(cr);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_set_line_width(cr, s * 0.06);
		cairo_move_to(cr, end_x, end_y + head_len);
		cairo_line_to(cr, end_x - wing_w, end_y);
		cairo_line_to(cr, end_x + wing_w, end_y);
		cairo_close_path(cr);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);
		cairo_restore(cr);
	} else {
		cairo_move_to(cr, end_x, end_y + head_len);
		cairo_line_to(cr, end_x - wing_w, end_y);
		cairo_line_to(cr, end_x + wing_w, end_y);
		cairo_close_path(cr);
		cairo_fill(cr);
	}
}

void toolbar_icon_redo(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double w = 2.8 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, rounded ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	double r = s * 0.30;
	double yc = cy + s * 0.05;
	cairo_arc(cr, cx, yc, r, -M_PI, 0.0);
	cairo_stroke(cr);
	double end_x = cx + r;
	double end_y = yc;
	double head_len = s * 0.24;
	double wing_w = s * 0.16;
	if (rounded) {
		cairo_save(cr);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_set_line_width(cr, s * 0.06);
		cairo_move_to(cr, end_x, end_y + head_len);
		cairo_line_to(cr, end_x - wing_w, end_y);
		cairo_line_to(cr, end_x + wing_w, end_y);
		cairo_close_path(cr);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);
		cairo_restore(cr);
	} else {
		cairo_move_to(cr, end_x, end_y + head_len);
		cairo_line_to(cr, end_x - wing_w, end_y);
		cairo_line_to(cr, end_x + wing_w, end_y);
		cairo_close_path(cr);
		cairo_fill(cr);
	}
}

void toolbar_icon_save(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double w = 3.2 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, rounded ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
	cairo_set_line_join(cr, rounded ? CAIRO_LINE_JOIN_ROUND : CAIRO_LINE_JOIN_MITER);
	cairo_move_to(cr, cx - s * 0.36, cy);
	cairo_line_to(cr, cx - s * 0.08, cy + s * 0.28);
	cairo_line_to(cr, cx + s * 0.40, cy - s * 0.30);
	cairo_stroke(cr);
}

void toolbar_icon_cancel(cairo_t *cr, double cx, double cy, double s, bool rounded) {
	double w = 3.0 * (s / 24.0);
	cairo_set_line_width(cr, w);
	cairo_set_line_cap(cr, rounded ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
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
						   uint32_t color, bool active, bool rounded) {
	double half = s * 0.46;
	double radius = rounded ? (s * 0.18) : 0.0;
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
