// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "cairo_util.h"
#include "cursor.h"
#include "ui/widgets.h"
#include "ui/window.h"
#include "ui_colors.h"
#include "ui_theme.h"
#include "wl/xkb.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void set_dialog_open(struct settings_state *st, const char *title, const char *text, char *arg) {
	set_dialog_close(st);
	struct set_dialog *d = &st->dialog;
	d->open = true;
	snprintf(d->title, sizeof d->title, "%s", title);
	snprintf(d->text, sizeof d->text, "%s", text);
	d->arg = arg;
	d->hover = d->press = SET_NONE;
	ui_window_damage(st->win);
}

void set_dialog_button(struct settings_state *st, const char *label, bool danger,
					   set_toast_action fn) {
	struct set_dialog *d = &st->dialog;
	if (d->n_btn == SET_DIALOG_BTNS) return;
	d->btn[d->n_btn++] = (struct set_dialog_btn){.label = label, .danger = danger, .fn = fn};
	d->focus = d->n_btn - 1;
}

void set_dialog_check(struct settings_state *st, const char *label, const char *key) {
	st->dialog.check_label = label;
	st->dialog.check_key = key;
}

void set_dialog_close(struct settings_state *st) {
	if (st->dialog.open) ui_window_damage(st->win);
	free(st->dialog.arg);
	st->dialog = (struct set_dialog){0};
}

static size_t hit_at(const struct settings_state *st, double x, double y) {
	const struct set_dialog *d = &st->dialog;
	for (size_t i = 0; i < d->n_btn; i++)
		if (set_rect_contains(&d->btn[i].rect, x, y)) return i;
	if (d->check_label && set_rect_contains(&d->check_rect, x, y)) return d->n_btn;
	return SET_NONE;
}

static void choose(struct settings_state *st, size_t i) {
	struct set_dialog *d = &st->dialog;
	if (i == d->n_btn) {
		d->check_on = !d->check_on;
		ui_window_damage(st->win);
		return;
	}
	if (i > d->n_btn) return;
	set_toast_action fn = d->btn[i].fn;
	struct set_row *check = d->btn[i].danger && d->check_on ? set_find_row(st, d->check_key) : NULL;
	char *arg = d->arg;
	d->arg = NULL;
	set_dialog_close(st);
	if (check) set_apply(st, check, "false");
	if (fn) fn(st, arg);
	free(arg);
}

bool set_dialog_press(struct settings_state *st, double x, double y, uint32_t button,
					  bool pressed) {
	struct set_dialog *d = &st->dialog;
	if (!d->open) return false;
	if (button != BTN_LEFT) return true;
	size_t hit = hit_at(st, x, y);
	if (pressed) {
		d->press = hit;
		ui_window_damage(st->win);
		return true;
	}
	size_t was = d->press;
	d->press = SET_NONE;
	ui_window_damage(st->win);
	if (hit != SET_NONE && hit == was) choose(st, hit);
	return true;
}

bool set_dialog_motion(struct settings_state *st, double x, double y) {
	struct set_dialog *d = &st->dialog;
	if (!d->open) return false;
	size_t hit = hit_at(st, x, y);
	st->win->content_cursor = hit == SET_NONE ? GCUR_DEFAULT : GCUR_HAND;
	if (hit != d->hover) {
		d->hover = hit;
		ui_window_damage(st->win);
	}
	return true;
}

bool set_dialog_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	struct set_dialog *d = &st->dialog;
	if (!d->open) return false;
	size_t stops = d->n_btn + (d->check_label ? 1 : 0);
	bool back = sym == XKB_KEY_Left || sym == XKB_KEY_ISO_Left_Tab ||
				(sym == XKB_KEY_Tab && (mods & KB_MOD_SHIFT));
	switch (sym) {
	case XKB_KEY_Escape:
		set_dialog_close(st);
		return true;
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab:
	case XKB_KEY_Left:
	case XKB_KEY_Right:
		if (stops) {
			size_t at = d->check_label ? (d->focus + 1) % stops : d->focus;
			at = (at + (back ? stops - 1 : 1)) % stops;
			d->focus = d->check_label ? (at + stops - 1) % stops : at;
		}
		ui_window_damage(st->win);
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_space:
		choose(st, d->focus);
		return true;
	default:
		return true;
	}
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
	size_t n = set_wrap(cr, d->text, w - pad * 2, 13, lines, 4);
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
		if (st->focus_visible && d->focus == d->n_btn) set_focus_ring(cr, &d->check_rect);
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
		if (st->focus_visible && d->focus == i) set_focus_ring(cr, &b->rect);
		bx -= gap;
	}
}
