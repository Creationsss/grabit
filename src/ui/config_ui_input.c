// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/config_ui_internal.h"

#include "clipboard/clipboard.h"
#include "notify_test.h"
#include "picker.h"
#include "ui/window.h"

#include <cairo/cairo.h>

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

bool test_btn_rect(struct cfg_ui *u, struct rect *r) {
	if (u->tab != TAB_NOTIFY) return false;
	int row = u->tab_n[u->tab] - u->scroll;
	if (row < 0 || row >= u->n_visible) return false;
	r->w = 200;
	r->h = FIELD_H;
	r->x = (PANEL_W - r->w) / 2;
	r->y = TABBAR_H + row * ROW_H + (ROW_H - r->h) / 2;
	return true;
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

static bool dd_is_enum(struct cfg_ui *u) {
	return u->dd_open >= 0 && u->keys[u->dd_open].kind == CFG_ENUM;
}

static int enum_val_count(const struct cfg_key_desc *d) {
	int n = 0;
	while (d->vals[n])
		n++;
	return n + (d->allow_empty ? 1 : 0);
}

int cfg_ui_dd_count(struct cfg_ui *u) {
	if (dd_is_enum(u)) return enum_val_count(&u->keys[u->dd_open]);
	if (dd_is_service(u)) return u->n_services;
	return cfg_ui_monitor_count(u);
}

const char *cfg_ui_dd_value(struct cfg_ui *u, int idx) {
	if (dd_is_enum(u)) {
		const struct cfg_key_desc *d = &u->keys[u->dd_open];
		int n = 0;
		while (d->vals[n])
			n++;
		return idx < n ? d->vals[idx] : "";
	}
	if (dd_is_service(u)) return u->services[idx];
	return cfg_ui_monitor_value(u, idx);
}

const char *cfg_ui_dd_label(struct cfg_ui *u, int idx) {
	if (dd_is_enum(u)) {
		const char *v = cfg_ui_dd_value(u, idx);
		return v[0] ? v : "(none)";
	}
	if (dd_is_service(u)) return u->services[idx];
	return cfg_ui_monitor_label(u, idx);
}

void dropdown_item_rect(struct cfg_ui *u, int idx, struct rect *r) {
	struct rect fr;
	field_rect(&fr, dd_is_service(u), u->dd_y);
	int list_h = cfg_ui_dd_count(u) * DD_ITEM_H;
	int avail_top = TABBAR_H, avail_bot = u->panel_h - FOOTER_H;
	int below = fr.y + fr.h + 2;
	int top = below + list_h <= avail_bot ? below : fr.y - 2 - list_h;
	if (top + list_h > avail_bot) top = avail_bot - list_h;
	if (top < avail_top) top = avail_top;
	r->x = fr.x;
	r->w = fr.w;
	r->h = DD_ITEM_H;
	r->y = top + idx * DD_ITEM_H;
}

static void set_val(struct cfg_ui *u, int i, const char *s) {
	if (config_set(&u->cfg, u->keys[i].key, s) != 0) return;
	config_save(&u->cfg);
	const char *stored = config_get(&u->cfg, u->keys[i].key);
	char *n = strdup(stored ? stored : s);
	if (!n) return;
	free(u->val[i]);
	u->val[i] = n;
	if (u->keys[i].is_service) cfg_ui_refresh_tabs(u);
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
		if (cfg_ui_import_sxcu(u, picked, name, sizeof name) == 0) {
			if (name[0]) set_val(u, u->pick_key, name);
			snprintf(u->status, sizeof u->status, "imported %s", name[0] ? name : "uploader");
		} else {
			snprintf(u->status, sizeof u->status, "couldn't import: not a valid .sxcu");
		}
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

static void service_cycle(struct cfg_ui *u, int i, int dir) {
	if (u->n_services <= 0) return;
	int cur = 0;
	for (int k = 0; k < u->n_services; k++) {
		if (strcmp(u->services[k], u->val[i]) == 0) {
			cur = k;
			break;
		}
	}
	int nx = ((cur + dir) % u->n_services + u->n_services) % u->n_services;
	set_val(u, i, u->services[nx]);
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
		if (u->keys[i].is_monitor)
			monitor_cycle(u, i, dir);
		else if (u->keys[i].is_service)
			service_cycle(u, i, dir);
		break;
	}
}

static int u8_prev(const char *s, int i) {
	if (i <= 0) return 0;
	i--;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
		i--;
	return i;
}

static int u8_next(const char *s, int i) {
	int n = (int)strlen(s);
	if (i >= n) return n;
	i++;
	while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80)
		i++;
	return i;
}

