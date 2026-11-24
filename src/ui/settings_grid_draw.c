// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_grid.h"

#include "cairo_util.h"
#include "config/config.h"
#include "ui/widgets.h"
#include "ui/window.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define PREVIEW_W 380.0
#define PREVIEW_H 420.0
#define PREVIEW_MIN_W 260.0
#define PREVIEW_PX 760

static void badge(cairo_t *cr, double x, double y, const char *text, bool right, bool video) {
	double lead = video ? 12 : 0;
	double tw = ui_text_width(cr, text, 10.5, CAIRO_FONT_WEIGHT_BOLD) + 12 + lead;
	double bx = right ? x - tw : x;
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.7);
	grabit_cairo_rect_r(cr, bx, y, tw, 18, 9);
	cairo_fill(cr);
	if (video) grid_play(cr, bx + 10, y + 9, 8, 0.95);
	ui_draw_text_fit(cr, bx + 6 + lead, y + 2, tw, text, 10.5, CAIRO_FONT_WEIGHT_BOLD, 0.95);
}

static void draw_tile(struct settings_state *st, cairo_t *cr, size_t pos, struct set_rect t,
					  bool focused) {
	struct set_hist_grid *g = st->grid;
	size_t i = g->items[pos].entry;
	const struct history_entry *e = &st->hist.v[i];
	bool hot = i == g->hover || (st->popup == POPUP_ACTIONS && g->tile_menu && i == g->sel);
	bool ring = focused && g->focus_stop == SET_NONE && i == g->sel;
	double rad = grabit_ui_radius(GUI_R_BTN);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.05);
	grabit_cairo_rect_r(cr, t.x, t.y, t.w, t.h, rad);
	cairo_fill(cr);
	cairo_surface_t *s = thumbs_get(g->th, i);
	bool file = history_has_file(e);
	if (s)
		grid_draw_image(cr, s, t.x, t.y, t.w, t.h, rad, true);
	else if (thumbs_pending(g->th, i))
		grid_centered(cr, t.x, t.y + t.h / 2, t.w, "\xe2\x80\xa6", 16, 0.35);
	else if (e->video)
		grid_play(cr, t.x + t.w / 2, t.y + t.h / 2, 34, file || e->kind == HISTORY_UPLOAD ? 0.55 : 0.25);
	else if (!file && e->kind == HISTORY_UPLOAD)
		grid_centered(cr, t.x + 6, t.y + t.h / 2, t.w - 12, "no preview", 12, 0.45);
	else if (!file)
		grid_centered(cr, t.x + 6, t.y + t.h / 2, t.w - 12, "file is gone", 12, 0.45);
	if (e->video) badge(cr, t.x + 6, t.y + t.h - 24, "video", false, true);
	if (e->kind == HISTORY_UPLOAD)
		badge(cr, t.x + t.w - 6, t.y + 6, e->service ? e->service : "upload", true, false);
	if (hot) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.10);
		grabit_cairo_rect_r(cr, t.x, t.y, t.w, t.h, rad);
		cairo_fill(cr);
	}
	if (ring) {
		grabit_ui_accent(cr, 0.9);
		cairo_set_line_width(cr, 2.0);
		grabit_cairo_rect_r_inset(cr, t.x, t.y, t.w, t.h, rad, 2.0);
		cairo_stroke(cr);
	}
	char when[32];
	set_hist_when(e->time, when, sizeof when);
	ui_draw_text_fit(cr, t.x + 2, t.y + t.h + 5, t.w - 4, set_hist_name(e), 12.5,
					 CAIRO_FONT_WEIGHT_NORMAL, file || e->kind == HISTORY_UPLOAD ? 0.9 : 0.5);
	ui_draw_text_fit(cr, t.x + 2, t.y + t.h + 21, t.w - 4, when, 11, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
}

static void chip(cairo_t *cr, struct set_rect r, const char *label, bool on, bool hot) {
	if (on)
		grabit_ui_accent(cr, hot ? 0.34 : 0.26);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, hot ? 0.12 : 0.06);
	grabit_cairo_rect_r(cr, r.x, r.y, r.w, r.h, grabit_ui_radius(GUI_R_BTN));
	cairo_fill(cr);
	grid_centered(cr, r.x, r.y + r.h / 2 + 1, r.w, label, 12.5, on ? 1.0 : 0.85);
}

static void dropdown(cairo_t *cr, struct set_rect r, const char *label, bool on, bool hot) {
	chip(cr, (struct set_rect){r.x, r.y, r.w, r.h}, "", on, hot);
	ui_draw_text_fit(cr, r.x + 12, r.y + 6, r.w - 30, label, 12.5, CAIRO_FONT_WEIGHT_NORMAL,
					 on ? 1.0 : 0.85);
	cairo_set_source_rgba(cr, GUI_C_TEXT, 0.7);
	ui_chevron_draw(cr, r.x + r.w - 14, r.y + r.h / 2, 7, false);
}

