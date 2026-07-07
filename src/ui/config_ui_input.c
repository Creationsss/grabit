// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/config_ui_internal.h"

#include "picker.h"
#include "ui/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

bool bool_on(const char *v) {
	return strcmp(v, "true") == 0;
}

static int cur_key(struct cfg_ui *u, int pos) {
	if (pos < 0 || pos >= u->tab_n[u->tab]) return -1;
	return u->tab_keys[u->tab][pos];
}

void tab_rect(int i, struct rect *r) {
	int tw = PANEL_W / NTAB;
	r->x = i * tw;
	r->y = 0;
	r->w = tw;
	r->h = TABBAR_H;
}

void toggle_rect(struct rect *r, double row_y) {
	r->w = TOGGLE_W;
	r->h = 24;
	r->x = PANEL_W - PAD - TOGGLE_W;
	r->y = (int)(row_y + (ROW_H - r->h) / 2.0);
}

void right_btn_rect(struct rect *r, double row_y) {
	r->x = PANEL_W - PAD - BTN_W;
	r->y = (int)row_y + (ROW_H - BTN_W) / 2;
	r->w = BTN_W;
	r->h = BTN_W;
}

void dec_inc_rects(struct rect *dec, struct rect *inc, double row_y) {
	right_btn_rect(inc, row_y);
	*dec = *inc;
	dec->x = PANEL_W - PAD - BTN_W - VAL_W - BTN_W;
}

void field_rect(struct rect *fr, bool is_path, double row_y) {
	int right = PANEL_W - PAD;
	if (is_path) right -= BTN_W + 6;
	fr->x = right - FIELD_W;
	fr->y = (int)row_y + (ROW_H - FIELD_H) / 2;
	fr->w = FIELD_W;
	fr->h = FIELD_H;
}

int cfg_ui_monitor_count(struct cfg_ui *u) {
	return u->n_monitors + 1;
}

const char *cfg_ui_monitor_value(struct cfg_ui *u, int idx) {
	return idx <= 0 ? "" : u->monitors[idx - 1];
}

const char *cfg_ui_monitor_label(struct cfg_ui *u, int idx) {
	return idx <= 0 ? "(auto)" : u->monitors[idx - 1];
}

static bool dd_is_service(struct cfg_ui *u) {
	return u->dd_open >= 0 && u->keys[u->dd_open].is_service;
}

int cfg_ui_dd_count(struct cfg_ui *u) {
	return dd_is_service(u) ? u->n_services : cfg_ui_monitor_count(u);
}

const char *cfg_ui_dd_value(struct cfg_ui *u, int idx) {
	return dd_is_service(u) ? u->services[idx] : cfg_ui_monitor_value(u, idx);
}

const char *cfg_ui_dd_label(struct cfg_ui *u, int idx) {
	return dd_is_service(u) ? u->services[idx] : cfg_ui_monitor_label(u, idx);
}

void dropdown_item_rect(struct cfg_ui *u, int idx, struct rect *r) {
	struct rect fr;
	field_rect(&fr, dd_is_service(u), u->dd_y);
	int list_h = cfg_ui_dd_count(u) * DD_ITEM_H;
	int below = fr.y + fr.h + 2;
	int top = (below + list_h <= u->panel_h - FOOTER_H) ? below : fr.y - 2 - list_h;
	r->x = fr.x;
	r->w = fr.w;
	r->h = DD_ITEM_H;
	r->y = top + idx * DD_ITEM_H;
}

static void set_val(struct cfg_ui *u, int i, const char *s) {
	char *n = strdup(s);
	if (!n) return;
	free(u->val[i]);
	u->val[i] = n;
	if (config_set(&u->cfg, u->keys[i].key, u->val[i]) == 0) config_save(&u->cfg);
	if (strcmp(u->keys[i].key, "service") == 0) cfg_ui_refresh_tabs(u);
}