static int sel_lo(struct cfg_ui *u) {
	return u->edit_cursor < u->edit_anchor ? u->edit_cursor : u->edit_anchor;
}

static int sel_hi(struct cfg_ui *u) {
	return u->edit_cursor > u->edit_anchor ? u->edit_cursor : u->edit_anchor;
}

static bool has_sel(struct cfg_ui *u) {
	return u->edit_cursor != u->edit_anchor;
}

static void edit_push_undo(struct cfg_ui *u) {
	if (u->edit_undo_n == EDIT_UNDO_MAX) {
		memmove(u->edit_undo[0], u->edit_undo[1], sizeof u->edit_undo - sizeof u->edit_undo[0]);
		memmove(u->edit_undo_cur, u->edit_undo_cur + 1,
				sizeof u->edit_undo_cur - sizeof u->edit_undo_cur[0]);
		u->edit_undo_n--;
	}
	memcpy(u->edit_undo[u->edit_undo_n], u->edit_buf, sizeof u->edit_buf);
	u->edit_undo_cur[u->edit_undo_n] = u->edit_cursor;
	u->edit_undo_n++;
}

static void edit_undo(struct cfg_ui *u) {
	if (u->edit_undo_n == 0) return;
	u->edit_undo_n--;
	memcpy(u->edit_buf, u->edit_undo[u->edit_undo_n], sizeof u->edit_buf);
	u->edit_cursor = u->edit_undo_cur[u->edit_undo_n];
	int len = (int)strlen(u->edit_buf);
	if (u->edit_cursor > len) u->edit_cursor = len;
	u->edit_anchor = u->edit_cursor;
}

static void edit_delete_sel(struct cfg_ui *u) {
	int lo = sel_lo(u), hi = sel_hi(u);
	if (lo == hi) return;
	int len = (int)strlen(u->edit_buf);
	memmove(u->edit_buf + lo, u->edit_buf + hi, len - hi + 1);
	u->edit_cursor = lo;
	u->edit_anchor = lo;
}

static cairo_t *measure_ctx(void) {
	static cairo_t *cr = NULL;
	if (!cr) {
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cr = cairo_create(s);
		cairo_surface_destroy(s);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 13);
	}
	return cr;
}

double cfg_ui_edit_prefix_w(const char *s, int bytes) {
	if (bytes <= 0) return 0;
	char tmp[512];
	if (bytes > (int)sizeof tmp - 1) bytes = (int)sizeof tmp - 1;
	memcpy(tmp, s, (size_t)bytes);
	tmp[bytes] = '\0';
	cairo_text_extents_t e;
	cairo_text_extents(measure_ctx(), tmp, &e);
	return e.x_advance;
}

static int edit_offset_from_x(const char *s, double rel_x) {
	int len = (int)strlen(s), best = 0, i = 0;
	double bestd = 1e9;
	for (;;) {
		double d = cfg_ui_edit_prefix_w(s, i) - rel_x;
		if (d < 0) d = -d;
		if (d < bestd) {
			bestd = d;
			best = i;
		}
		if (i >= len) break;
		i = u8_next(s, i);
	}
	return best;
}

static void edit_cursor_at_x(struct cfg_ui *u, int32_t x, bool extend) {
	int i = u->editing;
	if (i < 0) return;
	struct rect fr;
	double row_y = TABBAR_H + (u->sel - u->scroll) * ROW_H;
	field_rect(&fr, u->keys[i].is_path, row_y);
	double avail = fr.w - 14;
	double cur_x = cfg_ui_edit_prefix_w(u->edit_buf, u->edit_cursor);
	double scroll = cur_x > avail ? cur_x - avail : 0;
	int off = edit_offset_from_x(u->edit_buf, (double)x - (fr.x + 7) + scroll);
	u->edit_cursor = off;
	if (!extend) u->edit_anchor = off;
}

static void begin_edit(struct cfg_ui *u, int i) {
	u->editing = i;
	snprintf(u->edit_buf, sizeof u->edit_buf, "%s", u->val[i]);
	u->edit_cursor = (int)strlen(u->edit_buf);
	u->edit_anchor = u->edit_cursor;
	u->edit_undo_n = 0;
}

