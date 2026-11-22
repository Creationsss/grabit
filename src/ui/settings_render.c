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

#define CTRL_MIN_W 210
#define TEXT_MIN_W 80
#define RESET_GAP 20
#define CTRL_MAX_W 320
#define LABEL_LINE 21.0
#define KEY_LINE 19.0
#define KEY_SIZE 10.5
#define CHIP_R 4.0
#define TAG_SIZE 9.5

static size_t wrap(cairo_t *cr, const char *text, double max_w, double size,
				   char lines[][160], size_t max_lines) {
	if (!text || !*text) return 0;
	char src[UI_TEXT_MAX];
	size_t len = strlen(text);
	len = grabit_utf8_valid_prefix(text, len < sizeof src ? len : sizeof src - 1);
	memcpy(src, text, len);
	src[len] = '\0';
	ui_set_font(cr, size, CAIRO_FONT_WEIGHT_NORMAL);
	size_t n = 0;
	const char *p = src;
	while (*p && n < max_lines) {
		while (*p == ' ')
			p++;
		size_t best = 0;
		size_t i = 0;
		char buf[160];
		while (p[i] && i < sizeof buf - 4) {
			size_t j = i;
			while (p[j] && p[j] != ' ' && j < sizeof buf - 4)
				j++;
			j = grabit_utf8_valid_prefix(p, j);
			if (j == i) break;
			memcpy(buf, p, j);
			buf[j] = '\0';
			cairo_text_extents_t ext;
			cairo_text_extents(cr, buf, &ext);
			if (ext.x_advance > max_w && best > 0) break;
			best = j;
			if (ext.x_advance > max_w) break;
			i = j;
			if (p[i] == ' ') i++;
		}
		if (best == 0)
			best = grabit_utf8_valid_prefix(p, strlen(p) < sizeof buf - 4 ? strlen(p)
																		  : sizeof buf - 4);
		if (best == 0) break;
		bool last = n + 1 == max_lines && p[best];
		memcpy(lines[n], p, best);
		lines[n][best] = '\0';
		if (last) memcpy(lines[n] + grabit_utf8_prev(lines[n], best), "\xe2\x80\xa6", 4);
		n++;
		p += best;
	}
	return n;
}

double set_sidebar_w(double window_w) {
	double want = window_w * 0.26;
	if (want < SET_SIDEBAR_MIN) want = SET_SIDEBAR_MIN;
	if (want > SET_SIDEBAR_MAX) want = SET_SIDEBAR_MAX;
	return want;
}

double set_body_x(const struct settings_state *st) {
	return set_sidebar_w(st->view_w) + 1;
}

double set_body_w(const struct settings_state *st) {
	return st->view_w - set_body_x(st);
}

static double ctrl_w(const struct settings_state *st) {
	double w = set_body_w(st) * 0.42;
	if (w < CTRL_MIN_W) w = CTRL_MIN_W;
	if (w > CTRL_MAX_W) w = CTRL_MAX_W;
	double room = set_body_w(st) - SET_BODY_PAD * 2 - TEXT_MIN_W - SET_RESET_W - RESET_GAP;
	if (w > room) w = room < 100 ? 100 : room;
	return w;
}

static double ctrl_x(const struct settings_state *st) {
	return set_body_w(st) - SET_BODY_PAD - ctrl_w(st);
}

static double text_col_w(const struct settings_state *st) {
	double w = ctrl_x(st) - SET_BODY_PAD - SET_RESET_W - RESET_GAP;
	return w < TEXT_MIN_W ? TEXT_MIN_W : w;
}

static double row_text_w(struct settings_state *st, const struct set_row *r) {
	double w = text_col_w(st);
	for (size_t k = SET_ALT_BTNS; k-- > 0;) {
		struct set_rect b;
		if (!set_part_rect(st, r, (enum set_part)(SP_ALT + k), &b)) continue;
		double room = b.x - SET_BODY_PAD - RESET_GAP;
		return room < w ? room : w;
	}
	return w;
}

#define ERROR_ICON_W 18

static double error_w(const struct settings_state *st) {
	return text_col_w(st) + ctrl_w(st) - ERROR_ICON_W;
}

#define BANNER_H 34
#define TITLE_H 30
#define NOTE_H 22

static double title_y(const struct settings_state *st) {
	return SET_TITLE_Y + (st->banner[0] ? BANNER_H : 0);
}

static double rows_top(struct settings_state *st) {
	return title_y(st) + TITLE_H + (set_current_page(st)->note ? NOTE_H : 0) + 8;
}

static bool capturing(const struct settings_state *st, const struct set_row *r) {
	return st->capture.row == r;
}

static const char *row_hint(const struct settings_state *st, const struct set_row *r) {
	return capturing(st, r) ? "press keys or right, middle or side mouse buttons; Enter saves, "
							  "Esc cancels, Backspace removes the last"
							: r->hint;
}