static void on_pick_ready(struct ui_window *win, void *user) {
	struct cfg_ui *u = user;
	char picked[4096];
	int ok = grabit_pick_path_finish(u->pick_fd, (pid_t)u->pick_pid, picked, sizeof picked);
	ui_window_watch_fd(win, -1, NULL, NULL);
	close(u->pick_fd);
	u->pick_fd = -1;
	if (ok == 0 && u->pick_import) {
		char name[128];
		if (cfg_ui_import_sxcu(u, picked, name, sizeof name) == 0 && name[0])
			set_val(u, u->pick_key, name);
	} else if (ok == 0) {
		set_val(u, u->pick_key, picked);
	}
	u->pick_import = false;
	ui_window_redraw(win);
}

static void bool_toggle(struct cfg_ui *u, int i) {
	set_val(u, i, bool_on(u->val[i]) ? "false" : "true");
}

static void enum_cycle(struct cfg_ui *u, int i, int dir) {
	const struct cfg_key_desc *d = &u->keys[i];
	int n = 0;
	while (d->vals[n])
		n++;
	int total = n + (d->allow_empty ? 1 : 0);
	if (total <= 0) return;
	int cur = d->allow_empty ? n : 0;
	for (int k = 0; k < n; k++) {
		if (strcmp(d->vals[k], u->val[i]) == 0) {
			cur = k;
			break;
		}
	}
	int nx = ((cur + dir) % total + total) % total;
	set_val(u, i, nx < n ? d->vals[nx] : "");
}

static void monitor_cycle(struct cfg_ui *u, int i, int dir) {
	int count = cfg_ui_monitor_count(u);
	int cur = 0;
	for (int k = 0; k < count; k++) {
		if (strcmp(cfg_ui_monitor_value(u, k), u->val[i]) == 0) {
			cur = k;
			break;
		}
	}
	int nx = ((cur + dir) % count + count) % count;
	set_val(u, i, cfg_ui_monitor_value(u, nx));
}

static void int_step(struct cfg_ui *u, int i, long delta) {
	const struct cfg_key_desc *d = &u->keys[i];
	long cur = d->lo;
	if (u->val[i][0]) cur = strtol(u->val[i], NULL, 10);
	cur += delta;
	if (cur < d->lo) cur = d->lo;
	if (cur > d->hi) cur = d->hi;
	char buf[32];
	snprintf(buf, sizeof buf, "%ld", cur);
	set_val(u, i, buf);
}

static void change_row(struct cfg_ui *u, int pos, int dir, long mult) {
	int i = cur_key(u, pos);
	if (i < 0) return;
	switch (u->keys[i].kind) {
	case CFG_BOOL:
		bool_toggle(u, i);
		break;
	case CFG_ENUM:
		enum_cycle(u, i, dir);
		break;
	case CFG_INT:
		int_step(u, i, dir * mult);
		break;
	case CFG_STRING:
		if (u->keys[i].is_monitor) monitor_cycle(u, i, dir);
		break;
	}
}

static void begin_edit(struct cfg_ui *u, int i) {
	u->editing = i;
	snprintf(u->edit_buf, sizeof u->edit_buf, "%s", u->val[i]);
}

static void commit_edit(struct cfg_ui *u) {
	if (u->editing < 0) return;
	set_val(u, u->editing, u->edit_buf);
	u->editing = -1;
}

static void cancel_edit(struct cfg_ui *u) {
	u->editing = -1;
}

static void edit_insert(struct cfg_ui *u, const char *s) {
	size_t len = strlen(u->edit_buf), n = strlen(s);
	if (len + n + 1 > sizeof u->edit_buf) return;
	memcpy(u->edit_buf + len, s, n);
	u->edit_buf[len + n] = '\0';
}

static void edit_backspace(struct cfg_ui *u) {
	size_t len = strlen(u->edit_buf);
	while (len > 0) {
		unsigned char c = (unsigned char)u->edit_buf[--len];
		if ((c & 0xC0) != 0x80) break;
	}
	u->edit_buf[len] = '\0';
}

static void clamp_scroll(struct cfg_ui *u) {
	int max_scroll = u->tab_n[u->tab] - u->n_visible;
	if (max_scroll < 0) max_scroll = 0;
	if (u->scroll > max_scroll) u->scroll = max_scroll;
	if (u->scroll < 0) u->scroll = 0;
}