static void commit_edit(struct cfg_ui *u) {
	if (u->editing < 0) return;
	set_val(u, u->editing, u->edit_buf);
	u->editing = -1;
	u->edit_selecting = false;
}

static void cancel_edit(struct cfg_ui *u) {
	u->editing = -1;
	u->edit_selecting = false;
}

static void edit_insert(struct cfg_ui *u, const char *s) {
	size_t n = strlen(s);
	int lo = sel_lo(u), hi = sel_hi(u);
	int len = (int)strlen(u->edit_buf);
	if ((size_t)(len - (hi - lo)) + n + 1 > sizeof u->edit_buf) return;
	edit_push_undo(u);
	edit_delete_sel(u);
	len = (int)strlen(u->edit_buf);
	memmove(u->edit_buf + u->edit_cursor + n, u->edit_buf + u->edit_cursor,
			(size_t)(len - u->edit_cursor + 1));
	memcpy(u->edit_buf + u->edit_cursor, s, n);
	u->edit_cursor += (int)n;
	u->edit_anchor = u->edit_cursor;
}

static void edit_backspace(struct cfg_ui *u) {
	if (has_sel(u)) {
		edit_push_undo(u);
		edit_delete_sel(u);
		return;
	}
	if (u->edit_cursor == 0) return;
	edit_push_undo(u);
	int prev = u8_prev(u->edit_buf, u->edit_cursor);
	int len = (int)strlen(u->edit_buf);
	memmove(u->edit_buf + prev, u->edit_buf + u->edit_cursor,
			(size_t)(len - u->edit_cursor + 1));
	u->edit_cursor = prev;
	u->edit_anchor = prev;
}

static void edit_delete_fwd(struct cfg_ui *u) {
	if (has_sel(u)) {
		edit_push_undo(u);
		edit_delete_sel(u);
		return;
	}
	int len = (int)strlen(u->edit_buf);
	if (u->edit_cursor >= len) return;
	edit_push_undo(u);
	int next = u8_next(u->edit_buf, u->edit_cursor);
	memmove(u->edit_buf + u->edit_cursor, u->edit_buf + next, (size_t)(len - next + 1));
}

static void edit_select_all(struct cfg_ui *u) {
	u->edit_anchor = 0;
	u->edit_cursor = (int)strlen(u->edit_buf);
}

static void edit_copy(struct cfg_ui *u) {
	int lo = sel_lo(u), hi = sel_hi(u);
	if (lo == hi) {
		lo = 0;
		hi = (int)strlen(u->edit_buf);
	}
	if (hi == lo) return;
	char tmp[512];
	int n = hi - lo;
	memcpy(tmp, u->edit_buf + lo, (size_t)n);
	tmp[n] = '\0';
	clipboard_set_text(tmp);
}

