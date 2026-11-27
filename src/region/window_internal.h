// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_WINDOW_INTERNAL_H
#define GRABIT_REGION_WINDOW_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>

#include "region/wlr_state.h"
#include "ui/window.h"
#include "wl/wl.h"

struct xdg_surface;
struct xdg_toplevel;
struct region_window_req;

#define RWIN_PAD 12
#define RWIN_ZOOM_MIN 0.02
#define RWIN_ZOOM_MAX 32.0
#define RWIN_ZOOM_STEP 1.25
#define RWIN_MAX_ENTERED 16

struct wl_output;

struct ro_window {
	struct grabit_output go;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct ui_window frame;
	int32_t pending_w;
	int32_t pending_h;
	int32_t float_w;
	int32_t float_h;
	int32_t min_w;
	int32_t min_h;
	double zoom;
	double pan_x;
	double pan_y;
	bool fit;
	bool panning;
	uint32_t pan_button;
	int32_t pan_ux;
	int32_t pan_uy;
	uint32_t title_click_ms;
	bool grabbed;
	struct wl_output *entered[RWIN_MAX_ENTERED];
	size_t n_entered;
};

int region_window_create(struct ro_state *st, struct grabit_wl_state *s,
						 const struct region_window_req *req);
void region_window_destroy(struct ro_state *st);
void region_window_canvas(const struct ro_state *st, struct rect *out);
void region_window_view(struct ro_output *o);
void region_window_cursor_from_ui(struct ro_state *st);
double region_view_zoom(const struct ro_state *st);
void region_window_zoom_at(struct ro_state *st, double factor, int32_t ux, int32_t uy);
void region_window_zoom_to(struct ro_state *st, double zoom, int32_t ux, int32_t uy);
void region_window_fit(struct ro_state *st);
bool region_window_motion(struct ro_state *st);
bool region_window_button(struct ro_state *st, uint32_t serial, uint32_t time,
						  uint32_t button, uint32_t state);
int region_window_cursor(const struct ro_state *st);
void region_window_draw_frame(cairo_t *cr, struct ro_output *o);

#endif