static void draw_bar(struct settings_state *st, cairo_t *cr, const struct set_row *r, bool focused) {
	struct set_hist_grid *g = st->grid;
	double top = grid_bar_top(st, r), bw = set_body_w(st);
	if (top > r->y) {
		cairo_set_source_rgba(cr, GUI_C_CANVAS, 1.0);
		cairo_rectangle(cr, 0, top - 8, bw, g->bar_h + 8);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
		cairo_rectangle(cr, SET_BODY_PAD, top + g->bar_h - 1, bw - SET_BODY_PAD * 2, 1);
		cairo_fill(cr);
	}
	bool keys = focused && st->focus_visible;
	struct set_rect f = g->stops[GF_SEARCH];
	f.y += top;
	bool typing = focused && g->typing;
	char count[64];
	if (grid_filtered(g))
		snprintf(count, sizeof count, "%zu of %zu", g->n_items, st->hist.n);
	else
		snprintf(count, sizeof count, "%zu entr%s", st->hist.n, st->hist.n == 1 ? "y" : "ies");
	double cw = ui_text_width(cr, count, 12, CAIRO_FONT_WEIGHT_NORMAL);
	ui_text_draw(cr, typing ? &g->query : NULL, f.x, f.y, f.w, f.h, cw + 16, false, g->query.buf,
				 "Search history");
	ui_draw_text_fit(cr, f.x + f.w - cw - 10, f.y + 8, cw + 1, count, 12, CAIRO_FONT_WEIGHT_NORMAL,
					 0.5);
	for (size_t i = GF_TYPE; i < GF_COUNT; i++) {
		struct set_rect s = g->stops[i];
		if (s.w <= 0) continue;
		s.y += top;
		bool hot = g->bar_hover == i;
		if (i < GF_SERVICE)
			chip(cr, s, GRID_TYPE_LABELS[i - GF_TYPE], g->type == (enum grid_type)(i - GF_TYPE), hot);
		else if (i == GF_SERVICE)
			dropdown(cr, s, grid_service_label(g), g->service[0] != 0, hot);
		else if (i == GF_DATE)
			dropdown(cr, s, GRID_DATE_LABELS[g->date], g->date != GD_ANY, hot);
		else if (i == GF_SIZE)
			dropdown(cr, s, GRID_SIZE_LABELS[g->size], g->size != GS_ANY, hot);
		else
			chip(cr, s, "Clear filters", false, hot);
	}
	if (keys && g->focus_stop != SET_NONE && g->focus_stop != GF_SEARCH &&
		g->stops[g->focus_stop].w > 0) {
		struct set_rect s = g->stops[g->focus_stop];
		s.y += top;
		grid_focus_ring(cr, s, false);
	}
}

static void want_visible(struct settings_state *st, const struct set_row *r) {
	struct set_hist_grid *g = st->grid;
	double lo = st->body.offset - st->view_h * 0.5, hi = st->body.offset + st->view_h * 1.5;
	size_t *idx = malloc((g->n_items ? g->n_items : 1) * sizeof *idx), n = 0;
	if (!idx) return;
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < g->n_items; i++) {
			struct set_rect t = grid_tile_rect(g, r, i);
			bool seen = t.y + t.h >= st->body.offset && t.y <= st->body.offset + st->view_h;
			bool near = t.y + t.h >= lo && t.y <= hi;
			if (pass == 0 ? seen : near && !seen) idx[n++] = g->items[i].entry;
		}
	thumbs_want(g->th, idx, n);
	free(idx);
}

