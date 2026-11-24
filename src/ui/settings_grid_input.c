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

static void changed(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	g->dirty = true;
	g->hover = SET_NONE;
	st->body.offset = 0;
	ui_window_damage(st->win);
}

static void pick_date(struct settings_state *st, const char *label) {
	for (int i = 0; i < GD_COUNT; i++)
		if (strcmp(label, GRID_DATE_LABELS[i]) == 0) st->grid->date = (enum grid_date)i;
	changed(st);
}

static void pick_size(struct settings_state *st, const char *label) {
	for (int i = 0; i < GS_COUNT; i++)
		if (strcmp(label, GRID_SIZE_LABELS[i]) == 0) st->grid->size = (enum grid_size)i;
	changed(st);
}

static void pick_service(struct settings_state *st, const char *label) {
	snprintf(st->grid->service, sizeof st->grid->service, "%s",
			 strcmp(label, "All services") == 0 ? "" : label);
	changed(st);
}

static bool seen_service(const struct settings_state *st, size_t upto, const char *s) {
	for (size_t i = 0; i < upto; i++)
		if (st->hist.v[i].service && strcmp(st->hist.v[i].service, s) == 0) return true;
	return false;
}

static void g_tile_menu_off(struct settings_state *st) {
	st->grid->tile_menu = false;
}

static void open_filter(struct settings_state *st, size_t stop, double bx, double by) {
	size_t row;
	if (!grid_row(st, &row)) return;
	set_open_actions(st, row, bx, by, NULL);
	g_tile_menu_off(st);
	if (stop == GF_DATE) {
		for (int i = 0; i < GD_COUNT; i++)
			set_add_action(st, GRID_DATE_LABELS[i], pick_date);
	} else if (stop == GF_SIZE) {
		for (int i = 0; i < GS_COUNT; i++)
			set_add_action(st, GRID_SIZE_LABELS[i], pick_size);
	} else {
		set_add_action(st, "All services", pick_service);
		size_t k = 0;
		for (size_t i = 0; i < st->hist.n && k + 1 < SET_MAX_ACTIONS; i++) {
			const char *s = st->hist.v[i].service;
			if (!s || seen_service(st, i, s)) continue;
			snprintf(st->grid->svc_labels[k], sizeof st->grid->svc_labels[k], "%s", s);
			set_add_action(st, st->grid->svc_labels[k++], pick_service);
		}
	}
}

static void clear_filters(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	ui_text_set(&g->query, "");
	g->type = GT_ALL;
	g->service[0] = '\0';
	g->date = GD_ANY;
	g->size = GS_ANY;
	changed(st);
}

static void activate_stop(struct settings_state *st, const struct set_row *r, size_t stop) {
	struct set_hist_grid *g = st->grid;
	if (stop >= GF_TYPE && stop < GF_SERVICE) {
		g->type = (enum grid_type)(stop - GF_TYPE);
		changed(st);
	} else if (stop == GF_SERVICE || stop == GF_DATE || stop == GF_SIZE) {
		struct set_rect s = g->stops[stop];
		open_filter(st, stop, s.x, grid_bar_top(st, r) + s.y + s.h + 4);
	} else if (stop == GF_CLEAR) {
		clear_filters(st);
		g->focus_stop = GF_SEARCH;
		g->typing = true;
	}
}