static void edit_cut(struct cfg_ui *u) {
	if (!has_sel(u)) edit_select_all(u);
	if (!has_sel(u)) return;
	edit_copy(u);
	edit_push_undo(u);
	edit_delete_sel(u);
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

static int dd_index_of(struct cfg_ui *u, const char *val) {
	int count = cfg_ui_dd_count(u);
	for (int k = 0; k < count; k++)
		if (strcmp(cfg_ui_dd_value(u, k), val) == 0) return k;
	return -1;
}

static void open_dropdown(struct cfg_ui *u, int pos) {
	u->dd_open = cur_key(u, pos);
	u->dd_y = TABBAR_H + (pos - u->scroll) * ROW_H;
	u->sel = pos;
	int idx = dd_index_of(u, u->val[u->dd_open]);
	u->dd_hover = idx >= 0 ? idx : 0;
}

static void close_dropdown(struct cfg_ui *u) {
	u->dd_open = -1;
	u->dd_hover = -1;
}

void cfg_ui_key(struct ui_window *win, const struct ui_key_event *e, void *user) {
	struct cfg_ui *u = user;
	u->status[0] = '\0';

	if (u->editing >= 0) {
		if (e->ctrl) {
			switch (e->sym) {
			case XKB_KEY_a:
			case XKB_KEY_A:
				edit_select_all(u);
				break;
			case XKB_KEY_c:
			case XKB_KEY_C:
				edit_copy(u);
				break;
			case XKB_KEY_x:
			case XKB_KEY_X:
				edit_cut(u);
				break;
			case XKB_KEY_z:
			case XKB_KEY_Z:
				edit_undo(u);
				break;
			default:
				break;
			}
			ui_window_redraw(win);
			return;
		}
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
		case XKB_KEY_Delete:
			edit_delete_fwd(u);
			break;
		case XKB_KEY_Left:
			u->edit_cursor = u8_prev(u->edit_buf, u->edit_cursor);
			if (!e->shift) u->edit_anchor = u->edit_cursor;
			break;
		case XKB_KEY_Right:
			u->edit_cursor = u8_next(u->edit_buf, u->edit_cursor);
			if (!e->shift) u->edit_anchor = u->edit_cursor;
			break;
		case XKB_KEY_Home:
			u->edit_cursor = 0;
			if (!e->shift) u->edit_anchor = 0;
			break;
		case XKB_KEY_End:
			u->edit_cursor = (int)strlen(u->edit_buf);
			if (!e->shift) u->edit_anchor = u->edit_cursor;
			break;
		default:
			if (e->utf8[0] && (unsigned char)e->utf8[0] >= 0x20) edit_insert(u, e->utf8);
			break;
		}
		ui_window_redraw(win);
		return;
	}

	if (u->dd_open >= 0) {
		int count = cfg_ui_dd_count(u);
		switch (e->sym) {
		case XKB_KEY_Up:
		case XKB_KEY_k:
			if (count > 0) u->dd_hover = (u->dd_hover - 1 + count) % count;
			break;
		case XKB_KEY_Down:
		case XKB_KEY_j:
			if (count > 0) u->dd_hover = (u->dd_hover + 1) % count;
			break;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
		case XKB_KEY_space:
			if (u->dd_hover >= 0 && u->dd_hover < count)
				set_val(u, u->dd_open, cfg_ui_dd_value(u, u->dd_hover));
			close_dropdown(u);
			break;
		default:
			close_dropdown(u);
			break;
		}
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
		if (u->keys[i].kind == CFG_ENUM ||
			(u->keys[i].kind == CFG_STRING && (u->keys[i].is_monitor || u->keys[i].is_service)))
			open_dropdown(u, u->sel);
		else if (u->keys[i].kind == CFG_STRING)
			begin_edit(u, i);
		else
			change_row(u, u->sel, +1, 1);
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
	if (d->kind == CFG_ENUM) {
		field_rect(&r, false, ry);
		return rect_contains(r, x, y) ? HIT_DROPDOWN : HIT_NONE;
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
		if (u->edit_selecting && u->editing >= 0) {
			edit_cursor_at_x(u, e->x, true);
			ui_window_redraw(win);
			return;
		}
		bool moved = e->x != u->ptr_x || e->y != u->ptr_y;
		u->ptr_x = e->x;
		u->ptr_y = e->y;
		enum ui_cursor c = h == HIT_FIELD  ? UI_CURSOR_TEXT
						   : h == HIT_NONE ? UI_CURSOR_DEFAULT
										   : UI_CURSOR_HAND;
		struct rect tb;
		if (test_btn_rect(u, &tb) && rect_contains(tb, e->x, e->y)) c = UI_CURSOR_HAND;
		ui_window_set_cursor(win, c);
		if (moved && pos >= 0 && pos != u->sel && u->editing < 0) {
			u->sel = pos;
			ui_window_redraw(win);
		}
		return;
	}

	if (e->kind == UI_PTR_BUTTON && !e->pressed) {
		u->edit_selecting = false;
		return;
	}
	if (e->kind != UI_PTR_BUTTON || !e->pressed || e->button != BTN_LEFT) return;
	u->status[0] = '\0';
	int click_key = h == HIT_FIELD ? cur_key(u, pos) : -1;
	if (u->editing >= 0 && click_key != u->editing) commit_edit(u);

	struct rect tb;
	if (test_btn_rect(u, &tb) && rect_contains(tb, e->x, e->y)) {
		grabit_notify_test(&u->cfg);
		snprintf(u->status, sizeof u->status, "sent test notification");
		ui_window_redraw(win);
		return;
	}

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
			if (u->editing != i) begin_edit(u, i);
			edit_cursor_at_x(u, e->x, false);
			u->edit_selecting = true;
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
			change_row(u, pos, -1, e->shift ? 10 : 1);
			break;
		case HIT_INC:
			change_row(u, pos, +1, e->shift ? 10 : 1);
			break;
		default:
			break;
		}
	}
	ui_window_redraw(win);
}
