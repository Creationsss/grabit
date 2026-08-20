// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_WLR_INPUT_STATE_H
#define GRABIT_REGION_WLR_INPUT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct annotation;
struct ro_state;

struct annotation *region_anno_selected(const struct ro_state *st);

#define HANDLE_NONE -1
#define HANDLE_NW 0
#define HANDLE_N 1
#define HANDLE_NE 2
#define HANDLE_E 3
#define HANDLE_SE 4
#define HANDLE_S 5
#define HANDLE_SW 6
#define HANDLE_W 7
#define HANDLE_RADIUS 9

void region_handle_points(const struct ro_state *st, int32_t hx[8], int32_t hy[8]);
int region_handle_at(const struct ro_state *st, int32_t x, int32_t y);
void region_apply_handle_drag(struct ro_state *st);

void region_undo_arm(struct ro_state *st);
void region_undo_disarm(struct ro_state *st);

void region_undo_begin(struct ro_state *st);
void region_undo_commit(struct ro_state *st);
void region_undo_record_anno_move(struct ro_state *st, size_t idx,
								  int32_t dx, int32_t dy);
void region_undo_record_anno_geom(struct ro_state *st, size_t idx,
								  const int32_t g[4]);
void region_undo_pop(struct ro_state *st);
void region_redo_pop(struct ro_state *st);
void region_undo_free(struct ro_state *st);
void region_undo_record_anno_delete(struct ro_state *st, size_t idx,
									struct annotation *a);
void region_undo_record_anno_size(struct ro_state *st, size_t idx);
void region_undo_record_anno_color(struct ro_state *st, size_t idx);
void region_undo_record_selected_sizes(struct ro_state *st, int font_filter);
uint32_t region_active_color(const struct ro_state *st);
void region_apply_color(struct ro_state *st, uint32_t color, bool record);
void region_slider_range(const struct ro_state *st, int32_t *lo, int32_t *hi);
int32_t region_active_slider(const struct ro_state *st, int32_t *lo, int32_t *hi);
void region_apply_slider(struct ro_state *st, int32_t value, bool record);
void region_undo_group_begin(struct ro_state *st);
void region_undo_group_end(struct ro_state *st);
void region_delete_selected(struct ro_state *st);
bool region_has_selection(const struct ro_state *st);
const struct annotation *region_single_selection(const struct ro_state *st);
void region_clear_selection(struct ro_state *st);
void region_select_one(struct ro_state *st, size_t idx);
void region_select_toggle(struct ro_state *st, size_t idx);

#define NUDGE_LEFT (1u << 0)
#define NUDGE_RIGHT (1u << 1)
#define NUDGE_UP (1u << 2)
#define NUDGE_DOWN (1u << 3)

void region_nudge_press(struct ro_state *st, uint32_t dir);
void region_nudge_release(struct ro_state *st, uint32_t dir);
void region_nudge_disarm(struct ro_state *st);
void region_nudge_tick(struct ro_state *st);

void region_tooltip_arm(struct ro_state *st);
void region_tooltip_disarm(struct ro_state *st);

void region_drag_start(struct ro_state *st);
bool region_drag_active(const struct ro_state *st);
void region_drag_abort(struct ro_state *st);

bool region_set_hover(struct ro_state *st, int btn);

void region_clamp_move(struct ro_state *st);
void region_update_selection(struct ro_state *st);
bool region_inside_selection(const struct ro_state *st, int32_t x, int32_t y);

int region_snap_hit(const struct ro_state *st, int32_t x, int32_t y);
void region_snap_set_hover(struct ro_state *st, int hover);
bool region_snap_tick(struct ro_state *st);

void region_pen_append(struct ro_state *st, int32_t x, int32_t y);
void region_commit_drawing(struct ro_state *st);
void region_commit_text(struct ro_state *st);
void region_place_counter(struct ro_state *st);

void region_apply_shape_snap(int tool, bool shift, int32_t x0, int32_t y0,
							 int32_t *x1, int32_t *y1);

#endif
