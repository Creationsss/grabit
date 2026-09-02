// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cairo/cairo.h>

#define TP_W 168
#define TP_ROW_H 30
#define TP_PAD 5
#define TP_GAP 8
#define TP_DIVIDER_H 9

static const enum tool_kind LINES_TOOLS[] = {TOOL_PEN, TOOL_MARKER, TOOL_LINE};
static const enum tool_kind SHAPES_TOOLS[] = {TOOL_RECT, TOOL_RRECT, TOOL_ELLIPSE};
static const enum tool_kind ARROW_TOOLS[] = {TOOL_ARROW, TOOL_ARROW_PEN};
static const enum tool_kind REDACT_TOOLS[] = {TOOL_BLUR, TOOL_PIXELATE,
											  TOOL_SPOTLIGHT};

static const char *const LINES_LABELS[] = {"Pen", "Marker", "Line"};
static const char *const SHAPES_LABELS[] = {"Rectangle", "Rounded", "Ellipse"};
static const char *const ARROW_LABELS[] = {"Straight", "Freehand"};
static const char *const REDACT_LABELS[] = {"Blur", "Pixelate", "Spotlight"};

static const struct tool_group GROUPS[] = {
	{TB_TOOL_LINES, LINES_TOOLS, 3, true, LINES_LABELS,
	 "Line tools  (p cycles)"},
	{TB_TOOL_SHAPES, SHAPES_TOOLS, 3, true, SHAPES_LABELS,
	 "Shapes  (r cycles)"},
	{TB_TOOL_ARROW, ARROW_TOOLS, 2, false, ARROW_LABELS,
	 "Arrows  (a cycles)"},
	{TB_TOOL_REDACT, REDACT_TOOLS, 3, false, REDACT_LABELS,
	 "Redact & focus  (b cycles)"},
};

_Static_assert(sizeof GROUPS / sizeof GROUPS[0] == TB_TOOL_GROUP_COUNT,
			   "GROUPS and TB_TOOL_GROUP_COUNT out of sync");

const struct tool_group *toolbar_tool_group(enum tb_action btn) {
	for (int i = 0; i < TB_TOOL_GROUP_COUNT; i++)
		if (GROUPS[i].btn == btn) return &GROUPS[i];
	return NULL;
}

const struct tool_group *toolbar_group_of_tool(enum tool_kind t) {
	for (int i = 0; i < TB_TOOL_GROUP_COUNT; i++)
		for (int j = 0; j < GROUPS[i].n; j++)
			if (GROUPS[i].tools[j] == t) return &GROUPS[i];
	return NULL;
}

int toolbar_group_index(const struct tool_group *g) {
	return (int)(g - GROUPS);
}

enum tool_kind toolbar_group_default(int idx) {
	if (idx < 0 || idx >= TB_TOOL_GROUP_COUNT) return TOOL_PEN;
	return GROUPS[idx].tools[0];
}

int32_t toolbar_standalone_tool(enum tb_action btn) {
	switch (btn) {
	case TB_TOOL_TEXT:
		return TOOL_TEXT;
	case TB_TOOL_COUNTER:
		return TOOL_COUNTER;
	case TB_TOOL_CALLOUT:
		return TOOL_CALLOUT;
	case TB_TOOL_ERASER:
		return TOOL_ERASER;
	default:
		return -1;
	}
}

static int32_t picker_total_h(const struct tool_group *g) {
	int rows = g->n + (g->has_style ? STROKE_STYLE_COUNT : 0);
	int divider = g->has_style ? TP_DIVIDER_H : 0;
	return TP_PAD * 2 + rows * TP_ROW_H + divider;
}

void region_tool_picker_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h) {
	const struct tool_group *g = toolbar_tool_group(st->picker_group);
	if (!g) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	int32_t ph = picker_total_h(g);
	if (!region_toolbar_popup_pos(st, g->btn, TP_W, ph, TP_GAP, out_x, out_y)) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_w = TP_W;
	*out_h = ph;
}

