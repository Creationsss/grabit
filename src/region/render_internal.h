// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_RENDER_INTERNAL_H
#define GRABIT_REGION_RENDER_INTERNAL_H

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "region/wlr_state.h"

extern const struct wl_callback_listener gren_frame_listener_g;
void gren_anno_cache_ensure(struct ro_output *o);
void gren_anno_cache_paint(cairo_t *cr, cairo_surface_t *cache);
void gren_output_redraw(struct ro_output *o);
void gren_paint_anno_selection(cairo_t *cr, const struct ro_state *st);
void gren_render_bottom_hint(cairo_t *cr, const struct ro_output *o, const char *hint);

int gren_output_alloc_buffer(struct ro_output *o);

#endif