static void ensure_visible(struct cfg_ui *u) {
	if (u->sel < u->scroll) u->scroll = u->sel;
	if (u->sel >= u->scroll + u->n_visible) u->scroll = u->sel - u->n_visible + 1;
	clamp_scroll(u);
}

static void open_dropdown(struct cfg_ui *u, int pos) {
	u->dd_open = cur_key(u, pos);
	u->dd_y = TABBAR_H + (pos - u->scroll) * ROW_H;
	u->sel = pos;
	u->dd_hover = -1;
}

static void close_dropdown(struct cfg_ui *u) {
	u->dd_open = -1;
	u->dd_hover = -1;
}

void cfg_ui_key(struct ui_window *win, const struct ui_key_event *e, void *user) {
	struct cfg_ui *u = user;

	if (u->editing >= 0) {
		switch (e->sym) {
		case XKB_KEY_Escape:
			cancel_edit(u);
			break;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			commit_edit(u);
			break;
		case XKB_KEY_BackSpace:
			edit_backspace(u);
			break;
		default:
			if (e->utf8[0]) edit_insert(u, e->utf8);
			break;
		}
		ui_window_redraw(win);
		return;
	}

	if (u->dd_open >= 0) {
		close_dropdown(u);
		ui_window_redraw(win);
		return;
	}

	switch (e->sym) {
	case XKB_KEY_Escape:
		ui_window_close(win);
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter: {
		int i = cur_key(u, u->sel);
		if (i < 0) return;
		if (u->keys[i].kind != CFG_STRING)
			change_row(u, u->sel, +1, 1);
		else if (u->keys[i].is_monitor || u->keys[i].is_service)
			open_dropdown(u, u->sel);
		else
			begin_edit(u, i);
		break;
	}
	case XKB_KEY_Tab:
		u->tab = ((u->tab + (e->shift ? -1 : 1)) % NTAB + NTAB) % NTAB;
		u->sel = 0;
		u->scroll = 0;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_k:
		if (u->sel > 0) u->sel--;
		ensure_visible(u);
		break;
	case XKB_KEY_Down:
	case XKB_KEY_j:
		if (u->sel + 1 < u->tab_n[u->tab]) u->sel++;
		ensure_visible(u);
		break;
	case XKB_KEY_Left:
	case XKB_KEY_h:
		change_row(u, u->sel, -1, e->shift ? 10 : 1);
		break;
	case XKB_KEY_Right:
	case XKB_KEY_l:
	case XKB_KEY_space:
		change_row(u, u->sel, +1, e->shift ? 10 : 1);
		break;
	default:
		return;
	}
	ui_window_redraw(win);
}

enum hit { HIT_NONE,
		   HIT_TAB,
		   HIT_TOGGLE,
		   HIT_FIELD,
		   HIT_BROWSE,
		   HIT_DROPDOWN,
		   HIT_IMPORT,
		   HIT_DEC,
		   HIT_INC };

static enum hit hit_test(struct cfg_ui *u, int32_t x, int32_t y, int *out_pos) {
	if (y < TABBAR_H) return HIT_TAB;
	int row = (y - TABBAR_H) / ROW_H, pos = u->scroll + row;
	if (row < 0 || row >= u->n_visible || pos >= u->tab_n[u->tab]) return HIT_NONE;
	*out_pos = pos;
	const struct cfg_key_desc *d = &u->keys[u->tab_keys[u->tab][pos]];
	double ry = TABBAR_H + row * ROW_H;
	struct rect r;
	if (d->kind == CFG_BOOL) {
		toggle_rect(&r, ry);
		return rect_contains(r, x, y) ? HIT_TOGGLE : HIT_NONE;
	}
	if (d->kind == CFG_STRING) {
		if (d->is_monitor) {
			field_rect(&r, false, ry);
			return rect_contains(r, x, y) ? HIT_DROPDOWN : HIT_NONE;
		}
		if (d->is_service) {
			field_rect(&r, true, ry);
			if (rect_contains(r, x, y)) return HIT_DROPDOWN;
			right_btn_rect(&r, ry);
			return rect_contains(r, x, y) ? HIT_IMPORT : HIT_NONE;
		}
		field_rect(&r, d->is_path, ry);
		if (rect_contains(r, x, y)) return HIT_FIELD;
		if (d->is_path) {
			right_btn_rect(&r, ry);
			if (rect_contains(r, x, y)) return HIT_BROWSE;
		}
		return HIT_NONE;
	}
	struct rect dec, inc;
	dec_inc_rects(&dec, &inc, ry);
	if (rect_contains(dec, x, y)) return HIT_DEC;
	if (rect_contains(inc, x, y)) return HIT_INC;
	return HIT_NONE;
}

