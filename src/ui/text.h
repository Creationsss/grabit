// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_TEXT_H
#define GRABIT_UI_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#define UI_TEXT_MAX 1024
#define UI_TEXT_SIZE 13.0

struct ui_text {
	char buf[UI_TEXT_MAX];
	size_t len;
	size_t caret;
	size_t anchor;
	double scroll;
};

enum ui_text_result {
	UI_TEXT_IGNORED,
	UI_TEXT_HANDLED,
	UI_TEXT_COMMIT,
	UI_TEXT_CANCEL,
};

void ui_text_set(struct ui_text *t, const char *s);
void ui_text_select_all(struct ui_text *t);
void ui_text_select_word(struct ui_text *t);
enum ui_text_result ui_text_key(struct ui_text *t, xkb_keysym_t sym, uint8_t mods,
								bool masked);
void ui_text_click(struct ui_text *t, double x, bool masked, bool extend);
void ui_text_draw(cairo_t *cr, struct ui_text *t, double x, double y, double w,
				  double h, double inset, bool masked, const char *shown,
				  const char *placeholder);

#endif
