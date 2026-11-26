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

#define DWELL_MS 350

const char *const GRID_TYPE_LABELS[GT_COUNT] = {"All", "Uploads", "Saved", "Recordings"};
const char *const GRID_DATE_LABELS[GD_COUNT] = {"Any time", "Today", "Past 7 days",
												"Past 30 days", "This year"};
const char *const GRID_SIZE_LABELS[GS_COUNT] = {"Any size", "Under 1 MB", "1 to 10 MB",
												"Over 10 MB"};

void grid_draw_image(cairo_t *cr, cairo_surface_t *s, double x, double y, double w, double h,
					 double rad, bool cover) {
	int iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
	double sx = w / iw, sy = h / ih;
	double sc = cover ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
	double dw = iw * sc, dh = ih * sc;
	cairo_save(cr);
	if (cover)
		grabit_cairo_rect_r(cr, x, y, w, h, rad);
	else
		grabit_cairo_rect_r(cr, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, rad);
	cairo_clip(cr);
	cairo_translate(cr, x + (w - dw) / 2, y + (h - dh) / 2);
	cairo_scale(cr, sc, sc);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);
}

void grid_centered(cairo_t *cr, double x, double cy, double w, const char *text, double size,
				   double alpha) {
	double tw = ui_text_width(cr, text, size, CAIRO_FONT_WEIGHT_NORMAL);
	if (tw > w) tw = w;
	ui_draw_text_fit(cr, x + (w - tw) / 2, cy - size * 0.7, w, text, size, CAIRO_FONT_WEIGHT_NORMAL,
					 alpha);
}

void grid_play(cairo_t *cr, double cx, double cy, double size, double alpha) {
	cairo_set_source_rgba(cr, GUI_C_TEXT, alpha);
	cairo_move_to(cr, cx - size * 0.4, cy - size / 2);
	cairo_line_to(cr, cx + size * 0.5, cy);
	cairo_line_to(cr, cx - size * 0.4, cy + size / 2);
	cairo_close_path(cr);
	cairo_fill(cr);
}

size_t grid_wrap(cairo_t *cr, const char *text, double w, double size, char lines[][GRID_WRAP_W],
				 size_t max) {
	ui_set_font(cr, size, CAIRO_FONT_WEIGHT_NORMAL);
	size_t n = 0;
	const char *p = text ? text : "";
	while (*p && n < max) {
		size_t len = strlen(p), best = 0;
		if (len > GRID_WRAP_W - 8) len = GRID_WRAP_W - 8;
		char buf[GRID_WRAP_W];
		for (size_t j = 1; j <= len; j++) {
			if (grabit_utf8_valid_prefix(p, j) != j) continue;
			memcpy(buf, p, j);
			buf[j] = '\0';
			cairo_text_extents_t ext;
			cairo_text_extents(cr, buf, &ext);
			if (ext.x_advance > w) break;
			best = j;
		}
		if (best == 0) best = grabit_utf8_valid_prefix(p, 4) ? grabit_utf8_valid_prefix(p, 4) : 1;
		bool cut = n + 1 == max && p[best];
		if (cut && best > 3) best = grabit_utf8_prev(p, grabit_utf8_prev(p, best));
		memcpy(lines[n], p, best);
		lines[n][best] = '\0';
		if (cut) strcat(lines[n], "\xe2\x80\xa6");
		n++;
		p += best;
	}
	return n;
}

void grid_focus_ring(cairo_t *cr, struct set_rect r, bool round) {
	grabit_ui_accent(cr, 0.9);
	cairo_set_line_width(cr, 2.0);
	if (round) {
		cairo_new_sub_path(cr);
		cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2 + 3, 0, 6.283185307);
	} else {
		grabit_cairo_rect_r(cr, r.x - 3, r.y - 3, r.w + 6, r.h + 6, grabit_ui_radius(GUI_R_BTN) + 3);
	}
	cairo_stroke(cr);
}

void grid_size_text(int64_t bytes, char *out, size_t n) {
	if (bytes < 1024)
		snprintf(out, n, "%lld B", (long long)bytes);
	else if (bytes < 1024 * 1024)
		snprintf(out, n, "%.0f KB", bytes / 1024.0);
	else
		snprintf(out, n, "%.1f MB", bytes / (1024.0 * 1024.0));
}

