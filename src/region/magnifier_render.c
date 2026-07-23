// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "cairo_util.h"
#include "capture/capture.h"
#include "region/input_internal.h"
#include "region/ui.h"
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

bool region_magnifier_active(const struct ro_output *o) {
	return o->st->alt_held && o->st->cursor_on == o && o->cairo_frozen &&
		   !o->st->text_input_active;
}

bool region_coords_active(const struct ro_output *o) {
	return o->st->show_coords && o->st->cursor_on == o &&
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
	cairo_rectangle(cr, bx, by, bw, bh);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, bx + (bw - ext.width) / 2.0 - ext.x_bearing,
				  by + (bh - ext.height) / 2.0 - ext.y_bearing);
	cairo_show_text(cr, info);
}

void region_magnifier_render(cairo_t *cr, const struct ro_output *o) {
	if (!o->st->frozen) return;
	const struct image *img = &o->st->frozen[o->idx];
	if (!img->bytes || img->stride <= 0) return;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	int32_t px = (o->st->cursor_x - o->go->x) * S;
	int32_t py = (o->st->cursor_y - o->go->y) * S;
	if (px < 0 || py < 0 || px >= img->width || py >= img->height) return;

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

	cairo_save(cr);
	cairo_rectangle(cr, lx, ly, L, L);
	cairo_clip(cr);
	cairo_save(cr);
	cairo_translate(cr, lx + L / 2.0, ly + L / 2.0);
	cairo_scale(cr, cell, cell);
	cairo_translate(cr, -((double)px + 0.5), -((double)py + 0.5));
	cairo_set_source_surface(cr, o->cairo_frozen, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
	cairo_paint(cr);
	cairo_restore(cr);

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
	cairo_rectangle(cr, lx, ly, L, L);
	cairo_stroke(cr);

	uint32_t c = 0;
	ginp_eyedropper_sample(o->st, &c);
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
	cairo_rectangle(cr, bx, by, bw, bh);
	cairo_fill(cr);
	grabit_cairo_set_source_argb(cr, c, 1.0);
	cairo_rectangle(cr, bx + MAGNIFIER_SWATCH_INSET * S, by + (bh - sw) / 2.0, sw, sw);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, bx + sw + MAGNIFIER_TEXT_GAP * S,
				  by + (bh - ext.height) / 2.0 - ext.y_bearing);
	cairo_show_text(cr, info);
}