void set_hist_grid_press(struct settings_state *st, size_t row, double bx, double by,
						 uint32_t button) {
	struct set_hist_grid *g = st->grid;
	const struct set_row *r = set_current_page(st)->rows[row];
	if (!g) return;
	grid_relayout(st);
	size_t stop = grid_bar_at(st, r, bx, by);
	if (stop != SET_NONE) {
		g->typing = stop == GF_SEARCH;
		if (stop == GF_COUNT) {
			g->focus_stop = SET_NONE;
			return;
		}
		g->focus_stop = stop;
		if (stop == GF_SEARCH) {
			ui_text_click(&g->query, bx - g->stops[GF_SEARCH].x, false,
						  (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0);
			return;
		}
		if (button == BTN_LEFT) activate_stop(st, r, stop);
		return;
	}
	g->typing = false;
	size_t pos = grid_tile_at(st, r, bx, by);
	if (pos == SET_NONE) return;
	size_t i = g->items[pos].entry;
	g->sel = i;
	g->focus_stop = SET_NONE;
	g->hover = SET_NONE;
	g->previewed = false;
	if (button == BTN_RIGHT) {
		g->tile_menu = true;
		set_hist_open_menu(st, row, &st->hist.v[i], bx, by);
	} else {
		viewer_show(st, i);
	}
}

int set_hist_grid_motion(struct settings_state *st, const struct set_row *r, double bx, double by) {
	struct set_hist_grid *g = st->grid;
	if (!g) return 0;
	size_t stop = r && st->popup == POPUP_NONE ? grid_bar_at(st, r, bx, by) : SET_NONE;
	size_t pos = r && st->popup == POPUP_NONE && stop == SET_NONE ? grid_tile_at(st, r, bx, by) : SET_NONE;
	size_t i = pos == SET_NONE ? SET_NONE : g->items[pos].entry;
	size_t bh = stop == GF_COUNT ? SET_NONE : stop;
	if (bh != g->bar_hover) {
		g->bar_hover = bh;
		if (st->win) ui_window_damage(st->win);
	}
	if (i != g->hover) {
		g->hover = i;
		g->hover_at = grabit_now_ms();
		g->previewed = false;
		if (st->win) {
			ui_window_damage(st->win);
			set_arm_timer(st);
		}
	}
	if (stop == GF_SEARCH) return 2;
	return i != SET_NONE || bh != SET_NONE ? 1 : 0;
}

void set_hist_grid_key(struct settings_state *st, struct set_row *r) {
	(void)r;
	struct set_hist_grid *g = st->grid;
	if (!g || g->n_items == 0) return;
	if (grid_order_pos(g, g->sel) == SET_NONE) g->sel = g->items[0].entry;
	viewer_show(st, g->sel);
}

static void reveal(struct settings_state *st, const struct set_row *r) {
	struct set_hist_grid *g = st->grid;
	size_t pos = grid_order_pos(g, g->sel);
	if (pos == SET_NONE) return;
	struct set_rect t = grid_tile_rect(g, r, pos);
	ui_scroll_reveal(st->win, &st->body, t.y - g->bar_h - 8, t.h + GRID_CAPTION + g->bar_h + 16);
}

static size_t vertical(const struct set_hist_grid *g, size_t pos, int dir) {
	double y = g->items[pos].y, x = g->items[pos].x, row = -1;
	size_t best = SET_NONE;
	for (size_t k = 0; k < g->n_items; k++) {
		size_t i = dir > 0 ? k : g->n_items - 1 - k;
		double iy = g->items[i].y;
		if (dir > 0 ? iy <= y : iy >= y) continue;
		if (row < 0) row = iy;
		if (iy != row) break;
		if (best == SET_NONE || fabs(g->items[i].x - x) < fabs(g->items[best].x - x)) best = i;
	}
	return best;
}

static size_t next_stop(const struct set_hist_grid *g, size_t at, bool back) {
	size_t order[GF_COUNT + 1], n = 0;
	for (size_t i = 0; i < GF_COUNT; i++)
		if (g->stops[i].w > 0) order[n++] = i;
	order[n++] = SET_NONE;
	size_t k = 0;
	while (k < n && order[k] != at)
		k++;
	if (k == n) k = n - 1;
	if (back) return k == 0 ? (size_t)-2 : order[k - 1];
	return k + 1 >= n ? (size_t)-2 : order[k + 1];
}

bool set_hist_focus_search(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	size_t row;
	grid_relayout(st);
	if (!g || !grid_row(st, &row) || g->bar_h <= 0) return false;
	st->area = AREA_BODY;
	st->focus = row;
	g->focus_stop = GF_SEARCH;
	g->typing = true;
	ui_text_select_all(&g->query);
	st->body.offset = 0;
	ui_window_damage(st->win);
	return true;
}

static bool search_key(struct settings_state *st, const struct set_row *r, xkb_keysym_t sym,
					   uint8_t mods) {
	struct set_hist_grid *g = st->grid;
	if (sym == XKB_KEY_Down || sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) return false;
	char before[UI_TEXT_MAX];
	snprintf(before, sizeof before, "%s", g->query.buf);
	enum ui_text_result res = ui_text_key(&g->query, sym, mods, false);
	if (res == UI_TEXT_CANCEL) {
		if (g->query.len) {
			ui_text_set(&g->query, "");
		} else {
			g->typing = false;
			g->focus_stop = SET_NONE;
		}
	} else if (res == UI_TEXT_COMMIT) {
		g->typing = false;
		g->focus_stop = SET_NONE;
		if (g->n_items) g->sel = g->items[0].entry;
		reveal(st, r);
	} else if (res == UI_TEXT_IGNORED) {
		return false;
	}
	if (strcmp(before, g->query.buf) != 0) changed(st);
	ui_window_damage(st->win);
	return true;
}

bool set_hist_grid_keys(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	struct set_hist_grid *g = st->grid;
	const struct set_row *r = grid_row(st, NULL);
	if (!g || !r) return false;
	grid_relayout(st);
	bool shift = (mods & KB_MOD_SHIFT) != 0, ctrl = (mods & KB_MOD_CTRL) != 0;
	if (g->typing && g->focus_stop == GF_SEARCH && search_key(st, r, sym, mods)) return true;
	uint32_t ch = xkb_keysym_to_utf32(sym);
	bool printable = ch > 0x20 && ch != 0x7f && !(mods & (KB_MOD_ALT | KB_MOD_SUPER));
	if (!g->typing && g->bar_h > 0 && !ctrl && printable) {
		bool had = g->query.len > 0;
		set_hist_focus_search(st);
		ui_text_set(&g->query, "");
		bool done = search_key(st, r, sym, mods);
		if (had) changed(st);
		return done;
	}
	bool back = sym == XKB_KEY_ISO_Left_Tab || (sym == XKB_KEY_Tab && shift);
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		size_t to = next_stop(g, g->focus_stop, back);
		if (to == (size_t)-2) return false;
		g->focus_stop = to;
		g->typing = to == GF_SEARCH;
		if (to == SET_NONE && grid_order_pos(g, g->sel) == SET_NONE && g->n_items)
			g->sel = g->items[0].entry;
		ui_window_damage(st->win);
		return true;
	}
	if ((sym == XKB_KEY_Menu || (sym == XKB_KEY_F10 && shift)) && g->focus_stop == SET_NONE) {
		set_hist_grid_menu(st);
		return true;
	}
	if (g->focus_stop != SET_NONE) {
		if (sym == XKB_KEY_Down) {
			g->focus_stop = SET_NONE;
			g->typing = false;
			if (grid_order_pos(g, g->sel) == SET_NONE && g->n_items) g->sel = g->items[0].entry;
			reveal(st, r);
			ui_window_damage(st->win);
			return true;
		}
		if (sym == XKB_KEY_Return || sym == XKB_KEY_space) {
			activate_stop(st, r, g->focus_stop);
			ui_window_damage(st->win);
			return true;
		}
		if (sym == XKB_KEY_Left || sym == XKB_KEY_Right) {
			size_t to = next_stop(g, g->focus_stop, sym == XKB_KEY_Left);
			if (to != (size_t)-2 && to != SET_NONE) g->focus_stop = to;
			g->typing = g->focus_stop == GF_SEARCH;
			ui_window_damage(st->win);
			return true;
		}
		return sym == XKB_KEY_Up;
	}
	if (g->n_items == 0) return false;
	size_t pos = grid_order_pos(g, g->sel);
	if (pos == SET_NONE) pos = 0;
	size_t to = pos;
	switch (sym) {
	case XKB_KEY_Left:
		to = pos ? pos - 1 : pos;
		break;
	case XKB_KEY_Right:
		to = pos + 1 < g->n_items ? pos + 1 : pos;
		break;
	case XKB_KEY_Up:
		to = vertical(g, pos, -1);
		if (to == SET_NONE) {
			if (g->bar_h <= 0) return false;
			g->focus_stop = GF_SEARCH;
			g->typing = true;
			st->body.offset = 0;
			ui_window_damage(st->win);
			return true;
		}
		break;
	case XKB_KEY_Down:
		to = vertical(g, pos, 1);
		if (to == SET_NONE) to = pos;
		break;
	case XKB_KEY_Home:
		to = 0;
		break;
	case XKB_KEY_End:
		to = g->n_items - 1;
		break;
	case XKB_KEY_Return:
	case XKB_KEY_space:
		g->sel = g->items[pos].entry;
		viewer_show(st, g->sel);
		return true;
	default:
		return false;
	}
	g->sel = g->items[to].entry;
	reveal(st, r);
	ui_window_damage(st->win);
	return true;
}

