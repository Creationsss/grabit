// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/settings_internal.h"

#include "cairo_util.h"
#include "region/edit_persist.h"
#include "ui/widgets.h"
#include "ui/window.h"
#include "ui_colors.h"
#include "ui_theme.h"
#include "util/util.h"

#include <math.h>
#include <string.h>

static const char *const MENU_LABELS[MENU_COUNT] = {
	[MENU_RESET] = "Reset to default",
	[MENU_COPY_KEY] = "Copy key name",
};

static struct set_row *popup_row(struct settings_state *st) {
	struct set_page *p = set_current_page(st);
	return st->popup != POPUP_NONE && st->popup_row < p->n ? p->rows[st->popup_row] : NULL;
}

bool set_popup_filtered(struct settings_state *st) {
	struct set_row *r = st->popup == POPUP_SELECT ? popup_row(st) : NULL;
	return r && set_dd_count(r) > SET_POPUP_MAX_ITEMS;
}

static double filter_h(struct settings_state *st) {
	return set_popup_filtered(st) ? SET_POPUP_FILTER_H : 0;
}

size_t set_popup_choice(const struct settings_state *st, size_t i) {
	if (st->popup != POPUP_SELECT) return i;
	return i < st->popup_map_n ? st->popup_map[i] : SET_NONE;
}

size_t set_popup_count(struct settings_state *st) {
	struct set_row *r = popup_row(st);
	if (!r) return 0;
	if (st->popup == POPUP_ACTIONS) return st->popup_act_n;
	return st->popup == POPUP_MENU ? MENU_COUNT : st->popup_map_n;
}

const char *set_popup_label(struct settings_state *st, size_t i) {
	if (st->popup == POPUP_MENU) return MENU_LABELS[i];
	if (st->popup == POPUP_ACTIONS) return st->popup_acts[i].label;
	return set_dd_label(popup_row(st), set_popup_choice(st, i));
}

bool set_popup_enabled(struct settings_state *st, size_t i) {
	if (st->popup != POPUP_MENU || i != MENU_RESET) return true;
	return set_can_reset(st, popup_row(st));
}

static double popup_h(size_t vis, double filter) {
	return (double)vis * SET_POPUP_ITEM_H + 8 + filter;
}

bool set_popup_rect(struct settings_state *st, struct set_rect *out, size_t *visible) {
	struct set_row *r = popup_row(st);
	if (!r) return false;
	size_t n = set_popup_count(st);
	double fh = filter_h(st);
	size_t vis = fh > 0 || n > SET_POPUP_MAX_ITEMS ? SET_POPUP_MAX_ITEMS : n;
	double h = popup_h(vis, fh);
	double top = st->body.offset, bottom = top + st->view_h;
	if (st->popup == POPUP_MENU || st->popup == POPUP_ACTIONS) {
		if (visible) *visible = vis;
		double x = st->popup_x, y = st->popup_y;
		double max_x = set_body_w(st) - SET_PAD - SET_MENU_W;
		if (x > max_x) x = max_x;
		if (x < SET_PAD) x = SET_PAD;
		if (y + h > bottom) y -= h;
		if (y < top) y = top;
		*out = (struct set_rect){x, y, SET_MENU_W, h};
		return true;
	}
	struct set_rect c;
	if (!set_part_rect(st, r, SP_CONTROL, &c)) return false;
	double below = bottom - (c.y + c.h + 4), above = c.y - 4 - top;
	bool down = h <= below || (h > above && below >= above);
	double room = down ? below : above;
	if (h > room) {
		double fit = (room - 8 - fh) / SET_POPUP_ITEM_H;
		vis = fit < 1 ? 1 : (size_t)fit;
		h = popup_h(vis, fh);
	}
	if (visible) *visible = vis;
	double y = down ? c.y + c.h + 4 : c.y - 4 - h;
	*out = (struct set_rect){c.x, y, c.w, h};
	return true;
}

bool set_popup_area(struct settings_state *st, struct set_rect *outer, struct set_rect *list) {
	size_t vis;
	if (!set_popup_rect(st, outer, &vis)) return false;
	*list = (struct set_rect){outer->x, outer->y + 4 + filter_h(st), outer->w,
							  (double)vis * SET_POPUP_ITEM_H};
	return true;
}

bool set_popup_sync(struct settings_state *st, struct set_rect *outer, struct set_rect *list) {
	if (!set_popup_area(st, outer, list)) return false;
	ui_scroll_set(&st->popup_scroll, (double)set_popup_count(st) * SET_POPUP_ITEM_H, list->h);
	return true;
}

