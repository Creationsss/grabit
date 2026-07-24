// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"
#include "region/wlr_state.h"

#include "wl/wl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cairo/cairo.h>

#define LINE_PICKER_W 168
#define LINE_ROW_H 30
#define LINE_PICKER_PAD 5
#define LINE_PICKER_GAP 8
#define LINE_DIVIDER_H 9
#define LINE_N_TOOLS 3

static const enum tool_kind LINE_TOOLS[LINE_N_TOOLS] = {
	TOOL_PEN,
	TOOL_MARKER,
	TOOL_LINE,
};

static const char *const LINE_TOOL_LABELS[LINE_N_TOOLS] = {
	"Pen",
	"Marker",
	"Line",
};

static int32_t line_picker_total_h(void) {
	return LINE_PICKER_PAD * 2 + LINE_N_TOOLS * LINE_ROW_H + LINE_DIVIDER_H +
		   STROKE_STYLE_COUNT * LINE_ROW_H;
}

void region_line_picker_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h) {
	int32_t ph = line_picker_total_h();
	if (!region_toolbar_popup_pos(st, TB_TOOL_LINES, LINE_PICKER_W, ph,
								  LINE_PICKER_GAP, out_x, out_y)) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_w = LINE_PICKER_W;
	*out_h = ph;
}

enum line_picker_kind region_line_picker_hit(const struct ro_state *st,
											 int32_t abs_x, int32_t abs_y, int *index) {
	int32_t px, py, pw, ph;
	region_line_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return LP_NONE;
	if (abs_x < px || abs_x >= px + pw) return LP_NONE;
	int32_t ly = abs_y - (py + LINE_PICKER_PAD);
	if (ly < 0) return LP_NONE;
	if (ly < LINE_N_TOOLS * LINE_ROW_H) {
		*index = ly / LINE_ROW_H;
		return LP_TOOL;
	}
	ly -= LINE_N_TOOLS * LINE_ROW_H + LINE_DIVIDER_H;
	if (ly >= 0 && ly < STROKE_STYLE_COUNT * LINE_ROW_H) {
		*index = ly / LINE_ROW_H;
		return LP_STYLE;
	}
	return LP_NONE;
}

static void line_picker_tool_icon(cairo_t *cr, enum tool_kind t,
								  double cx, double cy, double s) {
	switch (t) {
	case TOOL_MARKER:
		toolbar_icon_marker(cr, cx, cy, s);
		break;
	case TOOL_LINE:
		toolbar_icon_line(cr, cx, cy, s);
		break;
	default:
		toolbar_icon_pen(cr, cx, cy, s);
		break;
	}
}

static void line_picker_row(cairo_t *cr, double x0, double w, double ry,
							int32_t S, bool sel, const char *label) {
	double rh = LINE_ROW_H * S;
	if (sel) {
		cairo_rectangle(cr, x0 + 3.0 * S, ry + 1.0 * S,
						w - 6.0 * S, rh - 2.0 * S);
		cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, 0.22);
		cairo_fill(cr);
	}
	cairo_set_source_rgba(cr, sel ? 1.0 : 0.86, sel ? 1.0 : 0.86,
						  sel ? 1.0 : 0.86, 1.0);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, label, &ext);
	cairo_move_to(cr, x0 + LINE_ROW_H * S * 1.25,
				  ry + rh / 2.0 - ext.height / 2.0 - ext.y_bearing);
	cairo_show_text(cr, label);
}

void region_line_picker_render(cairo_t *cr, const struct ro_output *o) {
	const struct ro_state *st = o->st;
	if (!st->line_picker_open) return;

	int32_t px, py, pw, ph;
	region_line_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return;
	if (!grabit_output_overlaps(o->go, (struct rect){px, py, pw, ph})) return;

	int32_t S = o->scale;
	double x0 = (double)(px - o->go->x) * S;
	double y0 = (double)(py - o->go->y) * S;
	double w = (double)pw * S;
	double h = (double)ph * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, x0, y0, w, h);
	cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.94);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, x0 + 0.5 * S, y0 + 0.5 * S, w - (double)S, h - (double)S);
	cairo_stroke(cr);

	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.0 * S);

	double icon_s = LINE_ROW_H * S * 0.72;
	double top = y0 + LINE_PICKER_PAD * S;

	for (int i = 0; i < LINE_N_TOOLS; i++) {
		double ry = top + i * LINE_ROW_H * S;
		bool sel = st->current_tool == LINE_TOOLS[i] && st->region_locked &&
				   !st->anno_edit_mode;
		line_picker_row(cr, x0, w, ry, S, sel, LINE_TOOL_LABELS[i]);
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, 1.0);
		line_picker_tool_icon(cr, LINE_TOOLS[i], x0 + LINE_ROW_H * S * 0.72,
							  ry + LINE_ROW_H * S / 2.0, icon_s);
	}

	double div_y = top + LINE_N_TOOLS * LINE_ROW_H * S + LINE_DIVIDER_H * S / 2.0;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, (double)S);
	cairo_move_to(cr, x0 + 8.0 * S, div_y);
	cairo_line_to(cr, x0 + w - 8.0 * S, div_y);
	cairo_stroke(cr);

	double stop = top + LINE_N_TOOLS * LINE_ROW_H * S + LINE_DIVIDER_H * S;
	for (int i = 0; i < STROKE_STYLE_COUNT; i++) {
		double ry = stop + i * LINE_ROW_H * S;
		bool sel = (enum stroke_style)i == st->current_style;
		char label[16];
		snprintf(label, sizeof label, "%s", grabit_line_style_names[i]);
		if (label[0] >= 'a' && label[0] <= 'z') label[0] -= 'a' - 'A';
		line_picker_row(cr, x0, w, ry, S, sel, label);
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, 1.0);
		toolbar_icon_line_style(cr, x0 + LINE_ROW_H * S * 0.72,
								ry + LINE_ROW_H * S / 2.0, icon_s,
								(enum stroke_style)i);
	}

	cairo_restore(cr);
}
