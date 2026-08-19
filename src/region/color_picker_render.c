// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/color_picker_internal.h"
#include "region/toolbar_internal.h"
#include "region/wlr_input_state.h"
#include "region/wlr_state.h"
#include "util/util.h"

#include "cairo_util.h"

#include "wl/wl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>

#define COLOR_PICKER_W 220
#define COLOR_PICKER_GRID_H 130
#define COLOR_PICKER_INPUT_H 26
#define COLOR_PICKER_INPUT_GAP 5
#define COLOR_PICKER_INPUT_BTN_GAP 4
#define COLOR_PICKER_PAD 6
#define COLOR_PICKER_GAP 8
#define COLOR_PICKER_SWATCH_PAD_X 5
#define COLOR_PICKER_SWATCH_PAD_Y 4
#define COLOR_PICKER_TEXT_GAP 8

static void picker_patterns_build(struct ro_state *st, int32_t dw, int32_t dh) {
	cairo_pattern_t *rainbow = cairo_pattern_create_linear(0, 0, dw, 0);
	static const int N_HUE_STOPS = 6;
	for (int i = 0; i <= N_HUE_STOPS; i++) {
		double frac = (double)i / (double)N_HUE_STOPS;
		double rd, gd, bd;
		gcp_hsl_to_rgb(frac * 360.0, 1.0, 0.5, &rd, &gd, &bd);
		cairo_pattern_add_color_stop_rgb(rainbow, frac, rd, gd, bd);
	}
	double mid = dh * 0.5;
	cairo_pattern_t *top = cairo_pattern_create_linear(0, 0, 0, mid);
	cairo_pattern_add_color_stop_rgba(top, 0.0, 1, 1, 1, 1);
	cairo_pattern_add_color_stop_rgba(top, 1.0, 1, 1, 1, 0);
	cairo_pattern_t *bot = cairo_pattern_create_linear(0, mid, 0, dh);
	cairo_pattern_add_color_stop_rgba(bot, 0.0, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(bot, 1.0, 0, 0, 0, 1);
	st->picker_rainbow_pat = rainbow;
	st->picker_top_pat = top;
	st->picker_bot_pat = bot;
	st->picker_pat_dw = dw;
	st->picker_pat_dh = dh;
}

static void picker_patterns_destroy(struct ro_state *st) {
	if (st->picker_rainbow_pat) cairo_pattern_destroy(st->picker_rainbow_pat);
	if (st->picker_top_pat) cairo_pattern_destroy(st->picker_top_pat);
	if (st->picker_bot_pat) cairo_pattern_destroy(st->picker_bot_pat);
	st->picker_rainbow_pat = st->picker_top_pat = st->picker_bot_pat = NULL;
	st->picker_pat_dw = st->picker_pat_dh = 0;
}

void region_color_picker_release_cache(struct ro_state *st) {
	picker_patterns_destroy(st);
}

static void render_grid(cairo_t *cr, struct ro_state *st, int32_t S,
						double dx, double dy, double dw, double dh) {
	int32_t idw = (int32_t)dw, idh = (int32_t)dh;
	if (st->picker_pat_dw != idw || st->picker_pat_dh != idh) {
		picker_patterns_destroy(st);
		picker_patterns_build(st, idw, idh);
	}

	cairo_matrix_t m;
	cairo_matrix_init_translate(&m, -dx, -dy);
	cairo_pattern_set_matrix(st->picker_rainbow_pat, &m);
	cairo_set_source(cr, st->picker_rainbow_pat);
	cairo_rectangle(cr, dx, dy, dw, dh);
	cairo_fill(cr);

	cairo_pattern_set_matrix(st->picker_top_pat, &m);
	cairo_set_source(cr, st->picker_top_pat);
	cairo_rectangle(cr, dx, dy, dw, dh * 0.5);
	cairo_fill(cr);

	cairo_pattern_set_matrix(st->picker_bot_pat, &m);
	cairo_set_source(cr, st->picker_bot_pat);
	cairo_rectangle(cr, dx, dy + dh * 0.5, dw, dh * 0.5);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, dx + 0.5 * S, dy + 0.5 * S, dw - (double)S, dh - (double)S);
	cairo_stroke(cr);
}