enum tool_picker_kind region_tool_picker_hit(const struct ro_state *st,
											 int32_t abs_x, int32_t abs_y, int *value) {
	const struct tool_group *g = toolbar_tool_group(st->picker_group);
	if (!g) return TP_NONE;
	int32_t px, py, pw, ph;
	region_tool_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return TP_NONE;
	if (abs_x < px || abs_x >= px + pw) return TP_NONE;
	int32_t ly = abs_y - (py + TP_PAD);
	if (ly < 0) return TP_NONE;
	if (ly < g->n * TP_ROW_H) {
		*value = g->tools[ly / TP_ROW_H];
		return TP_TOOL;
	}
	if (!g->has_style) return TP_NONE;
	ly -= g->n * TP_ROW_H + TP_DIVIDER_H;
	if (ly >= 0 && ly < STROKE_STYLE_COUNT * TP_ROW_H) {
		*value = ly / TP_ROW_H;
		return TP_STYLE;
	}
	return TP_NONE;
}

static void picker_row(cairo_t *cr, double x0, double w, double ry,
					   int32_t S, bool sel, const char *label) {
	double rh = TP_ROW_H * S;
	if (sel) {
		grabit_cairo_rect_r(cr, x0 + 3.0 * S, ry + 1.0 * S, w - 6.0 * S, rh - 2.0 * S,
							grabit_ui_radius(GUI_R_BTN) * S);
		cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, 0.22);
		cairo_fill(cr);
	}
	cairo_set_source_rgba(cr, sel ? 1.0 : 0.86, sel ? 1.0 : 0.86,
						  sel ? 1.0 : 0.86, 1.0);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, label, &ext);
	cairo_move_to(cr, x0 + TP_ROW_H * S * 1.25,
				  ry + rh / 2.0 - ext.height / 2.0 - ext.y_bearing);
	cairo_show_text(cr, label);
}

void region_tool_picker_render(cairo_t *cr, const struct ro_output *o) {
	const struct ro_state *st = o->st;
	const struct tool_group *g = toolbar_tool_group(st->picker_group);
	if (!g) return;

	int32_t px, py, pw, ph;
	region_tool_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return;
	if (!grabit_output_overlaps(o->go, (struct rect){px, py, pw, ph})) return;

	int32_t S = o->scale;
	double x0 = (double)(px - o->go->x) * S;
	double y0 = (double)(py - o->go->y) * S;
	double w = (double)pw * S;
	double h = (double)ph * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	grabit_ui_panel(cr, x0, y0, w, h, (double)S);

	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.0 * S);

	double icon_s = TP_ROW_H * S * 0.72;
	double icon_x = x0 + TP_ROW_H * S * 0.72;
	double top = y0 + TP_PAD * S;
	bool tool_active = st->region_locked && !st->anno_edit_mode;

	for (int i = 0; i < g->n; i++) {
		double ry = top + i * TP_ROW_H * S;
		bool sel = tool_active && st->current_tool == g->tools[i];
		picker_row(cr, x0, w, ry, S, sel, g->labels[i]);
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, 1.0);
		toolbar_icon_for_tool(cr, g->tools[i], icon_x,
							  ry + TP_ROW_H * S / 2.0, icon_s);
	}

	if (!g->has_style) {
		cairo_restore(cr);
		return;
	}

	double div_y = top + g->n * TP_ROW_H * S + TP_DIVIDER_H * S / 2.0;
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, (double)S);
	cairo_move_to(cr, x0 + 8.0 * S, div_y);
	cairo_line_to(cr, x0 + w - 8.0 * S, div_y);
	cairo_stroke(cr);

	double stop = top + g->n * TP_ROW_H * S + TP_DIVIDER_H * S;
	for (int i = 0; i < STROKE_STYLE_COUNT; i++) {
		double ry = stop + i * TP_ROW_H * S;
		bool sel = (enum stroke_style)i == st->current_style;
		char label[16];
		snprintf(label, sizeof label, "%s", grabit_line_style_names[i]);
		if (label[0] >= 'a' && label[0] <= 'z') label[0] -= 'a' - 'A';
		picker_row(cr, x0, w, ry, S, sel, label);
		cairo_set_source_rgba(cr, 0.92, 0.92, 0.92, 1.0);
		toolbar_icon_line_style(cr, icon_x, ry + TP_ROW_H * S / 2.0, icon_s,
								(enum stroke_style)i);
	}

	cairo_restore(cr);
}