void cfg_ui_pointer(struct ui_window *win, const struct ui_pointer_event *e, void *user) {
	struct cfg_ui *u = user;

	if (e->kind == UI_PTR_AXIS) {
		u->scroll += e->axis > 0 ? 1 : -1;
		clamp_scroll(u);
		ui_window_redraw(win);
		return;
	}

	if (u->dd_open >= 0) {
		int count = cfg_ui_dd_count(u), over = -1;
		for (int k = 0; k < count; k++) {
			struct rect ir;
			dropdown_item_rect(u, k, &ir);
			if (rect_contains(ir, e->x, e->y)) {
				over = k;
				break;
			}
		}
		if (e->kind == UI_PTR_MOTION) {
			ui_window_set_cursor(win, over >= 0 ? UI_CURSOR_HAND : UI_CURSOR_DEFAULT);
			if (over != u->dd_hover) {
				u->dd_hover = over;
				ui_window_redraw(win);
			}
			return;
		}
		if (e->kind == UI_PTR_BUTTON && e->pressed && e->button == BTN_LEFT) {
			if (over >= 0) set_val(u, u->dd_open, cfg_ui_dd_value(u, over));
			close_dropdown(u);
			ui_window_redraw(win);
		}
		return;
	}

	int pos = -1;
	enum hit h = hit_test(u, e->x, e->y, &pos);

	if (e->kind == UI_PTR_MOTION) {
		enum ui_cursor c = h == HIT_FIELD  ? UI_CURSOR_TEXT
						   : h == HIT_NONE ? UI_CURSOR_DEFAULT
										   : UI_CURSOR_HAND;
		ui_window_set_cursor(win, c);
		if (pos >= 0 && pos != u->sel && u->editing < 0) {
			u->sel = pos;
			ui_window_redraw(win);
		}
		return;
	}

	if (e->kind != UI_PTR_BUTTON || !e->pressed || e->button != BTN_LEFT) return;
	if (u->editing >= 0) commit_edit(u);

	if (h == HIT_TAB) {
		for (int t = 0; t < NTAB; t++) {
			struct rect tr;
			tab_rect(t, &tr);
			if (rect_contains(tr, e->x, e->y) && t != u->tab) {
				u->tab = t;
				u->sel = 0;
				u->scroll = 0;
				break;
			}
		}
		ui_window_redraw(win);
		return;
	}

	if (h != HIT_NONE) {
		u->sel = pos;
		int i = cur_key(u, pos);
		switch (h) {
		case HIT_TOGGLE:
			bool_toggle(u, i);
			break;
		case HIT_FIELD:
			begin_edit(u, i);
			break;
		case HIT_BROWSE:
		case HIT_IMPORT: {
			if (u->pick_fd >= 0) break;
			pid_t pid = 0;
			int fd = grabit_pick_path_start(h == HIT_BROWSE && u->keys[i].is_dir, &pid);
			if (fd >= 0) {
				u->pick_fd = fd;
				u->pick_pid = (int)pid;
				u->pick_key = i;
				u->pick_import = (h == HIT_IMPORT);
				ui_window_watch_fd(win, fd, on_pick_ready, u);
			}
			break;
		}
		case HIT_DROPDOWN:
			open_dropdown(u, pos);
			break;
		case HIT_DEC:
			change_row(u, pos, -1, 1);
			break;
		case HIT_INC:
			change_row(u, pos, +1, 1);
			break;
		default:
			break;
		}
	}
	ui_window_redraw(win);
}
