// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "cairo_util.h"
#include "region/keybinds.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <stdio.h>

#include <cairo/cairo.h>

static const char *plain_label(enum tb_action act, enum region_action *out_action) {
	*out_action = KA_COUNT;
	switch (act) {
	case TB_REGION:
		*out_action = KA_REGION_MODE;
		return "Select region";
	case TB_EDIT:
		*out_action = KA_EDIT_MODE;
		return "Move/resize annotations";
	case TB_TOOL_TEXT:
		return "Text";
	case TB_TOOL_COUNTER:
		return "Counter";
	case TB_TOOL_CALLOUT:
		return "Callout";
	case TB_TOOL_ERASER:
		return "Eraser";
	case TB_COLOR_CURRENT:
		return "Current color";
	case TB_WIDTH_SLIDER:
		return "Size  (drag or scroll)";
	case TB_UNDO:
		*out_action = KA_UNDO;
		return "Undo";
	case TB_REDO:
		*out_action = KA_REDO;
		return "Redo";
	case TB_SAVE:
		*out_action = KA_CONFIRM;
		return "Capture";
	case TB_CANCEL:
		*out_action = KA_CANCEL;
		return "Cancel";
	default:
		return NULL;
	}
}

static const char *tooltip_text(const struct ro_state *st, enum tb_action act,
								char *buf, size_t cap) {
	char key[64] = "";
	const struct tool_group *g = toolbar_tool_group(act);
	if (g) {
		region_keybind_first_tool(&st->keys, g->tools[0], key, sizeof key);
		if (!key[0]) return g->label;
		snprintf(buf, cap, "%s  (%s cycles)", g->label, key);
		return buf;
	}

	enum region_action ra;
	const char *label = plain_label(act, &ra);
	if (!label) return NULL;
	if (ra != KA_COUNT) {
		region_keybind_first(&st->keys, ra, key, sizeof key);
	} else {
		int32_t tool = toolbar_standalone_tool(act);
		if (tool >= 0) region_keybind_first_tool(&st->keys, (int)tool, key, sizeof key);
	}
	if (!key[0]) return label;
	snprintf(buf, cap, "%s  (%s)", label, key);
	return buf;
}

void region_toolbar_tooltip_render(cairo_t *cr, const struct ro_output *o) {
	const struct ro_state *st = o->st;
	if (!st->tooltip_visible || st->hovered_button < 0) return;
	char tipbuf[160];
	const char *text =
		tooltip_text(st, (enum tb_action)st->hovered_button, tipbuf, sizeof tipbuf);
	if (!text) return;

	int32_t tx, ty, tw, th;
	region_toolbar_rect(st, NULL, &tx, &ty, &tw, &th);
	if (!grabit_output_overlaps(o->go, (struct rect){tx, ty, tw, th})) return;

	int32_t S = o->scale;
	double bx0 = (double)(tx - o->go->x) * S;
	double by0 = (double)(ty - o->go->y) * S;

	int32_t bx_local, by_local, bw_local, bh_local;
	toolbar_btn_rect_local((enum tb_action)st->hovered_button, tw,
						   &bx_local, &by_local, &bw_local, &bh_local);
	double btn_cx = bx0 + ((double)bx_local + (double)bw_local / 2.0) * S;
	double btn_top = by0 + (double)by_local * S;
	double btn_bot = btn_top + (double)bh_local * S;

	cairo_save(cr);
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.0 * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);

	double pad_x = 9.0 * S, pad_y = 6.0 * S;
	double tip_w = ext.width + pad_x * 2;
	double tip_h = ext.height + pad_y * 2;
	double gap = 8.0 * S;

	double tip_x = btn_cx - tip_w / 2.0;
	double tip_y = btn_top - gap - tip_h;
	bool below = false;
	if (tip_y < (double)S * 4.0) {
		tip_y = btn_bot + gap;
		below = true;
	}
	double pw = (double)o->pixel_width;
	if (tip_x < (double)S * 4.0) tip_x = (double)S * 4.0;
	if (tip_x + tip_w > pw - (double)S * 4.0) tip_x = pw - tip_w - (double)S * 4.0;

	double r = grabit_cairo_clamp_r(tip_w, tip_h, grabit_ui_radius(GUI_R_TOOLTIP) * S);
	grabit_cairo_rect_r(cr, tip_x, tip_y, tip_w, tip_h, r);
	cairo_set_source_rgba(cr, 0.04, 0.04, 0.04, 0.94);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
	cairo_set_line_width(cr, (double)S);
	cairo_stroke(cr);

	double arrow_h = 5.0 * S;
	double arrow_w = 9.0 * S;
	double ax = btn_cx;
	if (ax < tip_x + r + arrow_w / 2.0) ax = tip_x + r + arrow_w / 2.0;
	if (ax > tip_x + tip_w - r - arrow_w / 2.0) ax = tip_x + tip_w - r - arrow_w / 2.0;
	cairo_set_source_rgba(cr, 0.04, 0.04, 0.04, 0.94);
	if (below) {
		cairo_move_to(cr, ax - arrow_w / 2.0, tip_y);
		cairo_line_to(cr, ax + arrow_w / 2.0, tip_y);
		cairo_line_to(cr, ax, tip_y - arrow_h);
	} else {
		cairo_move_to(cr, ax - arrow_w / 2.0, tip_y + tip_h);
		cairo_line_to(cr, ax + arrow_w / 2.0, tip_y + tip_h);
		cairo_line_to(cr, ax, tip_y + tip_h + arrow_h);
	}
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, tip_x + pad_x - ext.x_bearing,
				  tip_y + pad_y - ext.y_bearing);
	cairo_show_text(cr, text);
	cairo_restore(cr);
}
