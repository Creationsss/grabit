// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/text.h"

#include "clipboard/clipboard.h"
#include "ui/widgets.h"
#include "ui_colors.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <stdlib.h>
#include <string.h>

#define UI_TEXT_PAD 8.0
#define MASK_GLYPH "\xe2\x80\xa2"

static bool is_word(char c) {
	return c != ' ' && c != '/' && c != '.' && c != ',' && c != '-' && c != '_';
}

static size_t prev_word(const char *s, size_t at) {
	while (at > 0 && !is_word(s[at - 1]))
		at--;
	while (at > 0 && is_word(s[at - 1]))
		at--;
	return at;
}

static size_t next_word(const char *s, size_t len, size_t at) {
	while (at < len && !is_word(s[at]))
		at++;
	while (at < len && is_word(s[at]))
		at++;
	return at;
}

void ui_text_set(struct ui_text *t, const char *s) {
	size_t n = s ? strlen(s) : 0;
	n = grabit_utf8_valid_prefix(s, n < sizeof t->buf ? n : sizeof t->buf - 1);
	if (n) memcpy(t->buf, s, n);
	t->buf[n] = '\0';
	t->len = n;
	t->caret = n;
	t->anchor = n;
	t->scroll = 0;
}

void ui_text_select_all(struct ui_text *t) {
	t->anchor = 0;
	t->caret = t->len;
}

void ui_text_select_word(struct ui_text *t) {
	size_t a = t->caret, b = t->caret;
	while (a > 0 && is_word(t->buf[a - 1]))
		a--;
	while (b < t->len && is_word(t->buf[b]))
		b++;
	t->anchor = a;
	t->caret = b;
}

static size_t sel_lo(const struct ui_text *t) {
	return t->anchor < t->caret ? t->anchor : t->caret;
}

static size_t sel_hi(const struct ui_text *t) {
	return t->anchor > t->caret ? t->anchor : t->caret;
}

static void erase(struct ui_text *t, size_t from, size_t to) {
	if (to > from) {
		memmove(t->buf + from, t->buf + to, t->len - to + 1);
		t->len -= to - from;
	}
	t->caret = from;
	t->anchor = from;
}

static bool erase_selection(struct ui_text *t) {
	if (t->anchor == t->caret) return false;
	erase(t, sel_lo(t), sel_hi(t));
	return true;
}

static void insert(struct ui_text *t, const char *s, size_t n) {
	erase_selection(t);
	size_t room = sizeof t->buf - 1 - t->len;
	if (n > room) n = grabit_utf8_valid_prefix(s, room);
	memmove(t->buf + t->caret + n, t->buf + t->caret, t->len - t->caret + 1);
	memcpy(t->buf + t->caret, s, n);
	t->len += n;
	t->caret += n;
	t->anchor = t->caret;
}

static void paste(struct ui_text *t) {
	char *clip = clipboard_get_text();
	if (!clip) return;
	insert(t, clip, grabit_text_one_line(clip, sizeof t->buf));
	free(clip);
}

static void copy_selection(const struct ui_text *t) {
	char tmp[UI_TEXT_MAX];
	size_t lo = sel_lo(t), n = sel_hi(t) - lo;
	if (n == 0) return;
	memcpy(tmp, t->buf + lo, n);
	tmp[n] = '\0';
	clipboard_set_text(tmp);
}

static void move_caret(struct ui_text *t, size_t to, bool extend) {
	t->caret = to;
	if (!extend) t->anchor = to;
}

static size_t step(const struct ui_text *t, bool back, bool word) {
	if (back) return word ? prev_word(t->buf, t->caret) : grabit_utf8_prev(t->buf, t->caret);
	return word ? next_word(t->buf, t->len, t->caret)
				: grabit_utf8_next(t->buf, t->len, t->caret);
}

static void arrow(struct ui_text *t, bool back, bool word, bool extend) {
	if (!extend && t->anchor != t->caret && !word) {
		move_caret(t, back ? sel_lo(t) : sel_hi(t), false);
		return;
	}
	move_caret(t, step(t, back, word), extend);
}

static bool ctrl_key(struct ui_text *t, xkb_keysym_t sym, bool masked) {
	switch (xkb_keysym_to_lower(sym)) {
	case XKB_KEY_a:
		ui_text_select_all(t);
		return true;
	case XKB_KEY_c:
		if (!masked) copy_selection(t);
		return true;
	case XKB_KEY_x:
		if (masked) return true;
		copy_selection(t);
		erase_selection(t);
		return true;
	case XKB_KEY_v:
		paste(t);
		return true;
	case XKB_KEY_u:
		erase(t, 0, t->caret);
		return true;
	default:
		return false;
	}
}