void set_layout(struct settings_state *st) {
	cairo_t *cr = ui_measure_cr();
	struct set_page *p = set_current_page(st);
	double y = rows_top(st);
	char lines[SET_HINT_MAX_LINES + 2][160];
	const char *section = NULL;
	for (size_t i = 0; i < p->n; i++) {
		struct set_row *r = p->rows[i];
		double tw = row_text_w(st, r);
		r->section_head = p != &st->results && r->section &&
						  (!section || strcmp(r->section, section) != 0);
		section = r->section;
		if (r->section_head) y += SET_SECTION_H;
		double h = SET_ROW_PAD + LABEL_LINE + KEY_LINE;
		const char *why = set_inactive_reason(st, r);
		h += (double)wrap(cr, r->note, tw, SET_HINT_SIZE, lines, 2) * SET_HINT_LINE;
		h += (double)wrap(cr, why, tw, SET_HINT_SIZE, lines, 2) * SET_HINT_LINE;
		h += (double)wrap(cr, row_hint(st, r), tw, SET_HINT_SIZE, lines, SET_HINT_MAX_LINES) *
			 SET_HINT_LINE;
		if (r->error[0])
			h += 4 + (double)wrap(cr, r->error, error_w(st), SET_HINT_SIZE, lines, 3) *
						 SET_HINT_LINE;
		h += SET_ROW_PAD;
		double min = SET_ROW_PAD * 2 + SET_CTRL_H;
		r->y = y;
		r->h = h < min ? min : h;
		y += r->h;
	}
}

double set_content_h(struct settings_state *st) {
	struct set_page *p = set_current_page(st);
	if (p->n == 0) return rows_top(st) + 40;
	const struct set_row *last = p->rows[p->n - 1];
	return last->y + last->h + SET_BODY_PAD;
}

static double label_y(const struct set_row *r) {
	return r->y + SET_ROW_PAD - 2;
}

#define CHIP_PAD 5.0

static double draw_chip(cairo_t *cr, double x, double cy, const char *text, double size,
						bool accent, double *base) {
	cairo_select_font_face(cr, accent ? "sans-serif" : "monospace", CAIRO_FONT_SLANT_NORMAL,
						   CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	cairo_text_extents_t te, cap;
	cairo_text_extents(cr, text, &te);
	cairo_text_extents(cr, "H", &cap);
	double h = fe.ascent + fe.descent + 3, w = te.x_advance + CHIP_PAD * 2;
	double y = ui_snap_y(cr, cy - cap.y_bearing / 2);
	if (base) *base = y;
	if (accent)
		grabit_ui_accent(cr, 0.16);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
	grabit_cairo_rect_r(cr, x, cy - h / 2, w, h, CHIP_R);
	cairo_fill(cr);
	if (accent)
		grabit_ui_accent(cr, 0.95);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, 0.78);
	cairo_move_to(cr, x + CHIP_PAD, y);
	cairo_show_text(cr, text);
	return w;
}

