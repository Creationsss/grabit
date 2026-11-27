// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

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
	case TB_TOOL_COUNTER:
	case TB_TOOL_CALLOUT:
	case TB_TOOL_ERASER:
		return region_tool_label(toolbar_standalone_tool(act));
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

	double off = st->win ? (double)(st->ui_bounds.y - o->go->y) * S : 0.0;
	cairo_save(cr);
	cairo_translate(cr, 0, off);
	grabit_ui_tooltip(cr, (double)S, text, btn_cx, btn_top - off, btn_bot - off,
					  (double)o->pixel_width);
	cairo_restore(cr);
}
