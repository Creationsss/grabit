// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "region/input_internal.h"
#include "region/render_internal.h"
#include "region/ui.h"
#include "ui_theme.h"
#include "wl/wl.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include <cairo/cairo.h>

#define MAGNIFIER_CELLS 15
#define MAGNIFIER_CELL 10
#define MAGNIFIER_OFFSET 26
#define MAGNIFIER_PAD 6
#define MAGNIFIER_BORDER 2
#define MAGNIFIER_FONT 12
#define MAGNIFIER_SWATCH 12
#define MAGNIFIER_BOX_PAD_X 16
#define MAGNIFIER_BOX_PAD_Y 8
#define MAGNIFIER_SWATCH_INSET 5
#define MAGNIFIER_TEXT_GAP 10

static bool output_has_cursor(const struct ro_output *o) {
	if (!o->st->cursor_seen) return false;
	struct rect r;
	grabit_output_rect(o->go, &r);
	return rect_contains(r, o->st->cursor_x, o->st->cursor_y);
}

bool region_magnifier_active(const struct ro_output *o) {
	return o->st->magnifier_held && output_has_cursor(o) && o->cairo_frozen &&
		   !o->st->text_input_active;
}

bool region_coords_active(const struct ro_output *o) {
	return o->st->show_coords && output_has_cursor(o) &&
		   !o->st->text_input_active && !region_magnifier_active(o);
}

void region_coords_render(cairo_t *cr, const struct ro_output *o) {
	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;
	double cx = (o->st->cursor_x - o->go->x) * S;
	double cy = (o->st->cursor_y - o->go->y) * S;

	char info[32];
	snprintf(info, sizeof info, "%d, %d", o->st->cursor_x, o->st->cursor_y);

	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, MAGNIFIER_FONT * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, info, &ext);
	double pad = MAGNIFIER_PAD * S;
	double bh = ext.height + MAGNIFIER_BOX_PAD_Y * S;
	double bw = ext.width + MAGNIFIER_BOX_PAD_X * S;
	double bx = cx + MAGNIFIER_OFFSET * S;
	double by = cy + MAGNIFIER_OFFSET * S;
	if (bx + bw > pw - pad) bx = cx - MAGNIFIER_OFFSET * S - bw;
	if (by + bh > ph - pad) by = cy - MAGNIFIER_OFFSET * S - bh;
	bx = fmin(fmax(bx, pad), pw - bw - pad);
	by = fmin(fmax(by, pad), ph - bh - pad);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	grabit_cairo_rect_r(cr, bx, by, bw, bh, grabit_ui_radius(GUI_R_TIP) * S);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, bx + (bw - ext.width) / 2.0 - ext.x_bearing,
				  by + (bh - ext.height) / 2.0 - ext.y_bearing);
	cairo_show_text(cr, info);
}

static void paint_view(cairo_t *cr, const struct ro_output *o, bool edits,
					   const struct annotation *live) {
	cairo_set_source_surface(cr, o->cairo_frozen, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_paint(cr);
	if (!edits || !region_editing(o->st)) return;
	const struct image *img = &o->st->frozen[o->idx];
	cairo_scale(cr, (double)o->scale * img->width / o->pixel_width,
				(double)o->scale * img->height / o->pixel_height);
	cairo_translate(cr, -o->go->x, -o->go->y);
	gren_paint_annos(cr, o, live);
}

static uint32_t rgb_at(const void *data, int stride, int x, int y) {
	const uint8_t *row = (const uint8_t *)data + (size_t)y * (size_t)stride;
	return ((const uint32_t *)row)[x] & 0xFFFFFFu;
}

static uint32_t surface_rgb_at(cairo_surface_t *s, int x, int y) {
	cairo_surface_flush(s);
	return rgb_at(cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s), x, y);
}

static bool sample_view(const struct ro_output *o, int32_t fx, int32_t fy, uint32_t *out) {
	if (!o->cairo_frozen) return false;
	cairo_surface_t *px = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	if (cairo_surface_status(px) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(px);
		return false;
	}
	cairo_t *pc = cairo_create(px);
	cairo_translate(pc, -fx, -fy);
	paint_view(pc, o, true, NULL);
	cairo_destroy(pc);
	*out = surface_rgb_at(px, 0, 0);
	cairo_surface_destroy(px);
	return true;
}