size_t grid_order_pos(const struct set_hist_grid *g, size_t entry) {
	for (size_t i = 0; i < g->n_items; i++)
		if (g->items[i].entry == entry) return i;
	return SET_NONE;
}

int grid_want_px(const struct settings_state *st) {
	double scale = st->win && st->win->scale120 > 0 ? st->win->scale120 / 120.0 : 1.0;
	return (int)(GRID_TILE * (scale > 1 ? scale : 1) + 0.5);
}

bool grid_filtered(const struct set_hist_grid *g) {
	return g->query.len || g->type != GT_ALL || g->service[0] || g->date != GD_ANY ||
		   g->size != GS_ANY;
}

void set_hist_grid_free(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	if (!g) return;
	thumbs_release(g->th);
	viewer_free(g);
	if (g->big) cairo_surface_destroy(g->big);
	free(g->big_id);
	free(g->ffmpeg);
	free(g->sizes);
	free(g->items);
	free(g->heads);
	free(g);
	st->grid = NULL;
	grid_threads_wait(1500);
}

void set_hist_grid_sync(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	if (!g) {
		g = calloc(1, sizeof *g);
		if (!g) return;
		g->hover = g->sel = g->view = g->bar_hover = g->focus_stop = SET_NONE;
		g->view_hover = g->view_press = g->view_focus = SET_NONE;
		st->grid = g;
	}
	if (st->popup == POPUP_ACTIONS) {
		st->popup = POPUP_NONE;
		free(st->popup_arg);
		st->popup_arg = NULL;
		g->tile_menu = false;
	}
	struct grid_thumbs *old = g->th;
	free(g->ffmpeg);
	g->ffmpeg = media_ffmpeg(&st->cfg);
	g->th = thumbs_new(&st->hist, old, grid_want_px(st), g->ffmpeg);
	thumbs_release(old);
	thumbs_start(g->th);

	int64_t *sizes = realloc(g->sizes, (st->hist.n ? st->hist.n : 1) * sizeof *sizes);
	if (sizes) {
		g->sizes = sizes;
		g->n_sizes = st->hist.n;
		for (size_t i = 0; i < st->hist.n; i++) {
			struct stat sb;
			const char *p = st->hist.v[i].path;
			sizes[i] = p && stat(p, &sb) == 0 ? (int64_t)sb.st_size : -1;
		}
	}
	g->view = SET_NONE;
	for (size_t i = 0; g->view_id && i < st->hist.n; i++)
		if (strcmp(st->hist.v[i].id, g->view_id) == 0) g->view = i;
	if (g->view == SET_NONE) viewer_free(g);
	g->hover = g->sel = SET_NONE;
	g->dirty = true;
	if (st->win) {
		set_arm_timer(st);
		ui_window_damage(st->win);
	}
}

static bool words_match(const char *query, const struct history_entry *e) {
	char q[UI_TEXT_MAX];
	snprintf(q, sizeof q, "%s", query);
	char *save = NULL;
	for (char *w = strtok_r(q, " \t", &save); w; w = strtok_r(NULL, " \t", &save)) {
		const char *name = set_hist_name(e);
		bool hit = (name && strcasestr(name, w)) || (e->url && strcasestr(e->url, w)) ||
				   (e->path && strcasestr(e->path, w)) || (e->service && strcasestr(e->service, w));
		if (!hit) return false;
	}
	return true;
}

