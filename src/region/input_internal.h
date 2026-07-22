// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_INPUT_INTERNAL_H
#define GRABIT_REGION_INPUT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "region/region.h"
#include "region/wlr_state.h"

bool ginp_eyedropper_sample(struct ro_state *st, uint32_t *out_color);
bool ginp_region_abort_active(struct ro_state *st, struct wl_pointer *p);
bool ginp_toolbar_button_event(struct ro_state *st, struct wl_pointer *p, uint32_t state);
bool ginp_toolbar_reachable(const struct ro_state *st);
extern const struct wl_pointer_listener ginp_pointer_listener_g;
int ginp_anno_corner_at(const struct ro_state *st, int32_t x, int32_t y);
int ginp_anno_hit_index(const struct ro_state *st, int32_t x, int32_t y);
struct wl_cursor *ginp_pick_cursor(const struct ro_state *st, int32_t abs_x, int32_t abs_y);
uint32_t ginp_region_nudge_for_key(struct ro_state *st, xkb_keysym_t sym, uint8_t mods);
void ginp_apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial, struct ro_output *o, struct wl_cursor *c);
void ginp_lock_or_finish(struct ro_state *st);
void ginp_mode_enter_anno_edit(struct ro_state *st);
void ginp_mode_enter_region(struct ro_state *st);
void ginp_mode_select_tool(struct ro_state *st, enum tool_kind t);
void ginp_pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
void ginp_refresh_cursor(struct ro_state *st, struct wl_pointer *p);
void ginp_region_do_confirm(struct ro_state *st);
void ginp_slider_set_width_from_cursor(struct ro_state *st);

#endif