static void render_input(cairo_t *cr, const struct ro_output *o, int32_t S) {
	int32_t ix, iy, iw, ih;
	region_color_input_rect(o->st, &ix, &iy, &iw, &ih);
	double dix = (double)(ix - o->go->x) * S;
	double diy = (double)(iy - o->go->y) * S;
	double diw = (double)iw * S;
	double dih = (double)ih * S;

	double r_input = o->st->rounded_ui ? 4.0 * S : 0.0;
	double r_input_stroke = o->st->rounded_ui ? 4.0 * S - 0.5 * S : 0.0;
	cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 1);
	grabit_cairo_rounded_rect(cr, dix, diy, diw, dih, r_input);
	cairo_fill(cr);
	if (o->st->color_input_active) {
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
	} else {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	}
	cairo_set_line_width(cr, (double)S);
	grabit_cairo_rounded_rect(cr, dix + 0.5 * S, diy + 0.5 * S,
							  diw - (double)S, dih - (double)S, r_input_stroke);
	cairo_stroke(cr);

	double sw = dih - 2.0 * COLOR_PICKER_SWATCH_PAD_Y * S;
	double sx = dix + (double)COLOR_PICKER_SWATCH_PAD_X * S;
	double sy = diy + (double)COLOR_PICKER_SWATCH_PAD_Y * S;
	double r_swatch = o->st->rounded_ui ? 3.0 * S : 0.0;
	double r_swatch_stroke = o->st->rounded_ui ? 3.0 * S - 0.5 * S : 0.0;
	uint32_t cur = region_active_color(o->st);
	grabit_cairo_set_source_argb(cr, cur, 1);
	grabit_cairo_rounded_rect(cr, sx, sy, sw, sw, r_swatch);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
	cairo_set_line_width(cr, (double)S);
	grabit_cairo_rounded_rect(cr, sx + 0.5 * S, sy + 0.5 * S,
							  sw - (double)S, sw - (double)S, r_swatch_stroke);
	cairo_stroke(cr);

	char text[16];
	if (o->st->color_input_active) {
		snprintf(text, sizeof text, "#%s", o->st->color_input_buf);
	} else {
		snprintf(text, sizeof text, "#%06X", cur & 0xFFFFFFu);
	}
	cairo_select_font_face(cr, "monospace",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0 * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);
	double text_x = sx + sw + (double)COLOR_PICKER_TEXT_GAP * S;
	double text_y = diy + dih / 2.0 - ext.height / 2.0 - ext.y_bearing;
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, text_x, text_y);
	cairo_show_text(cr, text);

	if (o->st->color_input_active) {
		double cx = text_x + ext.x_advance;
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
		cairo_set_line_width(cr, 1.5 * S);
		cairo_move_to(cr, cx, diy + 4.0 * S);
		cairo_line_to(cr, cx, diy + dih - 4.0 * S);
		cairo_stroke(cr);
	}
}

static void render_eyedropper_btn(cairo_t *cr, const struct ro_output *o, int32_t S) {
	int32_t ex, ey, ew, eh;
	region_color_eyedropper_rect(o->st, &ex, &ey, &ew, &eh);
	double dex = (double)(ex - o->go->x) * S;
	double dey = (double)(ey - o->go->y) * S;
	double dew = (double)ew * S;
	double deh = (double)eh * S;
	if (o->st->eyedropper_mode) {
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 0.92);
	} else {
		cairo_set_source_rgba(cr, 0.18, 0.18, 0.18, 1);
	}
	double r_btn = o->st->rounded_ui ? 4.0 * S : 0.0;
	double r_btn_stroke = o->st->rounded_ui ? 4.0 * S - 0.5 * S : 0.0;
	grabit_cairo_rounded_rect(cr, dex, dey, dew, deh, r_btn);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	cairo_set_line_width(cr, (double)S);
	grabit_cairo_rounded_rect(cr, dex + 0.5 * S, dey + 0.5 * S,
							  dew - (double)S, deh - (double)S, r_btn_stroke);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	toolbar_icon_color_picker(cr, dex + dew / 2.0, dey + deh / 2.0, deh * 0.7);
}

void region_color_picker_render(cairo_t *cr, const struct ro_output *o) {
	if (!o->st->color_picker_open) return;
	int32_t px, py, pw, ph;
	region_color_picker_rect(o->st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return;
	if (!grabit_output_overlaps(o->go, (struct rect){px, py, pw, ph})) return;

	int32_t S = o->scale;
	double dx = (double)(px - o->go->x) * S;
	double dy = (double)(py - o->go->y) * S;
	double dw = (double)pw * S;
	double dh = (double)ph * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	double bg_x = dx - (double)COLOR_PICKER_PAD * S;
	double bg_y = dy - (double)COLOR_PICKER_PAD * S;
	double bg_w = dw + 2.0 * COLOR_PICKER_PAD * S;
	double bg_h = dh + (COLOR_PICKER_INPUT_GAP + COLOR_PICKER_INPUT_H +
						2 * COLOR_PICKER_PAD) *
						   S;
	double r_pop = o->st->rounded_ui ? 8.0 * S : 0.0;
	double r_stroke = o->st->rounded_ui ? 8.0 * S - 0.5 * S : 0.0;
	cairo_set_source_rgba(cr, 0.06, 0.06, 0.06, 0.96);
	grabit_cairo_rounded_rect(cr, bg_x, bg_y, bg_w, bg_h, r_pop);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, (double)S);
	grabit_cairo_rounded_rect(cr, bg_x + 0.5 * S, bg_y + 0.5 * S,
							  bg_w - (double)S, bg_h - (double)S, r_stroke);
	cairo_stroke(cr);

	render_grid(cr, o->st, S, dx, dy, dw, dh);
	render_input(cr, o, S);
	render_eyedropper_btn(cr, o, S);

	cairo_restore(cr);
}