static time_t midnight(time_t now, int days_back) {
	struct tm tm;
	localtime_r(&now, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_mday -= days_back;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

static bool keep(const struct set_hist_grid *g, size_t i, const struct history_entry *e, time_t now) {
	switch (g->type) {
	case GT_UPLOADS:
		if (e->kind != HISTORY_UPLOAD) return false;
		break;
	case GT_SAVED:
		if (e->kind != HISTORY_FILE || e->video) return false;
		break;
	case GT_RECORDINGS:
		if (!e->video) return false;
		break;
	default:
		break;
	}
	if (g->service[0] && (!e->service || strcmp(e->service, g->service) != 0)) return false;
	time_t since = 0;
	if (g->date == GD_TODAY) since = midnight(now, 0);
	if (g->date == GD_WEEK) since = now - 7 * 86400;
	if (g->date == GD_MONTH) since = now - 30 * 86400;
	if (g->date == GD_YEAR) {
		struct tm tm;
		localtime_r(&now, &tm);
		tm.tm_mon = 0;
		tm.tm_mday = 1;
		tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
		tm.tm_isdst = -1;
		since = mktime(&tm);
	}
	if (since && (time_t)e->time < since) return false;
	if (g->size != GS_ANY) {
		int64_t b = i < g->n_sizes ? g->sizes[i] : -1, mb = 1024 * 1024;
		if (b < 0) return false;
		if (g->size == GS_SMALL && b >= mb) return false;
		if (g->size == GS_MEDIUM && (b < mb || b > 10 * mb)) return false;
		if (g->size == GS_LARGE && b <= 10 * mb) return false;
	}
	return !g->query.len || words_match(g->query.buf, e);
}

static void group_label(time_t t, time_t now, char *out, size_t n) {
	time_t today = midnight(now, 0);
	if (t >= today)
		snprintf(out, n, "Today");
	else if (t >= midnight(now, 1))
		snprintf(out, n, "Yesterday");
	else if (t >= midnight(now, 6))
		snprintf(out, n, "Past week");
	else {
		struct tm tm;
		localtime_r(&t, &tm);
		strftime(out, n, "%B %Y", &tm);
	}
}

static size_t cols(double w) {
	return w < GRID_TILE * 2 + GRID_GAP ? 1 : (size_t)((w + GRID_GAP) / (GRID_TILE + GRID_GAP));
}

const char *grid_service_label(const struct set_hist_grid *g) {
	return g->service[0] ? g->service : "All services";
}

static void place(cairo_t *cr, struct set_hist_grid *g, size_t stop, const char *label, double pad,
				  double w, double *x, double *y) {
	double bw = ui_text_width(cr, label, 12.5, CAIRO_FONT_WEIGHT_NORMAL) + pad;
	if (*x > SET_BODY_PAD && *x + bw > SET_BODY_PAD + w) {
		*x = SET_BODY_PAD;
		*y += GRID_CHIP_H + 8;
	}
	g->stops[stop] = (struct set_rect){*x, *y, bw, GRID_CHIP_H};
	*x += bw + 6;
}

static void layout_bar(struct set_hist_grid *g, double w) {
	cairo_t *cr = ui_measure_cr();
	for (size_t i = 0; i < GF_COUNT; i++)
		g->stops[i] = (struct set_rect){0};
	g->stops[GF_SEARCH] = (struct set_rect){SET_BODY_PAD, 0, w, GRID_FIELD_H};
	double x = SET_BODY_PAD, y = GRID_FIELD_H + 10;
	for (int t = 0; t < GT_COUNT; t++)
		place(cr, g, GF_TYPE + (size_t)t, GRID_TYPE_LABELS[t], 24, w, &x, &y);
	x += 10;
	place(cr, g, GF_SERVICE, grid_service_label(g), 38, w, &x, &y);
	place(cr, g, GF_DATE, GRID_DATE_LABELS[g->date], 38, w, &x, &y);
	place(cr, g, GF_SIZE, GRID_SIZE_LABELS[g->size], 38, w, &x, &y);
	if (grid_filtered(g)) place(cr, g, GF_CLEAR, "Clear filters", 24, w, &x, &y);
	g->bar_h = y + GRID_CHIP_H + 16;
}

void grid_relayout(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	double w = set_body_w(st) - SET_BODY_PAD * 2;
	if (!g || (!g->dirty && g->laid_w == w)) return;
	g->dirty = false;
	g->laid_w = w;
	layout_bar(g, w);
	if (st->hist.n == 0) {
		g->bar_h = 0;
		g->typing = false;
		g->focus_stop = SET_NONE;
		for (size_t i = 0; i < GF_COUNT; i++)
			g->stops[i] = (struct set_rect){0};
	}
	free(g->items);
	free(g->heads);
	g->items = NULL;
	g->heads = NULL;
	g->n_items = g->n_heads = 0;
	size_t n = st->hist.n;
	g->items = calloc(n ? n : 1, sizeof *g->items);
	g->heads = calloc(n ? n : 1, sizeof *g->heads);
	if (!g->items || !g->heads) return;
	time_t now = time(NULL);
	size_t c = cols(w), col = 0;
	double y = 0;
	char last[48] = "";
	for (size_t i = 0; i < n; i++) {
		const struct history_entry *e = &st->hist.v[i];
		if (!keep(g, i, e, now)) continue;
		char label[48];
		group_label((time_t)e->time, now, label, sizeof label);
		if (strcmp(label, last) != 0) {
			if (col) {
				y += GRID_ROW_STEP;
				col = 0;
			}
			struct grid_head *h = &g->heads[g->n_heads++];
			h->y = y;
			snprintf(h->label, sizeof h->label, "%s", label);
			snprintf(last, sizeof last, "%s", label);
			y += GRID_HEAD_H;
		}
		g->items[g->n_items++] =
			(struct grid_item){SET_BODY_PAD + (double)col * (GRID_TILE + GRID_GAP), y, i};
		if (++col == c) {
			col = 0;
			y += GRID_ROW_STEP;
		}
	}
	if (col) y += GRID_ROW_STEP;
	g->body_h = g->n_items ? y : 72;
	if (g->sel != SET_NONE && grid_order_pos(g, g->sel) == SET_NONE) g->sel = SET_NONE;
}

double set_hist_grid_h(struct settings_state *st) {
	grid_relayout(st);
	struct set_hist_grid *g = st->grid;
	return g ? g->bar_h + g->body_h : 64;
}

double grid_items_top(const struct set_hist_grid *g, const struct set_row *r) {
	return r->y + g->bar_h;
}

struct set_rect grid_tile_rect(const struct set_hist_grid *g, const struct set_row *r, size_t pos) {
	return (struct set_rect){g->items[pos].x, grid_items_top(g, r) + g->items[pos].y, GRID_TILE,
							 GRID_TILE};
}

double grid_bar_top(const struct settings_state *st, const struct set_row *r) {
	const struct set_hist_grid *g = st->grid;
	double y = st->body.offset > r->y ? st->body.offset : r->y;
	double max = r->y + r->h - g->bar_h;
	return y > max ? (max > r->y ? max : r->y) : y;
}

size_t grid_tile_at(struct settings_state *st, const struct set_row *r, double bx, double by) {
	struct set_hist_grid *g = st->grid;
	for (size_t i = 0; g && i < g->n_items; i++) {
		struct set_rect t = grid_tile_rect(g, r, i);
		t.h += GRID_CAPTION;
		if (set_rect_contains(&t, bx, by)) return i;
	}
	return SET_NONE;
}

size_t grid_bar_at(struct settings_state *st, const struct set_row *r, double bx, double by) {
	struct set_hist_grid *g = st->grid;
	if (!g || g->bar_h <= 0) return SET_NONE;
	double top = grid_bar_top(st, r);
	if (by < top || by >= top + g->bar_h) return SET_NONE;
	for (size_t i = 0; i < GF_COUNT; i++) {
		struct set_rect s = g->stops[i];
		s.y += top;
		if (s.w > 0 && set_rect_contains(&s, bx, by)) return i;
	}
	return GF_COUNT;
}

const struct set_row *grid_row(struct settings_state *st, size_t *index) {
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; i < p->n; i++)
		if (p->rows[i]->grid) {
			if (index) *index = i;
			return p->rows[i];
		}
	return NULL;
}

bool grid_preview_due(const struct settings_state *st) {
	const struct set_hist_grid *g = st->grid;
	return g && g->hover != SET_NONE && st->popup == POPUP_NONE && !g->view_id &&
		   grabit_now_ms() - g->hover_at >= DWELL_MS;
}

bool set_hist_typing(const struct settings_state *st) {
	return st->grid && st->grid->typing;
}

bool set_hist_busy(const struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	if (!g) return false;
	return thumbs_busy(g->th) || viewer_busy(st) ||
		   (g->hover != SET_NONE && !g->previewed && st->popup == POPUP_NONE);
}

bool set_hist_poll_thumbs(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	if (!g) return false;
	bool redraw = thumbs_poll(g->th) && st->page == st->history_page;
	if (!g->previewed && grid_preview_due(st)) redraw = true;
	if (viewer_poll(st)) redraw = true;
	return redraw;
}
