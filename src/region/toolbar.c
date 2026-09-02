// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "cairo_util.h"
#include "region/wlr_input_state.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <math.h>

#include <cairo/cairo.h>

static bool button_active(const struct ro_state *st, enum tb_action act) {
	if (act == TB_REGION) return !st->region_locked;
	if (act == TB_EDIT) return st->anno_edit_mode;
	bool tool_active = st->region_locked && !st->anno_edit_mode;
	const struct tool_group *g = toolbar_tool_group(act);
	if (g)
		return st->picker_group == act ||
			   (tool_active && toolbar_group_of_tool(st->current_tool) == g);
	int32_t stool = toolbar_standalone_tool(act);
	if (stool >= 0)
		return tool_active && st->current_tool == (enum tool_kind)stool;
	if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE)
		return region_active_color(st) == TOOLBAR_COLORS[act - TB_COLOR_RED];
	return act == TB_UNDO && st->undo_held;
}

static void paint_button_bg(cairo_t *cr, const struct ro_state *st,
							enum tb_action act, int32_t S,
							double bxi, double byi, double bwi, double bhi) {
	bool active = button_active(st, act);
	double pad = 3.0 * S;
	double rr = 0.18, gg = 0.18, bb = 0.18, aa = 0.94;
	if (active) {
		rr = 1.0;
		gg = 0.45;
		bb = 0.28;
		aa = 0.92;
	} else if (act == TB_SAVE) {
		rr = 0.20;
		gg = 0.58;
		bb = 0.32;
		aa = st->has_selection ? 0.96 : 0.35;
	} else if (act == TB_CANCEL) {
		rr = 0.62;
		gg = 0.22;
		bb = 0.22;
		aa = 0.96;
	}
	cairo_set_source_rgba(cr, rr, gg, bb, aa);
	grabit_cairo_rect_r(cr, bxi + pad, byi + pad, bwi - pad * 2, bhi - pad * 2,
						grabit_ui_radius(GUI_R_BTN) * S);
	cairo_fill(cr);
}

static void paint_slider(cairo_t *cr, struct ro_state *st, int32_t S,
						 double bxi, double bwi, double cyi) {
	double pad_in = 10.0 * S;
	double tk_x0 = bxi + pad_in;
	double tk_x1 = bxi + bwi - pad_in;
	cairo_set_source_rgba(cr, 0.55, 0.55, 0.55, 1);
	cairo_set_line_width(cr, 2.0 * S);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, tk_x0, cyi);
	cairo_line_to(cr, tk_x1, cyi);
	cairo_stroke(cr);

	int32_t lo, hi;
	int32_t val = region_active_slider(st, &lo, &hi);
	if (val < lo) val = lo;
	if (val > hi) val = hi;
	double frac = (double)(val - lo) / (double)(hi - lo);
	double kx = tk_x0 + frac * (tk_x1 - tk_x0);
	double kr = (3.0 + frac * 6.0) * S;
	cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
	cairo_arc(cr, kx, cyi, kr, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
	cairo_set_line_width(cr, 1.0 * S);
	cairo_arc(cr, kx, cyi, kr, 0, 2.0 * M_PI);
	cairo_stroke(cr);
}

static void paint_tool_icon(cairo_t *cr, enum tb_action act, double cxi, double cyi, double s_icon) {
	switch (act) {
	case TB_REGION:
		toolbar_icon_region(cr, cxi, cyi, s_icon);
		break;
	case TB_EDIT:
		toolbar_icon_select(cr, cxi, cyi, s_icon);
		break;
	case TB_TOOL_TEXT:
		toolbar_icon_text(cr, cxi, cyi, s_icon);
		break;
	case TB_TOOL_COUNTER:
		toolbar_icon_counter(cr, cxi, cyi, s_icon);
		break;
	case TB_TOOL_CALLOUT:
		toolbar_icon_callout(cr, cxi, cyi, s_icon);
		break;
	case TB_TOOL_ERASER:
		toolbar_icon_eraser(cr, cxi, cyi, s_icon);
		break;
	case TB_REDO:
		toolbar_icon_redo(cr, cxi, cyi, s_icon);
		break;
	case TB_UNDO:
		toolbar_icon_undo(cr, cxi, cyi, s_icon);
		break;
	case TB_SAVE:
		toolbar_icon_save(cr, cxi, cyi, s_icon);
		break;
	case TB_CANCEL:
		toolbar_icon_cancel(cr, cxi, cyi, s_icon);
		break;
	default:
		break;
	}
}

void region_toolbar_render(cairo_t *cr, const struct ro_output *o) {
	int32_t S = o->scale;
	int32_t tx, ty, tw, th;
	region_toolbar_rect(o->st, NULL, &tx, &ty, &tw, &th);
	if (!grabit_output_overlaps(o->go, (struct rect){tx, ty, tw, th})) return;

	double bx0 = (double)(tx - o->go->x) * S;
	double by0 = (double)(ty - o->go->y) * S;
	double bw = (double)tw * S;
	double bh = (double)th * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	grabit_ui_panel(cr, bx0, by0, bw, bh, (double)S);

	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action act = (enum tb_action)i;
		int32_t bx_local, by_local, bw_local, bh_local;
		toolbar_btn_rect_local(act, tw, &bx_local, &by_local, &bw_local, &bh_local);
		double bxi = bx0 + (double)bx_local * S;
		double byi = by0 + (double)by_local * S;
		double bwi = (double)bw_local * S;
		double bhi = (double)bh_local * S;

		bool active = button_active(o->st, act);
		bool is_color = (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE);
		bool is_slider = (act == TB_WIDTH_SLIDER);
		bool is_current = (act == TB_COLOR_CURRENT);

		if (!is_color && !is_slider && !is_current)
			paint_button_bg(cr, o->st, act, S, bxi, byi, bwi, bhi);

		double cxi = bxi + bwi / 2.0;
		double cyi = byi + bhi / 2.0;
		double s_icon = (double)bh_local * S * 0.6;

		if (is_color) {
			toolbar_color_swatch(cr, cxi, cyi, s_icon,
								 TOOLBAR_COLORS[act - TB_COLOR_RED], active);
			continue;
		}
		if (is_current) {
			toolbar_color_current(cr, cxi, cyi, s_icon,
								  region_active_color(o->st),
								  o->st->color_picker_open);
			continue;
		}
		if (is_slider) {
			paint_slider(cr, o->st, S, bxi, bwi, cyi);
			continue;
		}

		double ia = (act == TB_SAVE && !o->st->has_selection) ? 0.45 : 1.0;
		cairo_set_source_rgba(cr, active ? 1.0 : 0.92,
							  active ? 1.0 : 0.92,
							  active ? 1.0 : 0.92, ia);
		const struct tool_group *g = toolbar_tool_group(act);
		if (g) {
			enum tool_kind gt = o->st->group_tool[toolbar_group_index(g)];
			toolbar_icon_for_tool(cr, gt, cxi, cyi, s_icon);
			continue;
		}
		paint_tool_icon(cr, act, cxi, cyi, s_icon);
	}

	cairo_restore(cr);
}