static void draw_key(cairo_t *cr, double x, double y, double max_w, const char *key,
					 char *tag, const char *meta) {
	double cy = y + KEY_LINE / 2.0, base;
	cairo_save(cr);
	cairo_rectangle(cr, x, y, max_w, KEY_LINE);
	cairo_clip(cr);
	double used = draw_chip(cr, x, cy, key, KEY_SIZE, false, &base) + 6;
	if (tag[0] && used + 40 < max_w) {
		ui_text_ellipsize(cr, tag, TAG_SIZE, CAIRO_FONT_WEIGHT_NORMAL,
						  max_w - used - CHIP_PAD * 2);
		used += draw_chip(cr, x + used, cy, tag, TAG_SIZE, true, NULL) + 8;
	}
	cairo_restore(cr);
	if (!meta || !meta[0] || used + 8 >= max_w) return;
	ui_set_font(cr, SET_HINT_SIZE, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	ui_draw_text_fit(cr, x + used, base - fe.ascent, max_w - used, meta, SET_HINT_SIZE,
					 CAIRO_FONT_WEIGHT_NORMAL, 0.5);
}

bool set_part_rect(struct settings_state *st, const struct set_row *r, enum set_part part,
				   struct set_rect *out) {
	double cx = ctrl_x(st), cw = ctrl_w(st);
	double y = r->y + SET_ROW_PAD;
	double right = set_body_w(st) - SET_BODY_PAD;
	switch (part) {
	case SP_NONE:
		return false;
	case SP_RESET: {
		if (!set_can_reset(st, r)) return false;
		double x0 = r->info.kind == CFG_KIND_BOOL ? right - UI_TOGGLE_W : cx;
		*out = (struct set_rect){x0 - 6 - SET_RESET_W, y + (SET_CTRL_H - SET_RESET_W) / 2.0,
								 SET_RESET_W, SET_RESET_W};
		return true;
	}
	case SP_ROW:
		*out = (struct set_rect){0, r->y, set_body_w(st), r->h};
		return true;
	case SP_LABEL: {
		double lw = ui_text_width(ui_measure_cr(), r->label, SET_LABEL_SIZE,
								  CAIRO_FONT_WEIGHT_NORMAL);
		double tw = row_text_w(st, r);
		*out = (struct set_rect){SET_BODY_PAD, label_y(r), lw < tw ? lw : tw, LABEL_LINE};
		return true;
	}
	case SP_MINUS:
		if (r->info.kind != CFG_KIND_INT) return false;
		*out = (struct set_rect){cx, y, SET_BTN_W, SET_CTRL_H};
		return true;
	case SP_PLUS:
		if (r->info.kind != CFG_KIND_INT) return false;
		*out = (struct set_rect){cx + cw - SET_BTN_W, y, SET_BTN_W, SET_CTRL_H};
		return true;
	case SP_AUX:
		if (r->aux == AUX_NONE) return false;
		*out = (struct set_rect){cx + cw - SET_BTN_W, y, SET_BTN_W, SET_CTRL_H};
		return true;
	case SP_ALT:
	case SP_ALT2: {
		size_t k = (size_t)(part - SP_ALT);
		if (!r->alt[k].label) return false;
		*out = (struct set_rect){right - (double)(k + 2) * SET_ROW_BTN_W - (double)(k + 1) * 2 * SET_GAP,
								 y, SET_ROW_BTN_W, SET_CTRL_H};
		return true;
	}
	case SP_CONTROL:
		if (r->button) {
			*out = (struct set_rect){right - SET_ROW_BTN_W, y, SET_ROW_BTN_W, SET_CTRL_H};
			return true;
		}
		switch (r->info.kind) {
		case CFG_KIND_BOOL:
			*out = (struct set_rect){right - UI_TOGGLE_W, y + (SET_CTRL_H - UI_TOGGLE_H) / 2.0,
									 UI_TOGGLE_W, UI_TOGGLE_H};
			return true;
		case CFG_KIND_INT:
			*out = (struct set_rect){cx + SET_BTN_W + SET_GAP, y,
									 cw - 2 * (SET_BTN_W + SET_GAP), SET_CTRL_H};
			return true;
		case CFG_KIND_ENUM:
		case CFG_KIND_TEXT:
		case CFG_KIND_KEYBIND:
			*out = (struct set_rect){cx, y, cw - (r->aux ? SET_BTN_W + SET_GAP : 0),
									 SET_CTRL_H};
			return true;
		}
	}
	return false;
}

bool set_rect_contains(const struct set_rect *r, double x, double y) {
	return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

size_t set_row_at(struct settings_state *st, double y) {
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; i < p->n; i++)
		if (y >= p->rows[i]->y && y < p->rows[i]->y + p->rows[i]->h) return i;
	return SET_NONE;
}

enum set_part set_part_at(struct settings_state *st, const struct set_row *r, double x,
						  double y) {
	static const enum set_part ORDER[] = {SP_RESET, SP_MINUS, SP_PLUS, SP_AUX,
										  SP_ALT2, SP_ALT, SP_CONTROL, SP_LABEL};
	struct set_rect rc;
	for (size_t i = 0; i < sizeof ORDER / sizeof *ORDER; i++)
		if (set_part_rect(st, r, ORDER[i], &rc) && set_rect_contains(&rc, x, y)) return ORDER[i];
	return SP_ROW;
}

static const char *const MENU_LABELS[MENU_COUNT] = {
	[MENU_RESET] = "Reset to default",
	[MENU_COPY_KEY] = "Copy key name",
};

static void draw_icon(struct settings_state *st, cairo_t *cr, const char *name, double x,
					  double cy) {
	double scale = st->win && st->win->scale120 > 0 ? st->win->scale120 / 120.0 : 1.0;
	int px = (int)(SET_ICON_PX * scale + 0.5);
	cairo_surface_t *s = set_icon(st, name, px);
	if (!s) return;
	size_t n = strlen(name);
	bool symbolic = n > 9 && strcmp(name + n - 9, "-symbolic") == 0;
	cairo_save(cr);
	cairo_translate(cr, x, cy - SET_ICON_PX / 2.0);
	cairo_scale(cr, 1.0 / scale, 1.0 / scale);
	if (symbolic) {
		cairo_set_source_rgba(cr, GUI_C_TEXT, 0.9);
		cairo_mask_surface(cr, s, 0, 0);
	} else {
		cairo_set_source_surface(cr, s, 0, 0);
		cairo_paint(cr);
	}
	cairo_restore(cr);
}

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
	return st->popup == POPUP_MENU ? MENU_COUNT : st->popup_map_n;
}

