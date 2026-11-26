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

size_t set_wrap(cairo_t *cr, const char *text, double max_w, double size,
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
		if (r->custom) {
			r->y = y;
			r->h = r->custom->height(st, r);
			y += r->h;
			continue;
		}
		double h = SET_ROW_PAD + LABEL_LINE + KEY_LINE;
		const char *why = set_inactive_reason(st, r);
		h += (double)set_wrap(cr, r->note, tw, SET_HINT_SIZE, lines, 2) * SET_HINT_LINE;
		h += (double)set_wrap(cr, why, tw, SET_HINT_SIZE, lines, 2) * SET_HINT_LINE;
		h += (double)set_wrap(cr, row_hint(st, r), tw, SET_HINT_SIZE, lines, SET_HINT_MAX_LINES) *
			 SET_HINT_LINE;
		if (r->error[0])
			h += 4 + (double)set_wrap(cr, r->error, error_w(st), SET_HINT_SIZE, lines, 3) *
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

double set_draw_chip(cairo_t *cr, double x, double cy, const char *text, double size,
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
	double used = set_draw_chip(cr, x, cy, key, KEY_SIZE, false, &base) + 6;
	if (tag[0] && used + 40 < max_w) {
		ui_text_ellipsize(cr, tag, TAG_SIZE, CAIRO_FONT_WEIGHT_NORMAL,
						  max_w - used - CHIP_PAD * 2);
		used += set_draw_chip(cr, x + used, cy, tag, TAG_SIZE, true, NULL) + 8;
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
	if (r->custom) return SP_ROW;
	struct set_rect rc;
	for (size_t i = 0; i < sizeof ORDER / sizeof *ORDER; i++)
		if (set_part_rect(st, r, ORDER[i], &rc) && set_rect_contains(&rc, x, y)) return ORDER[i];
	return SP_ROW;
}

void set_draw_icon(struct settings_state *st, cairo_t *cr, const char *name, double x,
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
		set_draw_icon(st, cr, set_dd_value(r, idx), c->x + 8, c->y + c->h / 2.0);
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

void set_focus_ring(cairo_t *cr, const struct set_rect *c) {
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
		set_focus_ring(cr, c);
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
		if (focused) set_focus_ring(cr, &c);
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
	if (r->scratch) {
		if (r->once && r->once_value) snprintf(out, n, "This upload");
		return;
	}
	char from[128];
	if (set_override_file(st, r, from, sizeof from)) {
		snprintf(out, n, "From %s", from);
		return;
	}
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
	if (r->custom) {
		(void)last;
		(void)why;
		(void)focused;
		r->custom->draw(st, cr, r, active);
		return;
	}
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
	draw_key(cr, SET_BODY_PAD, ty + LABEL_LINE, tw, r->once ? r->once : r->key, tag,
			 set_current_page(st) == &st->results ? r->page : NULL);

	char lines[SET_HINT_MAX_LINES + 2][160];
	double y = ty + LABEL_LINE + KEY_LINE + 1;
	size_t n = set_wrap(cr, r->note, tw, SET_HINT_SIZE, lines, 2);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit_rgba(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
							  CAIRO_FONT_WEIGHT_NORMAL, GUI_C_ACCENT, 0.95);
	n = set_wrap(cr, why, tw, SET_HINT_SIZE, lines, 2);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.72);
	n = set_wrap(cr, row_hint(st, r), tw, SET_HINT_SIZE, lines, SET_HINT_MAX_LINES);
	for (size_t k = 0; k < n; k++, y += SET_HINT_LINE)
		ui_draw_text_fit(cr, SET_BODY_PAD, y, tw + 4, lines[k], SET_HINT_SIZE,
						 CAIRO_FONT_WEIGHT_NORMAL, why ? 0.36 : 0.48);
	if (r->error[0]) {
		y += 4;
		ui_warning_icon_draw(cr, SET_BODY_PAD + 6, y + SET_HINT_LINE / 2.0, 11);
		n = set_wrap(cr, r->error, error_w(st), SET_HINT_SIZE, lines, 3);
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
	for (size_t i = 0; i < p->n; i++)
		if (p->rows[i]->custom && p->rows[i]->custom->overlay) p->rows[i]->custom->overlay(st, cr, p->rows[i]);
	if (!set_hist_view_shown(st)) set_draw_popup(st, cr);
	cairo_restore(cr);
	ui_scroll_draw(cr, &st->body, bw);
	cairo_restore(cr);
}

void set_draw(struct ui_window *w, cairo_t *cr, double width, double height) {
	struct settings_state *st = w->user;
	st->view_w = width;
	st->view_h = height;
	set_layout(st);
	ui_scroll_set(&st->body, set_content_h(st), height);
	double sw = set_sidebar_w(width);
	ui_scroll_set(&st->side, set_legend_y(st, height) + set_legend_h(), height);
	set_draw_sidebar(st, cr, sw, height);
	draw_body(st, cr);
	set_hist_view_draw(st, cr, width, height);
	if (set_hist_view_shown(st) && st->popup != POPUP_NONE) {
		cairo_save(cr);
		cairo_translate(cr, set_body_x(st), -st->body.offset);
		set_draw_popup(st, cr);
		cairo_restore(cr);
	}
	set_draw_toast(st, cr, height);
	set_draw_tip(st, cr);
	set_dialog_draw(st, cr, width, height);
	if (st->drag_over) set_draw_drop_overlay(st, cr, width, height);
}
