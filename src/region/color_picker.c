// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/color_picker_internal.h"
#include "region/toolbar_internal.h"
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

static int32_t color_picker_total_h(void) {
	return COLOR_PICKER_GRID_H + COLOR_PICKER_INPUT_GAP + COLOR_PICKER_INPUT_H;
}

void region_color_picker_rect(const struct ro_state *st,
							  int32_t *out_x, int32_t *out_y,
							  int32_t *out_w, int32_t *out_h) {
	if (!region_toolbar_popup_pos(st, TB_COLOR_CURRENT, COLOR_PICKER_W,
								  color_picker_total_h(), COLOR_PICKER_GAP,
								  out_x, out_y)) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_w = COLOR_PICKER_W;
	*out_h = COLOR_PICKER_GRID_H;
}

void region_color_input_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h) {
	int32_t gx, gy, gw, gh;
	region_color_picker_rect(st, &gx, &gy, &gw, &gh);
	if (gw <= 0) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_x = gx;
	*out_y = gy + gh + COLOR_PICKER_INPUT_GAP;
	*out_w = gw - COLOR_PICKER_INPUT_H - COLOR_PICKER_INPUT_BTN_GAP;
	*out_h = COLOR_PICKER_INPUT_H;
}

void region_color_eyedropper_rect(const struct ro_state *st,
								  int32_t *out_x, int32_t *out_y,
								  int32_t *out_w, int32_t *out_h) {
	int32_t gx, gy, gw, gh;
	region_color_picker_rect(st, &gx, &gy, &gw, &gh);
	if (gw <= 0) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_x = gx + gw - COLOR_PICKER_INPUT_H;
	*out_y = gy + gh + COLOR_PICKER_INPUT_GAP;
	*out_w = COLOR_PICKER_INPUT_H;
	*out_h = COLOR_PICKER_INPUT_H;
}

bool region_parse_hex_color(const char *s, uint32_t *out) {
	return grabit_parse_hex_color(s, out);
}

void gcp_hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b) {
	if (s <= 0.0) {
		*r = *g = *b = l;
		return;
	}
	double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
	double p = 2.0 * l - q;
	double hk = h / 360.0;
	double t[3] = {hk + 1.0 / 3.0, hk, hk - 1.0 / 3.0};
	double *out[3] = {r, g, b};
	for (int i = 0; i < 3; i++) {
		double tc = t[i];
		if (tc < 0) tc += 1.0;
		if (tc > 1) tc -= 1.0;
		double v;
		if (tc < 1.0 / 6.0)
			v = p + (q - p) * 6.0 * tc;
		else if (tc < 0.5)
			v = q;
		else if (tc < 2.0 / 3.0)
			v = p + (q - p) * (2.0 / 3.0 - tc) * 6.0;
		else
			v = p;
		*out[i] = v;
	}
}

bool region_color_picker_pick(const struct ro_state *st, int32_t abs_x, int32_t abs_y,
							  uint32_t *out_color) {
	int32_t px, py, pw, ph;
	region_color_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return false;
	int32_t lx = abs_x - px;
	int32_t ly = abs_y - py;
	if (lx < 0) lx = 0;
	if (lx >= pw) lx = pw - 1;
	if (ly < 0) ly = 0;
	if (ly >= ph) ly = ph - 1;
	double h = (double)lx / (double)pw * 360.0;
	double l = 1.0 - (double)ly / (double)(ph - 1);
	double rd, gd, bd;
	gcp_hsl_to_rgb(h, 1.0, l, &rd, &gd, &bd);
	uint32_t r = (uint32_t)(rd * 255.0 + 0.5);
	uint32_t g = (uint32_t)(gd * 255.0 + 0.5);
	uint32_t b = (uint32_t)(bd * 255.0 + 0.5);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	*out_color = (r << 16) | (g << 8) | b;
	return true;
}