const char *set_popup_label(struct settings_state *st, size_t i) {
	if (st->popup == POPUP_MENU) return MENU_LABELS[i];
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
	if (st->popup == POPUP_MENU) {
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

size_t set_popup_item_at(struct settings_state *st, double bx, double by) {
	struct set_rect d;
	size_t vis;
	if (!set_popup_rect(st, &d, &vis) || !set_rect_contains(&d, bx, by)) return SET_NONE;
	double list_top = d.y + 4 + filter_h(st);
	if (by < list_top || by >= list_top + (double)vis * SET_POPUP_ITEM_H) return SET_POPUP_PAD_HIT;
	double rel = by - list_top + st->popup_offset;
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
	double row = (cy - SET_SIDE_LIST_Y) / SET_SIDE_ROW_H;
	return row >= 0 && (size_t)row < st->n_pages ? (size_t)row : SET_NONE;
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

static double legend_h(void) {
	return 26 + LEGEND_N * LEGEND_ROW + SET_PAD;
}

static double sidebar_list_end(const struct settings_state *st) {
	return SET_SIDE_LIST_Y + (double)st->n_pages * SET_SIDE_ROW_H;
}

static double legend_y(const struct settings_state *st, double h) {
	double y = h - legend_h();
	double min = sidebar_list_end(st) + 16;
	return y < min ? min : y;
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
			draw_chip(cr, x, cy, "Mod", 8.0, true, NULL);
			break;
		case 1:
			ui_draw_text_fit(cr, x, cy - 8, 24, "Aa", 12, CAIRO_FONT_WEIGHT_NORMAL, 0.35);
			alpha = 0.4;
			break;
		case 2:
			ui_warning_icon_draw(cr, x + 10, cy, 11);
			break;
		case 3:
			draw_chip(cr, x + 1, cy, "a.b", 8.5, false, NULL);
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

static void draw_sidebar(struct settings_state *st, cairo_t *cr, double sw, double h) {
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
	draw_legend(cr, sw, legend_y(st, h));

	for (size_t i = 0; i < st->n_pages; i++) {
		double y = SET_SIDE_LIST_Y + (double)i * SET_SIDE_ROW_H;
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

static bool pressed(const struct settings_state *st, size_t i, enum set_part part) {
	return st->press_row == i && st->press_part == part;
}

static bool hovered(const struct settings_state *st, size_t i, enum set_part part) {
	return st->hover_row == i && st->hover_part == part;
}

static void draw_select(struct settings_state *st, cairo_t *cr, size_t i,
						const struct set_row *r, const struct set_rect *c, bool focused) {
	bool open = st->popup == POPUP_SELECT && st->popup_row == i;
	ui_field_bg(cr, c->x, c->y, c->w, c->h, focused || open);
	if (hovered(st, i, SP_CONTROL) && !open) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.04);
		grabit_cairo_rect_r(cr, c->x, c->y, c->w, c->h, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
	}
	set_icons_ensure(st, (struct set_row *)r, false);
	size_t idx = set_dd_index_of(st, r);
	const char *label = set_dd_label(r, idx);
	double tx = c->x + 8;
	if (set_row_has_icons(r)) {
		draw_icon(st, cr, set_dd_value(r, idx), c->x + 8, c->y + c->h / 2.0);
		tx += SET_ICON_PX + 8;
	}
	ui_draw_text_fit(cr, tx, c->y + (c->h - 16) / 2.0, c->x + c->w - 26 - tx, label, 13,
					 CAIRO_FONT_WEIGHT_NORMAL, 0.92);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.7);
	ui_chevron_draw(cr, c->x + c->w - 15, c->y + c->h / 2.0, 8, open);
}

static const char *placeholder(const struct set_row *r) {
	const char *ex = r->info.example;
	if (r->info.kind != CFG_KIND_TEXT || !ex || strlen(ex) > 40) return NULL;
	if (strstr(ex, "; ") || strstr(ex, " | ") || strchr(ex, '`')) return NULL;
	return ex;
}

static void ring(cairo_t *cr, const struct set_rect *c, double alpha, double width) {
	grabit_ui_accent(cr, alpha);
	cairo_set_line_width(cr, width);
	grabit_cairo_rect_r_inset(cr, c->x, c->y, c->w, c->h, grabit_ui_radius(GUI_R_BTN), width);
	cairo_stroke(cr);
}

static void focus_ring(cairo_t *cr, const struct set_rect *c) {
	ring(cr, c, 0.6, 1.0);
}

static void draw_field(struct settings_state *st, cairo_t *cr, const struct set_row *r,
					   const struct set_rect *c, bool focused) {
	bool capture = capturing(st, r);
	bool editing = st->editing && focused && !capture;
	char keys[256] = "";
	if (capture) region_keybind_list_join(&st->capture.binds, keys, sizeof keys);
	ui_text_draw(cr, editing ? &st->edit : NULL, c->x, c->y, c->w, c->h, 0,
				 !capture && set_masked(r), capture ? keys : set_effective(st, r),
				 capture ? "press keys" : placeholder(r));
	if (capture)
		ring(cr, c, 0.9, 1.5);
	else if (!editing && focused && st->focus_visible)
		focus_ring(cr, c);
}

static void draw_aux(struct settings_state *st, cairo_t *cr, size_t i,
					 const struct set_row *r) {
	struct set_rect a;
	if (!set_part_rect(st, r, SP_AUX, &a)) return;
	double mx = a.x + a.w / 2.0, my = a.y + a.h / 2.0;
	if (r->aux == AUX_SWATCH) {
		uint32_t rgb = 0;
		ui_field_bg(cr, a.x, a.y, a.w, a.h, false);
		if (edit_color_try(set_effective(st, r), &rgb)) {
			grabit_cairo_set_source_argb(cr, rgb, 1.0);
			grabit_cairo_rect_r(cr, a.x + 5, a.y + 5, a.w - 10, a.h - 10,
								grabit_ui_radius(GUI_R_GLYPH));
			cairo_fill(cr);
		}
		return;
	}
	ui_button_draw(cr, a.x, a.y, a.w, a.h, NULL, hovered(st, i, SP_AUX),
				   pressed(st, i, SP_AUX));
	if (r->aux == AUX_EYE)
		ui_eye_icon_draw(cr, mx, my, 20, !r->reveal, 0.8);
	else
		ui_record_icon_draw(cr, mx, my, 20, 0.8);
}

static void draw_stepper(struct settings_state *st, cairo_t *cr, size_t i,
						 const struct set_row *r, enum set_part part, const char *label) {
	struct set_rect b;
	set_part_rect(st, r, part, &b);
	bool on = set_step_enabled(st, r, part == SP_MINUS ? -1 : 1);
	if (!on) cairo_push_group(cr);
	ui_button_draw(cr, b.x, b.y, b.w, b.h, label, on && hovered(st, i, part),
				   on && pressed(st, i, part));
	if (on) return;
	cairo_pop_group_to_source(cr);
	cairo_paint_with_alpha(cr, 0.35);
}

static void draw_reset(struct settings_state *st, cairo_t *cr, size_t i,
					   const struct set_row *r, bool active) {
	struct set_rect b;
	if (!set_part_rect(st, r, SP_RESET, &b)) return;
	if (st->hover_row != i && !(active && st->focus_visible)) return;
	bool hot = hovered(st, i, SP_RESET);
	if (hot) {
		cairo_set_source_rgba(cr, GUI_C_INK, pressed(st, i, SP_RESET) ? 0.16 : 0.09);
		grabit_cairo_rect_r(cr, b.x, b.y, b.w, b.h, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
	}
	ui_reset_icon_draw(cr, b.x + b.w / 2.0, b.y + b.h / 2.0, b.w, hot ? 0.95 : 0.6);
}

static void draw_controls(struct settings_state *st, cairo_t *cr, size_t i,
						  struct set_row *r, bool active, bool focused) {
	struct set_rect c;
	set_part_rect(st, r, SP_CONTROL, &c);
	for (size_t k = 0; k < SET_ALT_BTNS; k++) {
		enum set_part part = (enum set_part)(SP_ALT + k);
		struct set_rect alt;
		if (set_part_rect(st, r, part, &alt))
			ui_button_draw(cr, alt.x, alt.y, alt.w, alt.h, r->alt[k].label, hovered(st, i, part),
						   pressed(st, i, part));
	}
	if (r->button) {
		ui_button_draw(cr, c.x, c.y, c.w, c.h, r->button, hovered(st, i, SP_CONTROL),
					   pressed(st, i, SP_CONTROL));
		if (focused) focus_ring(cr, &c);
		return;
	}
	switch (r->info.kind) {
	case CFG_KIND_BOOL:
		ui_toggle_draw(cr, c.x, c.y, strcmp(set_effective(st, r), "true") == 0,
					   hovered(st, i, SP_CONTROL), focused);
		break;
	case CFG_KIND_ENUM:
		draw_select(st, cr, i, r, &c, focused);
		break;
	case CFG_KIND_INT:
		draw_stepper(st, cr, i, r, SP_MINUS, "\xe2\x88\x92");
		draw_stepper(st, cr, i, r, SP_PLUS, "+");
		draw_field(st, cr, r, &c, active);
		break;
	case CFG_KIND_TEXT:
	case CFG_KIND_KEYBIND:
		draw_field(st, cr, r, &c, active);
		break;
	}
	draw_aux(st, cr, i, r);
	if (r->error[0]) {
		cairo_set_source_rgba(cr, GUI_C_ERROR, 0.9);
		cairo_set_line_width(cr, 1.5);
		grabit_cairo_rect_r_inset(cr, c.x, c.y, c.w, c.h, grabit_ui_radius(GUI_R_BTN), 1.5);
		cairo_stroke(cr);
	}
}

static void modified_tag(struct settings_state *st, const struct set_row *r, char *out,
						 size_t n) {
	out[0] = '\0';
	if (!set_can_reset(st, r)) return;
	char def[96] = "";
	bool has_def = r->info.kind == CFG_KIND_BOOL || (r->info.def && r->info.def[0]);
	if (!r->info.secret && has_def) set_default_text(r, def, sizeof def);
	if (def[0])
		snprintf(out, n, "Modified \xc2\xb7 default %s", def);
	else
		snprintf(out, n, "Modified");
}

static void draw_row(struct settings_state *st, cairo_t *cr, size_t i, struct set_row *r,
					 bool last) {
	bool active = st->area == AREA_BODY && st->focus == i;
	bool focused = active && st->focus_visible;
	const char *why = set_inactive_reason(st, r);
	double w = set_body_w(st);
	if (r->section_head)
		ui_draw_text_fit(cr, SET_BODY_PAD, r->y - SET_SECTION_H + 12, w - SET_BODY_PAD * 2,
						 r->section, 12, CAIRO_FONT_WEIGHT_BOLD, 0.6);
	if (focused) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.035);
		grabit_cairo_rect_r(cr, 8, r->y + 2, w - 16, r->h - 4, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
	}
	if (!last) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.06);
		cairo_rectangle(cr, SET_BODY_PAD, r->y + r->h - 1, w - SET_BODY_PAD * 2, 1);
		cairo_fill(cr);
	}

	double tw = row_text_w(st, r);
	double ty = label_y(r);
	ui_draw_text_fit(cr, SET_BODY_PAD, ty, tw, r->label, SET_LABEL_SIZE,
					 CAIRO_FONT_WEIGHT_NORMAL, why ? 0.5 : 0.95);

	char tag[192];
	modified_tag(st, r, tag, sizeof tag);
	draw_key(cr, SET_BODY_PAD, ty + LABEL_LINE, tw, r->key, tag,
			 set_current_page(st) == &st->results ? r->page : NULL);

	char lines[SET_HINT_MAX_LINES + 2][160];
	double y = ty + LABEL_LINE + KEY_LINE + 1;
	size_t n = wrap(cr, r->note, tw, SET_HINT_SIZE, lines, 2);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit_rgba(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
							  CAIRO_FONT_WEIGHT_NORMAL, GUI_C_ACCENT, 0.95);
	n = wrap(cr, why, tw, SET_HINT_SIZE, lines, 2);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.72);
	n = wrap(cr, row_hint(st, r), tw, SET_HINT_SIZE, lines, SET_HINT_MAX_LINES);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
						 CAIRO_FONT_WEIGHT_NORMAL, why ? 0.36 : 0.48);
	if (r->error[0]) {
		y += 4;
		ui_warning_icon_draw(cr, SET_BODY_PAD + 6, y + SET_HINT_LINE / 2.0, 11);
		n = wrap(cr, r->error, error_w(st), SET_HINT_SIZE, lines, 3);
		for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
			ui_draw_text_fit_rgba(cr, SET_BODY_PAD + ERROR_ICON_W, y, error_w(st) + 4, lines[k],
								  SET_HINT_SIZE, CAIRO_FONT_WEIGHT_NORMAL, GUI_C_ERROR, 0.95);
	}

	if (why) cairo_push_group(cr);
	draw_controls(st, cr, i, r, active, focused);
	if (why) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, 0.4);
	}
	draw_reset(st, cr, i, r, active);
}

