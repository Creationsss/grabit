// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UI_CONFIG_UI_INTERNAL_H
#define GRABIT_UI_CONFIG_UI_INTERNAL_H

#include "config.h"
#include "config_internal.h"
#include "region/region.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

struct ui_window;
struct ui_key_event;
struct ui_pointer_event;

enum {
	NTAB = 6,
	PANEL_W = 580,
	PAD = 18,
	TABBAR_H = 44,
	FOOTER_H = 30,
	ROW_H = 36,
	BTN_W = 28,
	TOGGLE_W = 50,
	VAL_W = 130,
	FIELD_W = 190,
	FIELD_H = 26,
	DD_ITEM_H = 26,
};

struct cfg_ui {
	struct config cfg;
	const struct cfg_key_desc *keys;
	size_t n_keys;
	char **val;

	int *tab_keys[NTAB];
	int tab_n[NTAB];
	int tab;
	int sel;
	int scroll;
	int n_visible;

	int editing;
	char edit_buf[512];

	int pick_fd;
	int pick_pid;
	int pick_key;

	const char **monitors;
	int n_monitors;
	int dd_open;
	double dd_y;
	int panel_h;

	struct ui_window *win;
};

int cfg_ui_monitor_count(struct cfg_ui *u);
const char *cfg_ui_monitor_value(struct cfg_ui *u, int idx);
const char *cfg_ui_monitor_label(struct cfg_ui *u, int idx);
void dropdown_item_rect(struct cfg_ui *u, int idx, struct rect *r);

bool bool_on(const char *v);
void tab_rect(int i, struct rect *r);
void toggle_rect(struct rect *r, double row_y);
void right_btn_rect(struct rect *r, double row_y);
void dec_inc_rects(struct rect *dec, struct rect *inc, double row_y);
void field_rect(struct rect *fr, bool is_path, double row_y);

void cfg_ui_draw(cairo_t *cr, int32_t w, int32_t h, void *user);
void cfg_ui_key(struct ui_window *win, const struct ui_key_event *e, void *user);
void cfg_ui_pointer(struct ui_window *win, const struct ui_pointer_event *e, void *user);

#endif