size_t set_popup_item_at(struct settings_state *st, double bx, double by) {
	struct set_rect d, list;
	if (!set_popup_area(st, &d, &list) || !set_rect_contains(&d, bx, by)) return SET_NONE;
	if (!set_rect_contains(&list, bx, by)) return SET_POPUP_PAD_HIT;
	double rel = by - list.y + st->popup_scroll.offset;
	size_t i = (size_t)(rel / SET_POPUP_ITEM_H);
	return i < set_popup_count(st) ? i : SET_POPUP_PAD_HIT;
}

bool set_search_clear_rect(const struct settings_state *st, struct set_rect *out) {
	if (st->search.len == 0) return false;
	double sw = set_sidebar_w(st->view_w);
	*out = (struct set_rect){sw - SET_PAD - SET_SEARCH_H, SET_PAD - st->side.offset,
							 SET_SEARCH_H, SET_SEARCH_H};
	return true;
}

size_t set_side_hit(const struct settings_state *st, double x, double y) {
	struct set_rect clear;
	if (set_search_clear_rect(st, &clear) && set_rect_contains(&clear, x, y))
		return SET_SIDE_CLEAR;
	double cy = y + st->side.offset;
	if (cy >= SET_PAD && cy < SET_PAD + SET_SEARCH_H) return SET_SIDE_SEARCH;
	for (size_t i = 0; i < st->n_pages; i++) {
		double ry = set_side_row_y(st, i);
		if (cy >= ry && cy < ry + SET_SIDE_ROW_H) return i;
	}
	return SET_NONE;
}

const char *set_tip_text(struct settings_state *st, size_t row, enum set_part part) {
	if (row == SET_SIDE_CLEAR) return "Clear search";
	struct set_page *p = set_current_page(st);
	if (row >= p->n) return NULL;
	const struct set_row *r = p->rows[row];
	switch (part) {
	case SP_RESET: {
		char def[96];
		set_default_text(r, def, sizeof def);
		if (r->info.secret || !def[0])
			snprintf(st->tip_buf, sizeof st->tip_buf, "Reset to default");
		else
			snprintf(st->tip_buf, sizeof st->tip_buf, "Reset to default (%s)", def);
		return st->tip_buf;
	}
	case SP_AUX:
		if (r->aux == AUX_EYE) return r->reveal ? "Hide" : "Show";
		if (r->aux == AUX_RECORD) return "Record keys";
		return NULL;
	case SP_MINUS:
		return set_step_enabled(st, r, -1) ? "Decrease (Shift+click: by 10)" : NULL;
	case SP_PLUS:
		return set_step_enabled(st, r, 1) ? "Increase (Shift+click: by 10)" : NULL;
	case SP_LABEL:
	case SP_NONE:
	case SP_ROW:
	case SP_CONTROL:
	case SP_ALT:
	case SP_ALT2:
		break;
	}
	return NULL;
}

static void draw_x(cairo_t *cr, double cx, double cy, double size, double alpha) {
	cairo_set_source_rgba(cr, GUI_C_INK, alpha);
	grabit_cairo_cross(cr, cx, cy, size / 2.0, 1.4);
}

#define LEGEND_ROW 20.0
#define LEGEND_N 4

double set_legend_h(void) {
	return 26 + LEGEND_N * LEGEND_ROW + SET_PAD;
}

#define SIDE_BOTTOM_GAP 20.0

static size_t bottom_pages(const struct settings_state *st) {
	size_t n = 0;
	for (size_t i = 0; i < st->n_pages; i++)
		n += st->pages[i].bottom;
	return n;
}

static double sidebar_list_end(const struct settings_state *st) {
	size_t bot = bottom_pages(st);
	return SET_SIDE_LIST_Y + (double)(st->n_pages - bot) * SET_SIDE_ROW_H +
		   (bot ? SIDE_BOTTOM_GAP + (double)bot * SET_SIDE_ROW_H : 0);
}

double set_legend_y(const struct settings_state *st, double h) {
	double y = h - set_legend_h();
	double min = sidebar_list_end(st) + 16;
	return y < min ? min : y;
}