void set_hist_grid_menu(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	size_t row;
	const struct set_row *r = grid_row(st, &row);
	if (!g || g->n_items == 0 || !r) return;
	size_t pos = grid_order_pos(g, g->sel);
	if (pos == SET_NONE) pos = 0;
	g->sel = g->items[pos].entry;
	struct set_rect t = grid_tile_rect(g, r, pos);
	g->tile_menu = true;
	set_hist_open_menu(st, row, &st->hist.v[g->sel], t.x + t.w / 2, t.y + t.h / 2);
}

static double custom_height(struct settings_state *st, struct set_row *r) {
	(void)r;
	return set_hist_grid_h(st);
}

static void custom_draw(struct settings_state *st, cairo_t *cr, struct set_row *r, bool active) {
	set_hist_grid_draw(st, cr, r, active && st->focus_visible);
}

static void custom_press(struct settings_state *st, struct set_row *r, double bx, double by,
						 uint32_t button) {
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; i < p->n; i++)
		if (p->rows[i] == r) set_hist_grid_press(st, i, bx, by, button);
}

static int custom_motion(struct settings_state *st, struct set_row *r, double bx, double by) {
	return set_hist_grid_motion(st, r, bx, by);
}

static bool custom_key(struct settings_state *st, struct set_row *r, xkb_keysym_t sym,
					   uint8_t mods) {
	(void)r;
	return set_hist_grid_keys(st, sym, mods);
}

static void custom_overlay(struct settings_state *st, cairo_t *cr, struct set_row *r) {
	(void)r;
	set_hist_overlay_draw(st, cr);
}

const struct set_custom SET_GRID_CUSTOM = {
	.height = custom_height,
	.draw = custom_draw,
	.press = custom_press,
	.motion = custom_motion,
	.key = custom_key,
	.typing = set_hist_typing,
	.overlay = custom_overlay,
};