static void draw_popup(struct settings_state *st, cairo_t *cr) {
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
	size_t first = (size_t)(st->popup_offset / SET_POPUP_ITEM_H);
	size_t cur = select ? set_dd_index_of(st, r) : SET_NONE;
	bool icons = select && set_row_has_icons(r);
	double fh = filter_h(st);
	double text_x = icons ? 18 + SET_ICON_PX : 12;
	if (fh > 0)
		ui_text_draw(cr, &st->popup_filter, d.x + 6, d.y + 6, d.w - 12, fh - 8, 0, false, NULL,
					 icons ? "Search icons" : "Search");
	double top = d.y + 4 + fh;
	cairo_save(cr);
	cairo_rectangle(cr, d.x, top, d.w, d.h - 8 - fh);
	cairo_clip(cr);
	for (size_t k = 0; k < vis && first + k < n; k++) {
		size_t idx = first + k;
		size_t choice = set_popup_choice(st, idx);
		double y = top + (double)k * SET_POPUP_ITEM_H;
		bool enabled = set_popup_enabled(st, idx);
		bool hot = idx == st->popup_hover && enabled;
		bool is_cur = select && choice == cur;
		draw_selection(cr, d.x + 4, y, d.w - 8, SET_POPUP_ITEM_H, is_cur, hot, 0.09);
		if (icons) draw_icon(st, cr, set_dd_value(r, choice), d.x + 12, y + SET_POPUP_ITEM_H / 2.0);
		bool dim = !enabled || (select && !set_dd_value(r, choice)[0]);
		double alpha = !enabled ? 0.35 : is_cur ? 1.0
									 : dim		? 0.55
												: 0.86;
		ui_draw_text_fit(cr, d.x + text_x, y + (SET_POPUP_ITEM_H - 16) / 2.0,
						 d.w - text_x - 8, set_popup_label(st, idx), 13,
						 CAIRO_FONT_WEIGHT_NORMAL, alpha);
	}
	if (n == 0)
		ui_draw_text_fit(cr, d.x + 12, top + (SET_POPUP_ITEM_H - 16) / 2.0, d.w - 20,
						 "no matches", 13, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	cairo_restore(cr);
	if (n > vis) {
		struct ui_scroll s = {.offset = st->popup_offset,
							  .content_h = (double)n * SET_POPUP_ITEM_H,
							  .view_h = (double)vis * SET_POPUP_ITEM_H};
		cairo_save(cr);
		cairo_translate(cr, d.x, top);
		ui_scroll_draw(cr, &s, d.w - 2);
		cairo_restore(cr);
	}
}

static void draw_tip(struct settings_state *st, cairo_t *cr) {
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

static void draw_body(struct settings_state *st, cairo_t *cr) {
	struct set_page *p = set_current_page(st);
	double bw = set_body_w(st);
	cairo_save(cr);
	cairo_translate(cr, set_body_x(st), 0);
	cairo_rectangle(cr, 0, 0, bw, st->view_h);
	cairo_clip(cr);
	cairo_save(cr);
	cairo_translate(cr, 0, -st->body.offset);

	if (st->banner[0]) {
		cairo_set_source_rgba(cr, GUI_C_ERROR, 0.14);
		grabit_cairo_rect_r(cr, SET_BODY_PAD - 8, SET_TITLE_Y - 6, bw - SET_BODY_PAD * 2 + 16,
							BANNER_H - 6, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
		ui_draw_text_fit(cr, SET_BODY_PAD, SET_TITLE_Y, bw - SET_BODY_PAD * 2, st->banner,
						 12.5, CAIRO_FONT_WEIGHT_NORMAL, 0.9);
	}
	double y = title_y(st);
	char title[UI_TEXT_MAX + 64];
	if (p == &st->results)
		snprintf(title, sizeof title, "%zu result%s for \xe2\x80\x9c%s\xe2\x80\x9d", p->n,
				 p->n == 1 ? "" : "s", st->search.buf);
	else
		snprintf(title, sizeof title, "%s", p->title);
	double title_w = bw - SET_BODY_PAD * 2;
	st->page_reset_btn = (struct set_rect){0};
	if (p != &st->results && set_page_resettable(st, p)) {
		const char *label = "Reset page";
		double rw = ui_text_width(cr, label, 12, CAIRO_FONT_WEIGHT_NORMAL) + 22, rh = 24;
		double rx = bw - SET_BODY_PAD - rw, ry = y - 3;
		ui_button_draw(cr, rx, ry, rw, rh, label, st->page_reset_hover, false);
		st->page_reset_btn = (struct set_rect){set_body_x(st) + rx, ry - st->body.offset, rw, rh};
		title_w -= rw + 12;
	}
	ui_draw_text_fit(cr, SET_BODY_PAD, y, title_w, title, 17, CAIRO_FONT_WEIGHT_BOLD, 0.92);
	if (p->note)
		ui_draw_text_fit(cr, SET_BODY_PAD, y + TITLE_H, bw - SET_BODY_PAD * 2, p->note, 11.5,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.45);

	double top = st->body.offset, bottom = top + st->view_h;
	for (size_t i = 0; i < p->n; i++) {
		struct set_row *r = p->rows[i];
		if (r->y + r->h < top || r->y - SET_SECTION_H > bottom) continue;
		draw_row(st, cr, i, r, i + 1 == p->n);
	}
	if (p->n == 0 && p == &st->results)
		ui_draw_text_fit(cr, SET_BODY_PAD, rows_top(st), bw - SET_BODY_PAD * 2,
						 "no settings match", 13, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	draw_popup(st, cr);
	cairo_restore(cr);
	ui_scroll_draw(cr, &st->body, bw);
	cairo_restore(cr);
}

static void draw_toast(struct settings_state *st, cairo_t *cr, double height) {
	st->toast_btn = (struct set_rect){0};
	if (!st->toast_until) return;
	const char *btn = st->toast_label;
	double size = 12, pad = 10, gap = 14, line = 16;
	double uw = btn ? ui_text_width(cr, btn, size, CAIRO_FONT_WEIGHT_BOLD) : 0;
	double extra = pad * 2 + (btn ? gap + uw : 0);
	double room = set_body_w(st) - SET_BODY_PAD * 2 - extra;
	char lines[3][160];
	size_t n = wrap(cr, st->toast, room, size, lines, 3);
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

static void draw_drop_overlay(struct settings_state *st, cairo_t *cr, double w, double h) {
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

static void draw_check(cairo_t *cr, double x, double y, double s, bool on, bool hover) {
	double r = grabit_ui_radius(GUI_R_GLYPH);
	if (on) {
		grabit_ui_accent(cr, hover ? 1.0 : 0.88);
		grabit_cairo_rect_r(cr, x, y, s, s, r);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, GUI_C_SHADE, 0.9);
		cairo_set_line_width(cr, 2.0);
		cairo_move_to(cr, x + s * 0.24, y + s * 0.52);
		cairo_line_to(cr, x + s * 0.43, y + s * 0.72);
		cairo_line_to(cr, x + s * 0.78, y + s * 0.30);
		cairo_stroke(cr);
		return;
	}
	cairo_set_source_rgba(cr, GUI_C_INK, hover ? 0.6 : 0.4);
	cairo_set_line_width(cr, 1.5);
	grabit_cairo_rect_r_inset(cr, x, y, s, s, r, 0.75);
	cairo_stroke(cr);
}

void set_dialog_draw(struct settings_state *st, cairo_t *cr, double width, double height) {
	struct set_dialog *d = &st->dialog;
	if (!d->open) return;
	double pad = 20, line = 18, btn_h = 30, gap = 8, check = 16;
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.55);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);
	double w = fmin(440, width - 32);
	char lines[4][160];
	size_t n = wrap(cr, d->text, w - pad * 2, 13, lines, 4);
	double h = pad + 22 + 6 + line * (double)n + (d->check_label ? 14 + check : 0) + 20 +
			   btn_h + pad;
	double x = floor((width - w) / 2), y = floor((height - h) / 2);
	cairo_set_source_rgba(cr, GUI_C_POPOVER, 0.98);
	grabit_cairo_rect_r(cr, x, y, w, h, grabit_ui_radius(GUI_R_PANEL));
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.12);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	double cy = y + pad;
	ui_draw_text_fit(cr, x + pad, cy, w - pad * 2, d->title, 15, CAIRO_FONT_WEIGHT_BOLD, 1.0);
	cy += 28;
	for (size_t i = 0; i < n; i++, cy += line)
		ui_draw_text_fit(cr, x + pad, cy, w - pad * 2, lines[i], 13, CAIRO_FONT_WEIGHT_NORMAL,
						 0.75);
	d->check_rect = (struct set_rect){0};
	if (d->check_label) {
		cy += 14;
		double lw = ui_text_width(cr, d->check_label, 13, CAIRO_FONT_WEIGHT_NORMAL);
		d->check_rect = (struct set_rect){x + pad - 4, cy - 4, check + 8 + lw + 8, check + 8};
		bool hot = d->hover == d->n_btn;
		draw_check(cr, x + pad, cy, check, d->check_on, hot);
		ui_draw_text_fit(cr, x + pad + check + 8, cy - 1, lw + 1, d->check_label, 13,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.9);
		if (st->focus_visible && d->focus == d->n_btn) focus_ring(cr, &d->check_rect);
		cy += check;
	}
	double bx = x + w - pad, by = y + h - pad - btn_h;
	static const double DANGER[] = {GUI_C_DESTRUCTIVE};
	for (size_t i = d->n_btn; i-- > 0;) {
		struct set_dialog_btn *b = &d->btn[i];
		double bw = fmax(84, ui_text_width(cr, b->label, 13, CAIRO_FONT_WEIGHT_NORMAL) + 28);
		bx -= bw;
		b->rect = (struct set_rect){bx, by, bw, btn_h};
		bool hot = d->hover == i, down = hot && d->press == i;
		if (b->danger)
			ui_button_draw_color(cr, bx, by, bw, btn_h, b->label, DANGER, hot, down);
		else
			ui_button_draw(cr, bx, by, bw, btn_h, b->label, hot, down);
		if (st->focus_visible && d->focus == i) focus_ring(cr, &b->rect);
		bx -= gap;
	}
}

void set_draw(struct ui_window *w, cairo_t *cr, double width, double height) {
	struct settings_state *st = w->user;
	st->view_w = width;
	st->view_h = height;
	set_layout(st);
	ui_scroll_set(&st->body, set_content_h(st), height);
	double sw = set_sidebar_w(width);
	ui_scroll_set(&st->side, legend_y(st, height) + legend_h(), height);
	draw_sidebar(st, cr, sw, height);
	draw_body(st, cr);
	draw_toast(st, cr, height);
	draw_tip(st, cr);
	set_dialog_draw(st, cr, width, height);
	if (st->drag_over) draw_drop_overlay(st, cr, width, height);
}
