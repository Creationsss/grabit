// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_TOOLBAR_INTERNAL_H
#define GRABIT_REGION_TOOLBAR_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>

#include "region/wlr_state.h"

#define TB_BTN_H_OPT 30
#define TB_ROW_GAP 4
#define TB_SLIDER_W 110

#define GRABIT_ACCENT_R 1.0
#define GRABIT_ACCENT_G 0.55
#define GRABIT_ACCENT_B 0.32

extern const uint32_t TOOLBAR_COLORS[6];

void toolbar_btn_rect_local(enum tb_action act, int32_t tw,
							int32_t *out_x, int32_t *out_y,
							int32_t *out_w, int32_t *out_h);

struct tool_group {
	enum tb_action btn;
	const enum tool_kind *tools;
	int n;
	bool has_style;
	const char *const *labels;
	const char *tip;
};

const struct tool_group *toolbar_tool_group(enum tb_action btn);
const struct tool_group *toolbar_group_of_tool(enum tool_kind t);
int toolbar_group_index(const struct tool_group *g);
enum tool_kind toolbar_group_default(int idx);
int32_t toolbar_standalone_tool(enum tb_action btn);

bool region_toolbar_popup_pos(const struct ro_state *st, enum tb_action anchor,
							  int32_t pw, int32_t place_h, int32_t gap,
							  int32_t *out_x, int32_t *out_y);

void toolbar_icon_region(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_select(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_pen(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_marker(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_line(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_rect(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_ellipse(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_arrow(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_blur(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_pixelate(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_spotlight(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_text(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_counter(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_callout(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_eraser(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_line_style(cairo_t *cr, double cx, double cy, double s,
							 enum stroke_style style, bool rounded);
void toolbar_icon_rrect(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_rarrow(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_rtext(cairo_t *cr, double cx, double cy, double s);
void toolbar_icon_for_tool(cairo_t *cr, enum tool_kind t,
						   double cx, double cy, double s, bool rounded);
void toolbar_icon_undo(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_redo(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_save(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_cancel(cairo_t *cr, double cx, double cy, double s, bool rounded);
void toolbar_icon_color_picker(cairo_t *cr, double cx, double cy, double s);
void toolbar_color_swatch(cairo_t *cr, double cx, double cy, double s,
						  uint32_t color, bool active);
void toolbar_color_current(cairo_t *cr, double cx, double cy, double s,
						   uint32_t color, bool active, bool rounded);

#endif
