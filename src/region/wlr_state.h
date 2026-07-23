// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_WLR_STATE_H
#define GRABIT_REGION_WLR_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

#include "region/ui.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "region/keybinds.h"
#include "region/region.h"

struct grabit_wl_state;
struct grabit_output;
struct image;
struct wl_cursor_theme;
struct wl_cursor;
struct zwlr_layer_surface_v1;

struct ro_state;

struct ro_output {
	struct ro_state *st;
	struct grabit_output *go;
	size_t idx;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	int32_t width;
	int32_t height;
	int32_t pixel_width;
	int32_t pixel_height;
	int32_t scale;
	bool configured;

	int stride;
	size_t buf_size;
	void *buf_data;
	struct wl_buffer *buffer;

	cairo_surface_t *cairo_dst;
	cairo_surface_t *cairo_frozen;
	cairo_pattern_t *cairo_frozen_pat;

	cairo_surface_t *anno_cache;
	size_t anno_cache_gen;
	struct rect anno_cache_sel;
	int32_t anno_cache_skip;

	bool dirty;
	struct wl_callback *frame_cb;
};

enum undo_kind {
	UNDO_ANNO_ADD,
	UNDO_ANNO_READD,
	UNDO_ANNO_DELETE,
	UNDO_ANNO_REDELETE,
	UNDO_REGION,
	UNDO_ANNO_MOVE,
	UNDO_ANNO_GEOM,
	UNDO_ANNO_SIZE,
};

struct undo_item {
	enum undo_kind kind;
	uint32_t group;
	union {
		struct {
			bool has;
			struct rect r;
		} region;
		struct {
			size_t idx;
			int32_t dx;
			int32_t dy;
		} move;
		struct {
			size_t idx;
			int32_t g[4];
		} geom;
		struct {
			size_t idx;
			int32_t width;
			int32_t font_size;
		} size;
		struct {
			struct annotation a;
		} readd;
		struct {
			size_t idx;
			struct annotation a;
		} del;
	} u;
};

struct ro_state {
	struct grabit_wl_state *wls;
	struct ro_output *outs;
	size_t n_outs;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor;
	struct wl_cursor *cursor_text;
	struct wl_cursor *cursor_default;
	struct wl_cursor *cursor_move;
	struct wl_cursor *cursor_hand;
	struct wl_cursor *cursor_resize[8];
	struct wl_cursor *current_cursor;
	struct wl_surface *cursor_surface;
	uint32_t last_cursor_serial;

	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct ro_output *cursor_on;
	int32_t cursor_x;
	int32_t cursor_y;

	bool dragging;
	int32_t drag_x0;
	int32_t drag_y0;

	bool has_selection;
	int32_t sel_x;
	int32_t sel_y;
	int32_t sel_w;
	int32_t sel_h;
	struct rect bounds;

	struct rect *snap_windows;
	size_t n_snap_windows;
	int snap_hover;

	bool finished;
	bool cancelled;
	bool cleanup;

	const struct image *frozen;

	bool annotate_mode;
	bool confirm_mode;
	bool resizing_anno;
	uint8_t multi_select_mods;
	bool edit_instant;
	bool region_locked;
	enum tool_kind current_tool;

	bool drawing;
	int32_t draw_x0;
	int32_t draw_y0;
	int32_t *pen_points;
	size_t pen_n;
	size_t pen_cap;

	bool text_input_active;
	char text_buf[256];
	size_t text_len;
	int32_t text_x;
	int32_t text_y;

	struct annotation_list *out_annos;

	uint32_t current_color;
	int32_t current_width;
	int32_t current_font;
	enum stroke_style current_style;
	enum tool_kind current_line_tool;
	double scroll_accum;
	bool edit_choices_dirty;
	bool shift_held;
	bool ctrl_held;
	bool magnifier_held;
	bool show_coords;
	struct region_keymap keys;
	int handle_dragging;
	bool moving_region;
	bool region_moved;
	int32_t move_grab_dx;
	int32_t move_grab_dy;
	uint32_t last_inside_press;
	bool slider_dragging;
	bool tb_dragging;
	bool tb_moved;
	int32_t tb_x;
	int32_t tb_y;
	int32_t tb_grab_dx;
	int32_t tb_grab_dy;
	const struct grabit_output *tb_out;
	const struct grabit_output *tb_lock;
	bool eyedropper_mode;
	bool color_picker_open;
	bool color_picker_dragging;
	bool line_picker_open;
	bool color_input_active;
	char color_input_buf[8];
	size_t color_input_len;

	int undo_timer_fd;
	bool undo_held;
	struct undo_item *undo_items;
	size_t undo_n;
	size_t undo_cap;
	struct undo_item *redo_items;
	size_t redo_n;
	size_t redo_cap;
	uint32_t undo_group_seq;
	uint32_t undo_group_active;
	struct rect undo_snap;
	bool undo_snap_has;
	bool undo_snap_armed;

	bool anno_edit_mode;
	int32_t sel_anno;
	int anno_drag;
	int32_t anno_press_x;
	int32_t anno_press_y;
	int32_t anno_last_x;
	int32_t anno_last_y;
	int32_t anno_geom_snap[4];

	int nudge_timer_fd;
	uint32_t nudge_held;
	int32_t nudge_ticks;

	int tooltip_timer_fd;
	int hovered_button;
	bool tooltip_visible;

	cairo_pattern_t *picker_rainbow_pat;
	cairo_pattern_t *picker_top_pat;
	cairo_pattern_t *picker_bot_pat;
	int32_t picker_pat_dw;
	int32_t picker_pat_dh;
};

static inline bool region_editing(const struct ro_state *st) {
	return st->annotate_mode && st->out_annos;
}

static inline bool region_tool_uses_font(const struct ro_state *st) {
	return st->current_tool == TOOL_TEXT || st->current_tool == TOOL_COUNTER ||
		   st->text_input_active;
}

static inline bool region_multi_select_held(const struct ro_state *st) {
	return st->xkb_state && st->multi_select_mods &&
		   (region_xkb_mods(st->xkb_state) & st->multi_select_mods) != 0;
}

static inline int32_t *region_slider_field(struct ro_state *st, int32_t *lo, int32_t *hi) {
	if (region_tool_uses_font(st)) {
		*lo = FONT_MIN;
		*hi = FONT_MAX;
		return &st->current_font;
	}
	*lo = WIDTH_MIN;
	*hi = WIDTH_MAX;
	return &st->current_width;
}

#define ANNO_DRAG_NONE (-1)
#define ANNO_DRAG_MOVE 4

static inline bool region_anno_dragging(const struct ro_state *st) {
	return st->anno_drag >= 0;
}

#endif