void set_hist_grid_draw(struct settings_state *st, cairo_t *cr, const struct set_row *r,
						bool focused) {
	struct set_hist_grid *g = st->grid;
	if (!g) return;
	if (thumbs_px(g->th) != grid_want_px(st)) set_hist_grid_sync(st);
	grid_relayout(st);
	double base = grid_items_top(g, r), bw = set_body_w(st);
	if (st->hist.n == 0 || g->n_items == 0) {
		const char *on = config_get(&st->cfg, "history.enabled");
		const char *text =
			st->hist.n ? "nothing matches; try another search or clear the filters"
			: on && strcmp(on, "false") == 0
				? "history is off; turn on Keep history on the General page to record screenshots and uploads"
				: "nothing recorded yet; screenshots and recordings you save or upload show up here";
		ui_draw_text_fit(cr, SET_BODY_PAD, base + 18, bw - SET_BODY_PAD * 2, text, 12,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	}
	double top = st->body.offset, bottom = top + st->view_h;
	for (size_t i = 0; i < g->n_heads; i++) {
		double y = base + g->heads[i].y;
		if (y + GRID_HEAD_H < top || y > bottom) continue;
		ui_draw_text_fit(cr, SET_BODY_PAD, y + 10, bw - SET_BODY_PAD * 2, g->heads[i].label, 12,
						 CAIRO_FONT_WEIGHT_BOLD, 0.6);
	}
	for (size_t i = 0; i < g->n_items; i++) {
		struct set_rect t = grid_tile_rect(g, r, i);
		if (t.y + t.h + GRID_CAPTION < top || t.y > bottom) continue;
		draw_tile(st, cr, i, t, focused);
	}
	want_visible(st, r);
	if (g->bar_h > 0) draw_bar(st, cr, r, focused);
}

static cairo_surface_t *big_for(struct set_hist_grid *g, size_t i, const struct history_entry *e) {
	if (g->big_id && strcmp(g->big_id, e->id) == 0) return g->big;
	if (thumbs_pending(g->th, i)) return NULL;
	if (g->big) cairo_surface_destroy(g->big);
	free(g->big_id);
	g->big_id = strdup(e->id);
	g->big = NULL;
	char cache[PATH_MAX];
	history_thumb_path(e->id, ".png", cache, sizeof cache);
	if (!e->video && grid_is_image(e->path) && history_has_file(e))
		g->big = grid_load_fit(e->path, PREVIEW_PX);
	else if (access(cache, R_OK) == 0)
		g->big = grid_load_fit(cache, PREVIEW_PX);
	return g->big;
}

void set_hist_overlay_draw(struct settings_state *st, cairo_t *cr) {
	struct set_hist_grid *g = st->grid;
	if (!grid_preview_due(st) || g->hover >= st->hist.n || st->page != st->history_page) return;
	const struct set_row *r = grid_row(st, NULL);
	size_t pos = grid_order_pos(g, g->hover);
	if (!r || pos == SET_NONE) return;
	g->previewed = true;
	const struct history_entry *e = &st->hist.v[g->hover];
	cairo_surface_t *img = big_for(g, g->hover, e);
	char when[32], where[512], size[32] = "", lines[4][600];
	size_t nl = 0;
	set_hist_when(e->time, when, sizeof when);
	set_hist_tilde(e->path, where, sizeof where);
	if (g->hover < g->n_sizes && g->sizes[g->hover] >= 0)
		grid_size_text(g->sizes[g->hover], size, sizeof size);
	snprintf(lines[nl++], sizeof lines[0], "%s%s%s%s%s", when, e->service ? " \xc2\xb7 " : "",
			 e->service ? e->service : "", size[0] ? " \xc2\xb7 " : "", size);
	if (e->url) snprintf(lines[nl++], sizeof lines[0], "%s", e->url);
	if (e->path)
		snprintf(lines[nl++], sizeof lines[0], "%s%s", where,
				 history_has_file(e) ? "" : " (file is gone)");
	if (history_host_keeps(e)) snprintf(lines[nl++], sizeof lines[0], "this host has no delete");

	double iw = 0, ih = 0;
	if (img) {
		double w = cairo_image_surface_get_width(img), h = cairo_image_surface_get_height(img);
		double sc = PREVIEW_W / w < PREVIEW_H / h ? PREVIEW_W / w : PREVIEW_H / h;
		iw = w * sc;
		ih = h * sc;
	}
	double pad = 10, pw = iw > PREVIEW_MIN_W ? iw : PREVIEW_MIN_W, tw = pw - pad * 2;
	char wrapped[8][GRID_WRAP_W];
	size_t nw = 0;
	for (size_t i = 0; i < nl && nw < 8; i++)
		nw += grid_wrap(cr, lines[i], tw, 11.5, wrapped + nw, nw + 2 <= 8 ? 2 : 8 - nw);
	double ph = (img ? ih : 0) + pad + 20 + (double)nw * 17 + pad;
	struct set_rect t = grid_tile_rect(g, r, pos);
	double bw = set_body_w(st);
	double x = t.x + t.w + 12;
	if (x + pw > bw - 8) x = t.x - 12 - pw;
	if (x < 8) x = 8;
	double top = st->body.offset + 8, bottom = st->body.offset + st->view_h - 8;
	double y = t.y;
	if (y + ph > bottom) y = bottom - ph;
	if (y < top) y = top;
	double rad = grabit_ui_radius(GUI_R_BTN);
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.35);
	grabit_cairo_rect_r(cr, x + 1, y + 3, pw, ph, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_SURFACE, 1.0);
	grabit_cairo_rect_r(cr, x, y, pw, ph, rad);
	cairo_fill(cr);
	double cy = y;
	if (img) {
		cairo_save(cr);
		grabit_cairo_rect_r(cr, x, y, pw, ph, rad);
		cairo_clip(cr);
		grid_draw_image(cr, img, x + (pw - iw) / 2, y, iw, ih, 0, false);
		cairo_restore(cr);
		cy += ih;
	}
	cairo_set_source_rgba(cr, GUI_C_INK, 0.14);
	cairo_set_line_width(cr, 1.0);
	grabit_cairo_rect_r_inset(cr, x, y, pw, ph, rad, 1.0);
	cairo_stroke(cr);
	cy += pad;
	ui_draw_text_fit(cr, x + pad, cy, tw, set_hist_name(e), 13, CAIRO_FONT_WEIGHT_BOLD, 0.95);
	cy += 20;
	for (size_t i = 0; i < nw; i++, cy += 17)
		ui_draw_text_fit(cr, x + pad, cy, tw, wrapped[i], 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.6);
}
