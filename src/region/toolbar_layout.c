// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "wl/wl.h"

#include <stdbool.h>
#include <stddef.h>

const uint32_t TOOLBAR_COLORS[6] = {
	0xff3030u,
	0xfff030u,
	0x40ff40u,
	0x4080ffu,
	0x000000u,
	0xffffffu,
};

static int toolbar_row_of(enum tb_action act) {
	if (act >= TB_COLOR_RED && act <= TB_CANCEL) return 1;
	return 0;
}

static int32_t toolbar_btn_w(enum tb_action act) {
	if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE) return 26;
	if (act == TB_COLOR_CURRENT) return 28;
	if (act == TB_WIDTH_SLIDER) return TB_SLIDER_W;
	return TB_BTN_W;
}

static int32_t toolbar_btn_h(enum tb_action act) {
	return toolbar_row_of(act) == 1 ? TB_BTN_H_OPT : TB_BTN_H;
}

static int32_t section_gap_before(enum tb_action act) {
	if (act == TB_TOOL_PEN) return 12;
	if (act == TB_WIDTH_SLIDER) return 2;
	if (act == TB_UNDO) return 10;
	if (act == TB_SAVE) return 12;
	return 2;
}

static int32_t row_total_w(int row) {
	int32_t x = 0;
	bool first = true;
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action a = (enum tb_action)i;
		if (toolbar_row_of(a) != row) continue;
		if (!first) x += section_gap_before(a);
		x += toolbar_btn_w(a);
		first = false;
	}
	return x;
}

static int32_t btn_x_in_row(enum tb_action act) {
	int row = toolbar_row_of(act);
	int32_t x = 0;
	bool first = true;
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action a = (enum tb_action)i;
		if (toolbar_row_of(a) != row) continue;
		if (!first) x += section_gap_before(a);
		if (a == act) return x;
		x += toolbar_btn_w(a);
		first = false;
	}
	return x;
}

static int32_t row_y_offset(int row) {
	if (row == 1) return TB_PAD + TB_BTN_H + TB_ROW_GAP;
	return TB_PAD;
}

static int32_t tb_total_w(void) {
	int32_t r0 = row_total_w(0);
	int32_t r1 = row_total_w(1);
	int32_t mx = r0 > r1 ? r0 : r1;
	return mx + TB_PAD * 2;
}

static int32_t tb_total_h(void) {
	return TB_PAD + TB_BTN_H + TB_ROW_GAP + TB_BTN_H_OPT + TB_PAD;
}

void toolbar_btn_rect_local(enum tb_action act, int32_t tw,
							int32_t *out_x, int32_t *out_y,
							int32_t *out_w, int32_t *out_h) {
	int row = toolbar_row_of(act);
	int32_t row_w = row_total_w(row);
	int32_t row_x0 = (tw - row_w) / 2;
	*out_x = row_x0 + btn_x_in_row(act);
	*out_y = row_y_offset(row);
	*out_w = toolbar_btn_w(act);
	*out_h = toolbar_btn_h(act);
}

static const struct grabit_output *toolbar_output(const struct ro_state *st) {
	if (st->tb_out) return st->tb_out;
	return grabit_wl_primary_output(st->wls);
}

void region_toolbar_rect(const struct ro_state *st,
						 const struct grabit_output **out_o,
						 int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
	const struct grabit_output *o = toolbar_output(st);
	if (out_o) *out_o = o;
	if (!o) {
		*x = *y = *w = *h = 0;
		return;
	}
	int32_t tw = tb_total_w();
	int32_t th = tb_total_h();
	*w = tw;
	*h = th;

	if (st->tb_moved) {
		struct rect b = st->bounds;
		if (st->tb_lock) grabit_output_rect(st->tb_lock, &b);
		struct rect r = rect_clamp_into((struct rect){st->tb_x, st->tb_y, tw, th}, b);
		*x = r.x;
		*y = r.y;
		return;
	}

	*x = o->x + (o->logical_width - tw) / 2;
	*y = o->y + TB_GAP;
}

void region_toolbar_slider_rect(const struct ro_state *st,
								int32_t *out_x, int32_t *out_y,
								int32_t *out_w, int32_t *out_h) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	int32_t bx, by, bw, bh;
	toolbar_btn_rect_local(TB_WIDTH_SLIDER, tw, &bx, &by, &bw, &bh);
	*out_x = tx + bx + 10;
	*out_y = ty + by;
	*out_w = bw - 20;
	*out_h = bh;
}

bool region_toolbar_contains(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) return false;
	return rect_contains((struct rect){tx, ty, tw, th}, abs_x, abs_y);
}

static int32_t btn_rect_cache[TB_BTN_COUNT][4];
static int32_t btn_rect_cache_tw = -1;

static void btn_rect_cache_build(int32_t tw) {
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		toolbar_btn_rect_local((enum tb_action)i, tw,
							   &btn_rect_cache[i][0], &btn_rect_cache[i][1],
							   &btn_rect_cache[i][2], &btn_rect_cache[i][3]);
	}
	btn_rect_cache_tw = tw;
}

enum tb_action region_toolbar_hit(const struct ro_state *st,
								  int32_t abs_x, int32_t abs_y) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) return TB_NONE;
	int32_t local_x = abs_x - tx;
	int32_t local_y = abs_y - ty;
	if (tw != btn_rect_cache_tw) btn_rect_cache_build(tw);
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		struct rect br = {btn_rect_cache[i][0], btn_rect_cache[i][1],
						  btn_rect_cache[i][2], btn_rect_cache[i][3]};
		if (rect_contains(br, local_x, local_y)) return (enum tb_action)i;
	}
	return TB_NONE;
}