bool ginp_sample_color(const struct ro_output *o, bool edits, uint32_t *out) {
	int32_t fx, fy;
	if (!o || !ginp_frozen_px(o->st, o, &fx, &fy)) return false;
	if (edits && sample_view(o, fx, fy, out)) return true;
	const struct image *img = &o->st->frozen[o->idx];
	*out = rgb_at(img->bytes, img->stride, fx, fy);
	return true;
}

void region_magnifier_render(cairo_t *cr, const struct ro_output *o,
							 const struct annotation *live) {
	int32_t fx, fy;
	if (!ginp_frozen_px(o->st, o, &fx, &fy)) return;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	int32_t px = (o->st->cursor_x - o->go->x) * S;
	int32_t py = (o->st->cursor_y - o->go->y) * S;

	const double cell = MAGNIFIER_CELL * S;
	const double L = MAGNIFIER_CELLS * cell;
	const double off = MAGNIFIER_OFFSET * S;
	const double pad = MAGNIFIER_PAD * S;

	double lx = px + off;
	if (lx + L > pw - pad) lx = px - off - L;
	double ly = py + off;
	if (ly + L > ph - pad) ly = py - off - L;
	lx = fmin(fmax(lx, pad), pw - L - pad);
	ly = fmin(fmax(ly, pad), ph - L - pad);

	const double lr = grabit_ui_radius(GUI_R_PANEL) * S;
	cairo_save(cr);
	grabit_cairo_rect_r(cr, lx, ly, L, L, lr);
	cairo_clip(cr);
	cairo_save(cr);
	cairo_translate(cr, lx + L / 2.0, ly + L / 2.0);
	cairo_scale(cr, cell, cell);
	cairo_translate(cr, -((double)fx + 0.5), -((double)fy + 0.5));
	paint_view(cr, o, o->st->magnifier_edits, live);
	cairo_restore(cr);
	uint32_t c = surface_rgb_at(cairo_get_target(cr), (int)(lx + L / 2.0), (int)(ly + L / 2.0));

	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.20);
	for (int i = 0; i <= MAGNIFIER_CELLS; i++) {
		double gx = lx + i * cell;
		cairo_move_to(cr, gx, ly);
		cairo_line_to(cr, gx, ly + L);
		double gy = ly + i * cell;
		cairo_move_to(cr, lx, gy);
		cairo_line_to(cr, lx + L, gy);
	}
	cairo_stroke(cr);
	cairo_restore(cr);

	int ci = (MAGNIFIER_CELLS - 1) / 2;
	double hx = lx + ci * cell, hy = ly + ci * cell;
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
	cairo_rectangle(cr, hx - 1.0, hy - 1.0, cell + 2.0, cell + 2.0);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
	cairo_rectangle(cr, hx, hy, cell, cell);
	cairo_stroke(cr);

	cairo_set_line_width(cr, MAGNIFIER_BORDER * S);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	grabit_cairo_rect_r(cr, lx, ly, L, L, lr);
	cairo_stroke(cr);

	char info[48];
	snprintf(info, sizeof info, "#%06X  %d, %d", c, o->st->cursor_x, o->st->cursor_y);

	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, MAGNIFIER_FONT * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, info, &ext);
	double sw = MAGNIFIER_SWATCH * S;
	double bh = ext.height + MAGNIFIER_BOX_PAD_Y * S;
	double bw = ext.width + sw + MAGNIFIER_BOX_PAD_X * S;
	double bx = lx + (L - bw) / 2.0;
	double by = ly + L + pad;
	if (by + bh > ph - pad) by = ly - bh - pad;
	bx = fmin(fmax(bx, pad), pw - bw - pad);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	grabit_cairo_rect_r(cr, bx, by, bw, bh, grabit_ui_radius(GUI_R_TIP) * S);
	cairo_fill(cr);
	grabit_cairo_set_source_argb(cr, c, 1.0);
	grabit_cairo_rect_r(cr, bx + MAGNIFIER_SWATCH_INSET * S, by + (bh - sw) / 2.0, sw, sw,
						grabit_ui_radius(GUI_R_GLYPH) * S);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, bx + sw + MAGNIFIER_TEXT_GAP * S,
				  by + (bh - ext.height) / 2.0 - ext.y_bearing);
	cairo_show_text(cr, info);
}