double set_side_row_y(const struct settings_state *st, size_t i) {
	size_t top = 0, below = 0;
	for (size_t j = 0; j < st->n_pages; j++) {
		if (!st->pages[j].bottom)
			top += j < i;
		else if (j < i)
			below++;
	}
	if (i >= st->n_pages || !st->pages[i].bottom) return SET_SIDE_LIST_Y + (double)top * SET_SIDE_ROW_H;
	double start = set_legend_y(st, st->view_h) - 12 - (double)bottom_pages(st) * SET_SIDE_ROW_H;
	return start + (double)below * SET_SIDE_ROW_H;
}

static void draw_legend(cairo_t *cr, double sw, double y) {
	cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
	cairo_rectangle(cr, SET_PAD, y, sw - SET_PAD * 2, 1);
	cairo_fill(cr);
	ui_draw_text_fit(cr, SET_PAD + 4, y + 8, sw - SET_PAD * 2, "Key", 11, CAIRO_FONT_WEIGHT_BOLD,
					 0.55);
	static const char *const TEXT[LEGEND_N] = {"Changed from default", "Not used right now",
											   "Invalid value", "Config key name"};
	double x = SET_PAD + 4, tx = x + 30, tw = sw - tx - SET_PAD;
	for (int i = 0; i < LEGEND_N; i++) {
		double ry = y + 26 + i * LEGEND_ROW, cy = ry + LEGEND_ROW / 2.0 - 1;
		double alpha = 0.66;
		switch (i) {
		case 0:
			set_draw_chip(cr, x, cy, "Mod", 8.0, true, NULL);
			break;
		case 1:
			ui_draw_text_fit(cr, x, cy - 8, 24, "Aa", 12, CAIRO_FONT_WEIGHT_NORMAL, 0.35);
			alpha = 0.4;
			break;
		case 2:
			ui_warning_icon_draw(cr, x + 10, cy, 11);
			break;
		case 3:
			set_draw_chip(cr, x + 1, cy, "a.b", 8.5, false, NULL);
			break;
		}
		ui_draw_text_fit(cr, tx, cy - 8, tw, TEXT[i], 11.5, CAIRO_FONT_WEIGHT_NORMAL, alpha);
	}
}

static void draw_selection(cairo_t *cr, double x, double y, double w, double h, bool sel,
						   bool hot, double hover_alpha) {
	if (!sel && !hot) return;
	if (sel)
		grabit_ui_accent(cr, hot ? 0.30 : 0.22);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, hover_alpha);
	grabit_cairo_rect_r(cr, x, y, w, h, grabit_ui_radius(GUI_R_BTN));
	cairo_fill(cr);
}

void set_draw_sidebar(struct settings_state *st, cairo_t *cr, double sw, double h) {
	cairo_set_source_rgba(cr, GUI_C_INK, 0.03);
	cairo_rectangle(cr, 0, 0, sw, h);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
	cairo_rectangle(cr, sw, 0, 1, h);
	cairo_fill(cr);

	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, sw, h);
	cairo_clip(cr);
	cairo_translate(cr, 0, -st->side.offset);

	bool searching = st->search.len > 0;
	ui_text_draw(cr, st->area == AREA_SEARCH ? &st->search : NULL, SET_PAD, SET_PAD,
				 sw - SET_PAD * 2, SET_SEARCH_H, searching ? SET_SEARCH_H - 6 : 0, false,
				 st->search.buf, "Search  (Ctrl+F)");
	if (searching)
		draw_x(cr, sw - SET_PAD - SET_SEARCH_H / 2.0, SET_PAD + SET_SEARCH_H / 2.0, 8,
			   st->side_hover == SET_SIDE_CLEAR ? 0.95 : 0.55);
	draw_legend(cr, sw, set_legend_y(st, h));

	for (size_t i = 0; i < st->n_pages; i++) {
		double y = set_side_row_y(st, i);
		if (st->pages[i].bottom && (i == 0 || !st->pages[i - 1].bottom)) {
			cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
			cairo_rectangle(cr, SET_PAD, y - 8, sw - SET_PAD * 2, 1);
			cairo_fill(cr);
		}
		bool sel = !searching && i == st->page;
		draw_selection(cr, SET_PAD / 2.0, y, sw - SET_PAD, SET_SIDE_ROW_H - 2, sel,
					   i == st->side_hover, 0.05);
		if (sel && st->area == AREA_SIDEBAR && st->focus_visible) {
			grabit_ui_accent(cr, 0.9);
			grabit_cairo_rect_r(cr, SET_PAD / 2.0, y + 7, 3, SET_SIDE_ROW_H - 16, 1.5);
			cairo_fill(cr);
		}
		double tw = sw - SET_PAD * 2 - 8;
		double alpha = sel ? 1.0 : 0.86;
		if (searching) {
			size_t hits = set_page_matches(st, i);
			if (hits == 0) alpha = 0.3;
			char count[24];
			snprintf(count, sizeof count, "%zu", hits);
			double cw = ui_text_width(cr, count, 11.5, CAIRO_FONT_WEIGHT_NORMAL);
			if (hits)
				ui_draw_text_fit(cr, sw - SET_PAD - 6 - cw, y + 8, cw + 1, count, 11.5,
								 CAIRO_FONT_WEIGHT_NORMAL, 0.55);
			tw -= cw + 10;
		}
		ui_draw_text_fit(cr, SET_PAD + 8, y + 7, tw, st->pages[i].title, 13,
						 CAIRO_FONT_WEIGHT_NORMAL, alpha);
	}
	cairo_restore(cr);
	ui_scroll_draw(cr, &st->side, sw);
}

