// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_UI_H
#define GRABIT_REGION_UI_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>

struct ro_state;
struct ro_output;
struct wl_surface;
struct grabit_wl_state;
struct config;
struct rect;
struct grabit_output;

enum tb_action {
	TB_NONE = -1,
	TB_REGION = 0,
	TB_EDIT,
	TB_TOOL_LINES,
	TB_TOOL_SHAPES,
	TB_TOOL_ARROW,
	TB_TOOL_REDACT,
	TB_TOOL_TEXT,
	TB_TOOL_COUNTER,
	TB_TOOL_ERASER,
	TB_COLOR_RED,
	TB_COLOR_YELLOW,
	TB_COLOR_GREEN,
	TB_COLOR_BLUE,
	TB_COLOR_BLACK,
	TB_COLOR_WHITE,
	TB_COLOR_CURRENT,
	TB_WIDTH_SLIDER,
	TB_UNDO,
	TB_REDO,
	TB_SAVE,
	TB_CANCEL,
	TB_BTN_COUNT,
};

#define TB_TOOL_GROUP_COUNT 3

#define TB_BTN_W 38
#define TB_BTN_H 38
#define TB_PAD 6
#define TB_GAP 12

void region_toolbar_rect(const struct ro_state *st,
						 const struct grabit_output **out_o,
						 int32_t *x, int32_t *y, int32_t *w, int32_t *h);
enum tb_action region_toolbar_hit(const struct ro_state *st,
								  int32_t abs_x, int32_t abs_y);
bool region_toolbar_contains(const struct ro_state *st, int32_t abs_x, int32_t abs_y);
void region_toolbar_slider_rect(const struct ro_state *st,
								int32_t *out_x, int32_t *out_y,
								int32_t *out_w, int32_t *out_h);
void region_color_picker_rect(const struct ro_state *st,
							  int32_t *out_x, int32_t *out_y,
							  int32_t *out_w, int32_t *out_h);
void region_color_input_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h);
void region_color_eyedropper_rect(const struct ro_state *st,
								  int32_t *out_x, int32_t *out_y,
								  int32_t *out_w, int32_t *out_h);
bool region_color_picker_pick(const struct ro_state *st, int32_t abs_x, int32_t abs_y,
							  uint32_t *out_color);
bool region_parse_hex_color(const char *s, uint32_t *out);
void region_color_picker_render(cairo_t *cr, const struct ro_output *o);
void region_color_picker_release_cache(struct ro_state *st);

enum tool_picker_kind { TP_NONE,
						TP_TOOL,
						TP_STYLE };

void region_tool_picker_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h);
enum tool_picker_kind region_tool_picker_hit(const struct ro_state *st,
											 int32_t abs_x, int32_t abs_y, int *value);
void region_tool_picker_render(cairo_t *cr, const struct ro_output *o);

#define WIDTH_MIN 1
#define WIDTH_MAX 12
#define FONT_MIN 8
#define FONT_MAX 72
void region_toolbar_render(cairo_t *cr, const struct ro_output *o);
void region_toolbar_tooltip_render(cairo_t *cr, const struct ro_output *o);

bool region_magnifier_active(const struct ro_output *o);
void region_magnifier_render(cairo_t *cr, const struct ro_output *o);
bool region_coords_active(const struct ro_output *o);
void region_coords_render(cairo_t *cr, const struct ro_output *o);

void region_render_attach_layer(struct ro_output *o);
void region_render_free_buffer(struct ro_output *o);
void region_render_request_redraw_all(struct ro_state *st);
struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s);

void region_input_attach(struct ro_state *st);

struct config;
struct rect;
struct grabit_output;

void gregion_apply_config(struct ro_state *st, struct config *cfg, bool annotate_mode,
						  struct grabit_wl_state *s, const struct rect *snap_rects,
						  size_t n_snap_rects);
void gregion_create_surfaces(struct ro_state *st, struct grabit_wl_state *s);
void gregion_select_teardown(struct ro_state *st, struct grabit_wl_state *s);

#endif
