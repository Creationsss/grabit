// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "cursor.h"
#include "ui/window.h"
#include "wl/xkb.h"

#include <linux/input-event-codes.h>
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
	struct set_row *check = fn && d->check_on ? set_find_row(st, d->check_key) : NULL;
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
