// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_WINDOW_H
#define GRABIT_UI_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

struct grabit_wl_state;
struct grabit_output;
struct ui_window;

enum ui_cursor {
	UI_CURSOR_DEFAULT,
	UI_CURSOR_HAND,
	UI_CURSOR_TEXT,
};

enum ui_pointer_kind {
	UI_PTR_MOTION,
	UI_PTR_BUTTON,
	UI_PTR_LEAVE,
	UI_PTR_AXIS,
};

struct ui_pointer_event {
	enum ui_pointer_kind kind;
	int32_t x, y;
	uint32_t button;
	bool pressed;
	bool shift;
	double axis;
};

struct ui_key_event {
	xkb_keysym_t sym;
	char utf8[8];
	bool ctrl, shift;
};

struct ui_window_opts {
	struct grabit_wl_state *wls;
	struct grabit_output *output;
	int32_t width, height;
	const char *name;
	const char *title;
	void *user;
	void (*on_draw)(cairo_t *cr, int32_t w, int32_t h, void *user);
	void (*on_pointer)(struct ui_window *win, const struct ui_pointer_event *e, void *user);
	void (*on_key)(struct ui_window *win, const struct ui_key_event *e, void *user);
};

struct ui_window *ui_window_create(const struct ui_window_opts *opts);
void ui_window_redraw(struct ui_window *w);
void ui_window_set_cursor(struct ui_window *w, enum ui_cursor c);
void ui_window_watch_fd(struct ui_window *w, int fd,
						void (*on_ready)(struct ui_window *, void *), void *user);
void ui_window_close(struct ui_window *w);
int ui_window_run(struct ui_window *w);
void ui_window_destroy(struct ui_window *w);

#endif