void set_draw_popup(struct settings_state *st, cairo_t *cr) {
	struct set_rect d;
	size_t vis;
	if (!set_popup_rect(st, &d, &vis)) return;
	double rad = grabit_ui_radius(GUI_R_BTN);
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.35);
	grabit_cairo_rect_r(cr, d.x + 1, d.y + 3, d.w, d.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_SURFACE, 1.0);
	grabit_cairo_rect_r(cr, d.x, d.y, d.w, d.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.14);
	cairo_set_line_width(cr, 1.0);
	grabit_cairo_rect_r_inset(cr, d.x, d.y, d.w, d.h, rad, 1.0);
	cairo_stroke(cr);

	bool select = st->popup == POPUP_SELECT;
	struct set_row *r = popup_row(st);
	size_t n = set_popup_count(st);
	struct set_rect list;
	set_popup_sync(st, &d, &list);
	size_t first = (size_t)(st->popup_scroll.offset / SET_POPUP_ITEM_H);
	size_t cur = select ? set_dd_index_of(st, r) : SET_NONE;
	bool icons = select && set_row_has_icons(r);
	double fh = filter_h(st);
	double text_x = icons ? 18 + SET_ICON_PX : 12;
	if (fh > 0)
		ui_text_draw(cr, &st->popup_filter, d.x + 6, d.y + 6, d.w - 12, fh - 8, 0, false, NULL,
					 icons ? "Search icons" : "Search");
	cairo_save(cr);
	cairo_rectangle(cr, list.x, list.y, list.w, list.h);
	cairo_clip(cr);
	for (size_t idx = first; idx < n && idx <= first + vis; idx++) {
		size_t choice = set_popup_choice(st, idx);
		double y = list.y + (double)idx * SET_POPUP_ITEM_H - st->popup_scroll.offset;
		bool enabled = set_popup_enabled(st, idx);
		bool hot = idx == st->popup_hover && enabled;
		bool is_cur = select && choice == cur;
		draw_selection(cr, d.x + 4, y, d.w - 8, SET_POPUP_ITEM_H, is_cur, hot, 0.09);
		if (icons) set_draw_icon(st, cr, set_dd_value(r, choice), d.x + 12, y + SET_POPUP_ITEM_H / 2.0);
		bool dim = !enabled || (select && !set_dd_value(r, choice)[0]);
		double alpha = !enabled ? 0.35 : is_cur ? 1.0
									 : dim		? 0.55
												: 0.86;
		ui_draw_text_fit(cr, d.x + text_x, y + (SET_POPUP_ITEM_H - 16) / 2.0,
						 d.w - text_x - 8, set_popup_label(st, idx), 13,
						 CAIRO_FONT_WEIGHT_NORMAL, alpha);
	}
	if (n == 0)
		ui_draw_text_fit(cr, d.x + 12, list.y + (SET_POPUP_ITEM_H - 16) / 2.0, d.w - 20,
						 icons && ui_icon_is_file(st->popup_filter.buf) &&
								 ui_icon_supported(st->popup_filter.buf)
							 ? "press Enter to use this image file"
							 : "no matches",
						 13, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	cairo_restore(cr);
	if (n > vis) {
		cairo_save(cr);
		cairo_translate(cr, list.x, list.y);
		ui_scroll_draw(cr, &st->popup_scroll, list.w - 2);
		cairo_restore(cr);
	}
}