enum ui_text_result ui_text_key(struct ui_text *t, xkb_keysym_t sym, uint8_t mods,
								bool masked) {
	bool ctrl = (mods & KB_MOD_CTRL) != 0;
	bool shift = (mods & KB_MOD_SHIFT) != 0;
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		return UI_TEXT_COMMIT;
	case XKB_KEY_Escape:
		return UI_TEXT_CANCEL;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
	case XKB_KEY_Up:
	case XKB_KEY_Down:
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down:
		return UI_TEXT_IGNORED;
	case XKB_KEY_Left:
	case XKB_KEY_KP_Left:
	case XKB_KEY_Right:
	case XKB_KEY_KP_Right:
		arrow(t, sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left, ctrl, shift);
		return UI_TEXT_HANDLED;
	case XKB_KEY_Home:
	case XKB_KEY_KP_Home:
		move_caret(t, 0, shift);
		return UI_TEXT_HANDLED;
	case XKB_KEY_End:
	case XKB_KEY_KP_End:
		move_caret(t, t->len, shift);
		return UI_TEXT_HANDLED;
	case XKB_KEY_BackSpace:
	case XKB_KEY_Delete:
	case XKB_KEY_KP_Delete:
		if (!erase_selection(t)) {
			size_t to = step(t, sym == XKB_KEY_BackSpace, ctrl);
			if (to < t->caret)
				erase(t, to, t->caret);
			else
				erase(t, t->caret, to);
		}
		return UI_TEXT_HANDLED;
	default:
		break;
	}
	if (ctrl) return ctrl_key(t, sym, masked) ? UI_TEXT_HANDLED : UI_TEXT_IGNORED;
	if (mods & (KB_MOD_ALT | KB_MOD_SUPER)) return UI_TEXT_IGNORED;
	char utf8[8];
	int n = xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	if (n <= 1 || (unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f) return UI_TEXT_IGNORED;
	insert(t, utf8, (size_t)n - 1);
	return UI_TEXT_HANDLED;
}

static size_t display_text(const char *src, size_t len, bool masked, char *out,
						   size_t cap, size_t caret, size_t *caret_out) {
	if (!masked) {
		size_t n = grabit_utf8_valid_prefix(src, len < cap - 1 ? len : cap - 1);
		memcpy(out, src, n);
		out[n] = '\0';
		if (caret_out) *caret_out = caret < n ? caret : n;
		return n;
	}
	size_t n = 0;
	for (size_t i = 0; i < len && n + 4 < cap; i = grabit_utf8_next(src, len, i)) {
		if (caret_out && i == caret) *caret_out = n;
		memcpy(out + n, MASK_GLYPH, 3);
		n += 3;
	}
	if (caret_out && caret >= len) *caret_out = n;
	out[n] = '\0';
	return n;
}

static double advance(cairo_t *cr, const char *s, size_t n) {
	char tmp[UI_TEXT_MAX * 3 + 4];
	if (n >= sizeof tmp) n = sizeof tmp - 1;
	memcpy(tmp, s, n);
	tmp[n] = '\0';
	cairo_text_extents_t ext;
	cairo_text_extents(cr, tmp, &ext);
	return ext.x_advance;
}

void ui_text_click(struct ui_text *t, double x, bool masked, bool extend) {
	cairo_t *cr = ui_measure_cr();
	ui_set_font(cr, UI_TEXT_SIZE, CAIRO_FONT_WEIGHT_NORMAL);
	char shown[UI_TEXT_MAX * 3 + 4];
	display_text(t->buf, t->len, masked, shown, sizeof shown, 0, NULL);
	double want = x - UI_TEXT_PAD + t->scroll;
	size_t best = 0, cps = 0;
	double best_d = want < 0 ? -want : want;
	for (size_t i = 0; i < t->len;) {
		i = grabit_utf8_next(t->buf, t->len, i);
		cps++;
		double d = advance(cr, shown, masked ? cps * 3 : i) - want;
		if (d < 0) d = -d;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	move_caret(t, best, extend);
}

void ui_text_draw(cairo_t *cr, struct ui_text *t, double x, double y, double w,
				  double h, double inset, bool masked, const char *shown_value,
				  const char *placeholder) {
	bool focused = t != NULL;
	ui_field_bg(cr, x, y, w, h, focused);
	w -= inset;

	cairo_save(cr);
	cairo_rectangle(cr, x + 2, y, w - 4, h);
	cairo_clip(cr);
	ui_set_font(cr, UI_TEXT_SIZE, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	double base = ui_snap_y(cr, y + (h - (fe.ascent + fe.descent)) / 2.0 + fe.ascent);
	double inner = w - UI_TEXT_PAD * 2;

	char shown[UI_TEXT_MAX * 3 + 4];
	const char *src = focused ? t->buf : shown_value ? shown_value
													 : "";
	size_t len = focused ? t->len : strlen(src);
	size_t caret_off = 0;
	size_t n = display_text(src, len, masked, shown, sizeof shown, focused ? t->caret : 0,
							&caret_off);

	double cx = advance(cr, shown, caret_off);
	double scroll = 0;
	if (focused) {
		if (cx - t->scroll > inner) t->scroll = cx - inner;
		if (cx - t->scroll < 0) t->scroll = cx;
		double full = advance(cr, shown, n);
		if (full - t->scroll < inner && t->scroll > 0) {
			t->scroll = full - inner;
			if (t->scroll < 0) t->scroll = 0;
		}
		scroll = t->scroll;
	}
	double tx = x + UI_TEXT_PAD - scroll;

	if (n == 0 && placeholder && *placeholder) {
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_ITALIC,
							   CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_source_rgba(cr, GUI_C_INK, 0.38);
		cairo_move_to(cr, tx, base);
		cairo_show_text(cr, placeholder);
	} else {
		if (focused && t->anchor != t->caret) {
			size_t lo_off = 0, hi_off = 0;
			display_text(t->buf, t->len, masked, shown, sizeof shown, sel_lo(t), &lo_off);
			display_text(t->buf, t->len, masked, shown, sizeof shown, sel_hi(t), &hi_off);
			double sx = advance(cr, shown, lo_off);
			grabit_ui_accent(cr, 0.35);
			cairo_rectangle(cr, tx + sx, y + 5, advance(cr, shown, hi_off) - sx, h - 10);
			cairo_fill(cr);
		}
		cairo_set_source_rgba(cr, GUI_C_INK, 0.92);
		cairo_move_to(cr, tx, base);
		cairo_show_text(cr, shown);
	}
	if (focused) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.9);
		cairo_rectangle(cr, tx + cx, y + 6, 1.2, h - 12);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}
