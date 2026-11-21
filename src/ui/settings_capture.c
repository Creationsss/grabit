// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "ui/window.h"

#include <linux/input-event-codes.h>

void set_capture_start(struct settings_state *st, struct set_row *r) {
	st->capture.row = r;
	st->capture.binds.n = 0;
	r->error[0] = '\0';
	ui_window_damage(st->win);
}

void set_capture_end(struct settings_state *st, bool save) {
	struct set_row *r = st->capture.row;
	if (!r) return;
	st->capture.row = NULL;
	ui_window_damage(st->win);
	if (!save) return;
	char text[256];
	region_keybind_list_join(&st->capture.binds, text, sizeof text);
	if (text[0]) set_apply(st, r, text);
}

bool set_capture_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	if (!st->capture.row) return false;
	switch (region_capture_key(&st->capture.binds, sym, mods)) {
	case REGION_CAPTURE_CANCEL:
		set_capture_end(st, false);
		break;
	case REGION_CAPTURE_SAVE:
		set_capture_end(st, true);
		break;
	case REGION_CAPTURE_CHANGED:
		ui_window_damage(st->win);
		break;
	case REGION_CAPTURE_IGNORED:
		break;
	}
	return true;
}

bool set_capture_button(struct settings_state *st, uint32_t button, bool pressed) {
	if (!st->capture.row) return false;
	if (!pressed) return true;
	if (button == BTN_LEFT) {
		set_capture_end(st, false);
		return true;
	}
	struct keybind b = {.is_button = true, .button = button};
	if (region_keybind_list_add(&st->capture.binds, &b)) ui_window_damage(st->win);
	return true;
}