void set_draw_tip(struct settings_state *st, cairo_t *cr) {
	if (!set_tip_visible(st)) return;
	const char *text = set_tip_text(st, st->tip_row, st->tip_part);
	if (!text) return;
	struct set_rect a;
	if (st->tip_row == SET_SIDE_CLEAR) {
		if (!set_search_clear_rect(st, &a)) return;
	} else {
		struct set_page *p = set_current_page(st);
		if (st->tip_row >= p->n || !set_part_rect(st, p->rows[st->tip_row], st->tip_part, &a))
			return;
		a.x += set_body_x(st);
		a.y -= st->body.offset;
	}
	grabit_ui_tooltip(cr, 1.0, text, a.x + a.w / 2.0, a.y, a.y + a.h, st->view_w);
}

void set_draw_toast(struct settings_state *st, cairo_t *cr, double height) {
	st->toast_btn = (struct set_rect){0};
	if (!st->toast_until) return;
	const char *btn = st->toast_label;
	double size = 12, pad = 10, gap = 14, line = 16;
	double uw = btn ? ui_text_width(cr, btn, size, CAIRO_FONT_WEIGHT_BOLD) : 0;
	double extra = pad * 2 + (btn ? gap + uw : 0);
	double room = set_body_w(st) - SET_BODY_PAD * 2 - extra;
	char lines[3][160];
	size_t n = set_wrap(cr, st->toast, room, size, lines, 3);
	double tw = 0;
	for (size_t i = 0; i < n; i++) {
		double lw = ui_text_width(cr, lines[i], size, CAIRO_FONT_WEIGHT_NORMAL);
		if (lw > tw) tw = lw;
	}
	if (tw > room) tw = room;
	double w = tw + extra, h = 14 + line * (double)(n ? n : 1);
	double x = set_body_x(st) + (set_body_w(st) - w) / 2.0, y = height - 18 - h;
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.78);
	grabit_cairo_rect_r(cr, x, y, w, h, grabit_ui_radius(GUI_R_TIP));
	cairo_fill(cr);
	for (size_t i = 0; i < n; i++)
		ui_draw_text_fit(cr, x + pad, y + 7 + line * (double)i, tw, lines[i], size,
						 CAIRO_FONT_WEIGHT_NORMAL, 1.0);
	if (!btn) return;
	double ux = x + pad + tw + gap, ty = y + (h - line) / 2.0;
	if (st->toast_danger)
		ui_draw_text_fit_rgba(cr, ux, ty, uw + 1, btn, size, CAIRO_FONT_WEIGHT_BOLD,
							  GUI_C_ERROR, 1.0);
	else
		ui_draw_text_fit_rgba(cr, ux, ty, uw + 1, btn, size, CAIRO_FONT_WEIGHT_BOLD,
							  GUI_C_ACCENT, 1.0);
	double hit_x = ux - gap / 2;
	st->toast_btn = (struct set_rect){hit_x, y, x + w - hit_x, h};
}

void set_draw_drop_overlay(struct settings_state *st, cairo_t *cr, double w, double h) {
	cairo_set_source_rgba(cr, GUI_C_PANEL, 0.86);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);
	double pad = 18, r = grabit_ui_radius(GUI_R_PANEL);
	double dash[] = {6, 5};
	cairo_save(cr);
	cairo_set_dash(cr, dash, 2, 0);
	grabit_ui_accent(cr, 0.9);
	cairo_set_line_width(cr, 2);
	grabit_cairo_rect_r(cr, pad, pad, w - pad * 2, h - pad * 2, r);
	cairo_stroke(cr);
	cairo_restore(cr);
	const struct set_drop_hint *hint = set_drop_hint(st);
	const char *title = hint->title;
	const char *sub = hint->sub;
	double tw = ui_text_width(cr, title, 17, CAIRO_FONT_WEIGHT_BOLD);
	double sw = ui_text_width(cr, sub, 12.5, CAIRO_FONT_WEIGHT_NORMAL);
	ui_draw_text_fit(cr, (w - fmin(tw, w - pad * 4)) / 2, h / 2 - 22, w - pad * 4, title, 17, CAIRO_FONT_WEIGHT_BOLD,
					 0.95);
	ui_draw_text_fit(cr, (w - fmin(sw, w - pad * 4)) / 2, h / 2 + 6, w - pad * 4, sub, 12.5,
					 CAIRO_FONT_WEIGHT_NORMAL, 0.6);
}
