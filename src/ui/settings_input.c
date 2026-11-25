// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/settings_input.h"

#include "clipboard/clipboard.h"
#include "ui/window.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <linux/input-event-codes.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct set_row *set_focus_row(struct settings_state *st) {
	struct set_page *p = set_current_page(st);
	return st->focus < p->n ? p->rows[st->focus] : NULL;
}

void set_close_popup(struct settings_state *st) {
	if (st->popup == POPUP_NONE) return;
	st->popup = POPUP_NONE;
	free(st->popup_arg);
	st->popup_arg = NULL;
	ui_window_damage(st->win);
}

bool set_tip_visible(const struct settings_state *st) {
	return st->tip_at && grabit_now_ms() >= st->tip_at;
}

void set_arm_timer(struct settings_state *st) {
	int64_t due = st->tip_at > grabit_now_ms() ? st->tip_at : 0;
	if (st->toast_until && (!due || st->toast_until < due)) due = st->toast_until;
	if (st->icons_state == ICONS_SCANNING) {
		int64_t poll = grabit_now_ms() + 50;
		if (!due || poll < due) due = poll;
	}
	if (st->plugin_jobs.pid > 0 || st->upload_test || st->hist_pid > 0 || st->up_pid > 0) {
		int64_t poll = grabit_now_ms() + SET_JOB_POLL_MS;
		if (!due || poll < due) due = poll;
	}
	if (set_upload_busy(st)) {
		int64_t poll = grabit_now_ms() + 80;
		if (!due || poll < due) due = poll;
	}
	if (set_hist_busy(st)) {
		int64_t poll = grabit_now_ms() + 60;
		if (!due || poll < due) due = poll;
	}
	if (!due) {
		ui_window_set_timer(st->win, -1);
		return;
	}
	int64_t left = due - grabit_now_ms();
	ui_window_set_timer(st->win, left < 0 ? 0 : (int)left);
}

void set_hide_tip(struct settings_state *st) {
	if (set_tip_visible(st)) ui_window_damage(st->win);
	st->tip_at = 0;
	set_arm_timer(st);
}

void set_reveal_focus(struct settings_state *st) {
	struct set_row *r = set_focus_row(st);
	if (!r) return;
	set_layout(st);
	ui_scroll_set(&st->body, set_content_h(st), st->view_h);
	double head = r->section_head ? SET_SECTION_H : 0;
	ui_scroll_reveal(st->win, &st->body, r->y - 8 - head, r->h + 16 + head);
}

void set_popup_reveal(struct settings_state *st) {
	struct set_rect d, list;
	if (set_popup_sync(st, &d, &list))
		ui_scroll_reveal(st->win, &st->popup_scroll,
						 (double)st->popup_hover * SET_POPUP_ITEM_H, SET_POPUP_ITEM_H);
}

static size_t popup_index_of(const struct settings_state *st, size_t choice) {
	for (size_t i = 0; i < st->popup_map_n; i++)
		if (st->popup_map[i] == choice) return i;
	return 0;
}

static void open_popup(struct settings_state *st, enum set_popup kind, size_t row) {
	st->popup = kind;
	st->popup_row = row;
	st->popup_scroll = (struct ui_scroll){0};
	struct set_row *r = set_current_page(st)->rows[row];
	ui_text_set(&st->popup_filter, "");
	st->popup_hover = SET_NONE;
	if (kind == POPUP_SELECT) {
		set_icons_ensure(st, r, true);
		size_t cur = set_dd_index_of(st, r);
		set_popup_refilter(st);
		st->popup_hover = popup_index_of(st, cur);
		set_popup_reveal(st);
	}
	set_hide_tip(st);
	ui_window_damage(st->win);
}

void set_open_menu(struct settings_state *st, size_t row, double bx, double by) {
	st->popup_x = bx;
	st->popup_y = by;
	open_popup(st, POPUP_MENU, row);
}

void set_add_action(struct settings_state *st, const char *label, set_toast_action fn) {
	if (st->popup_act_n == SET_MAX_ACTIONS) return;
	st->popup_acts[st->popup_act_n].label = label;
	st->popup_acts[st->popup_act_n++].fn = fn;
}

void set_open_actions(struct settings_state *st, size_t row, double bx, double by, char *arg) {
	set_close_popup(st);
	st->popup_x = bx;
	st->popup_y = by;
	open_popup(st, POPUP_ACTIONS, row);
	st->popup_arg = arg;
	st->popup_act_n = 0;
}

static void begin_edit(struct settings_state *st, struct set_row *r) {
	if (st->editing) return;
	ui_text_set(&st->edit, set_effective(st, r));
	ui_text_select_all(&st->edit);
	st->editing = true;
	ui_window_damage(st->win);
}

void set_cancel_edit(struct settings_state *st) {
	if (!st->editing) return;
	st->editing = false;
	struct set_row *r = set_focus_row(st);
	if (r) r->error[0] = '\0';
	ui_window_damage(st->win);
}

bool set_leave_edit(struct settings_state *st) {
	if (!st->editing) return true;
	set_commit_edit(st);
	return !st->editing;
}

void set_step_int(struct settings_state *st, struct set_row *r, int dir, bool big) {
	if (!set_step_enabled(st, r, dir)) return;
	const char *cur = set_effective(st, r);
	bool is_auto = !cur[0] || strcmp(cur, "auto") == 0;
	long v = is_auto ? r->info.lo : strtol(cur, NULL, 10);
	long n = is_auto ? r->info.lo : v + (long)dir * (big ? 10 : 1);
	if (n < r->info.lo) {
		if (r->info.allow_auto && v <= r->info.lo) {
			set_apply(st, r, "auto");
			return;
		}
		n = r->info.lo;
	}
	if (n > r->info.hi) n = r->info.hi;
	char buf[32];
	snprintf(buf, sizeof buf, "%ld", n);
	set_apply(st, r, buf);
}

void set_step_enum(struct settings_state *st, struct set_row *r, int dir) {
	set_icons_ensure(st, r, true);
	size_t n = set_dd_count(r);
	size_t i = set_dd_index_of(st, r);
	if (dir < 0 && i == 0) return;
	if (dir > 0 && i + 1 >= n) return;
	set_apply(st, r, set_dd_value(r, dir < 0 ? i - 1 : i + 1));
}

void set_toggle(struct settings_state *st, struct set_row *r) {
	bool on = strcmp(set_effective(st, r), "true") == 0;
	set_apply(st, r, on ? "false" : "true");
}

static void record_binding(struct settings_state *st, struct set_row *r) {
	set_close_popup(st);
	if (!set_leave_edit(st)) return;
	set_capture_start(st, r);
}

static bool clicks(const struct set_row *r) {
	return r->button || r->info.kind == CFG_KIND_BOOL || r->info.kind == CFG_KIND_ENUM;
}

void set_activate(struct settings_state *st, size_t row) {
	struct set_row *r = set_current_page(st)->rows[row];
	if (r->action) {
		r->action(st, r);
		return;
	}
	if (set_inactive_reason(st, r)) return;
	switch (r->info.kind) {
	case CFG_KIND_BOOL:
		set_toggle(st, r);
		break;
	case CFG_KIND_ENUM:
		open_popup(st, POPUP_SELECT, row);
		break;
	case CFG_KIND_INT:
	case CFG_KIND_TEXT:
	case CFG_KIND_KEYBIND:
		begin_edit(st, r);
		break;
	}
}

void set_popup_choose(struct settings_state *st, size_t idx) {
	if (idx >= set_popup_count(st) || !set_popup_enabled(st, idx)) return;
	struct set_row *r = set_current_page(st)->rows[st->popup_row];
	enum set_popup kind = st->popup;
	size_t choice = set_popup_choice(st, idx);
	if (kind == POPUP_ACTIONS) {
		set_toast_action fn = st->popup_acts[idx].fn;
		char *arg = st->popup_arg ? st->popup_arg : strdup(st->popup_acts[idx].label);
		st->popup_arg = NULL;
		set_close_popup(st);
		if (fn && arg) fn(st, arg);
		free(arg);
		return;
	}
	set_close_popup(st);
	if (kind == POPUP_SELECT) {
		set_apply(st, r, set_dd_value(r, choice));
	} else if (idx == MENU_RESET) {
		st->editing = false;
		set_apply(st, r, NULL);
	} else if (idx == MENU_COPY_KEY) {
		clipboard_set_text(r->once ? r->once : r->key);
	}
}

void set_select_page(struct settings_state *st, size_t i) {
	if (i >= st->n_pages) return;
	bool searching = st->search.len > 0;
	if (i == st->page && !searching) return;
	if (!set_leave_edit(st)) return;
	set_close_popup(st);
	set_capture_end(st, false);
	if (searching) ui_text_set(&st->search, "");
	st->page = i;
	st->focus = 0;
	st->body.offset = 0;
	st->hover_row = SET_NONE;
	ui_window_damage(st->win);
}

void set_search_changed(struct settings_state *st) {
	settings_filter(st);
	st->focus = 0;
	st->body.offset = 0;
	st->hover_row = SET_NONE;
	set_close_popup(st);
	ui_window_damage(st->win);
}

static void sidebar_press(struct settings_state *st, double x, double y) {
	double sw = set_sidebar_w(st->view_w);
	if (ui_scroll_press(st->win, &st->side, sw, x, y)) return;
	size_t hit = set_side_hit(st, x, y);
	if (hit == SET_NONE || !set_leave_edit(st)) return;
	if (hit == SET_SIDE_CLEAR) {
		ui_text_set(&st->search, "");
		set_search_changed(st);
		st->area = AREA_SEARCH;
	} else if (hit == SET_SIDE_SEARCH) {
		bool extend = st->area == AREA_SEARCH && (ui_window_mods(st->win) & KB_MOD_SHIFT);
		st->area = AREA_SEARCH;
		ui_text_click(&st->search, x - SET_PAD, false, extend);
	} else {
		st->area = AREA_SIDEBAR;
		set_select_page(st, hit);
	}
	ui_window_damage(st->win);
}

static bool popup_press(struct settings_state *st, double bx, double by, uint32_t button) {
	if (st->popup == POPUP_NONE) return false;
	struct set_rect outer, list;
	if (button == BTN_LEFT && set_popup_sync(st, &outer, &list) && set_rect_contains(&list, bx, by) &&
		ui_scroll_press(st->win, &st->popup_scroll, list.w - 2, bx - list.x, by - list.y))
		return true;
	size_t item = set_popup_item_at(st, bx, by);
	struct set_rect d;
	if (item == SET_POPUP_PAD_HIT && set_popup_filtered(st) && set_popup_rect(st, &d, NULL) &&
		by < d.y + SET_POPUP_FILTER_H) {
		ui_text_click(&st->popup_filter, bx - d.x - 6, false,
					  (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0);
		ui_window_damage(st->win);
		return true;
	}
	if (item == SET_NONE)
		set_close_popup(st);
	else if (item != SET_POPUP_PAD_HIT && button == BTN_LEFT)
		set_popup_choose(st, item);
	return true;
}

size_t set_captured_row(struct settings_state *st, double bx, double by) {
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; i < p->n; i++)
		if (p->rows[i]->custom && p->rows[i]->custom->capture &&
			p->rows[i]->custom->capture(st, p->rows[i], bx, by))
			return i;
	return SET_NONE;
}

struct set_row *set_drop_row(struct settings_state *st, bool paste) {
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; p != &st->results && i < p->n; i++) {
		const struct set_custom *c = p->rows[i]->custom;
		if (c && (paste ? c->paste != NULL : c->drop != NULL)) return p->rows[i];
	}
	return NULL;
}

static bool is_double_click(struct settings_state *st, size_t row) {
	int64_t now = grabit_now_ms();
	bool dbl = st->last_click_row == row && now - st->last_click_ms < SET_DOUBLE_CLICK_MS;
	st->last_click_ms = dbl ? 0 : now;
	st->last_click_row = row;
	return dbl;
}

static void field_press(struct settings_state *st, struct set_row *r, size_t row, double bx,
						bool same_field) {
	struct set_rect c;
	set_part_rect(st, r, SP_CONTROL, &c);
	bool shift = (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0;
	begin_edit(st, r);
	ui_text_click(&st->edit, bx - c.x, set_masked(r), same_field && shift);
	if (is_double_click(st, row) && same_field)
		ui_text_select_word(&st->edit);
	else
		st->text_drag = true;
}

static void body_press(struct settings_state *st, double x, double y, uint32_t button) {
	double bx = x - set_body_x(st), by = y + st->body.offset;
	if (popup_press(st, bx, by, button)) return;
	if (button == BTN_LEFT &&
		ui_scroll_press(st->win, &st->body, set_body_w(st), bx, y))
		return;
	set_layout(st);
	size_t row = set_captured_row(st, bx, by);
	if (row == SET_NONE) row = set_row_at(st, by);
	if (row == SET_NONE) {
		set_leave_edit(st);
		return;
	}
	struct set_row *r = set_current_page(st)->rows[row];
	if (r->custom) {
		if (!set_leave_edit(st)) return;
		st->area = AREA_BODY;
		st->focus = row;
		r->custom->press(st, r, bx, by, button);
		ui_window_damage(st->win);
		return;
	}
	enum set_part part = set_part_at(st, r, bx, by);
	bool here = st->editing && st->focus == row;
	bool same_field = here && part == SP_CONTROL;
	if (here && part == SP_RESET && button == BTN_LEFT) set_cancel_edit(st);
	bool keep = same_field || (here && part == SP_AUX && r->aux == AUX_EYE) ||
				(here && part == SP_RESET && button == BTN_RIGHT);
	if (!keep && !set_leave_edit(st)) return;

	st->area = AREA_BODY;
	st->focus = row;
	if (button == BTN_RIGHT) {
		set_open_menu(st, row, bx, by);
		return;
	}
	st->press_row = row;
	st->press_part = part;
	if (part != SP_RESET && set_inactive_reason(st, r)) {
		ui_window_damage(st->win);
		return;
	}
	bool big = (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0;
	switch (part) {
	case SP_RESET:
		set_apply(st, r, NULL);
		break;
	case SP_MINUS:
		set_step_int(st, r, -1, big);
		break;
	case SP_PLUS:
		set_step_int(st, r, 1, big);
		break;
	case SP_AUX:
		if (r->aux == AUX_EYE) r->reveal = !r->reveal;
		if (r->aux == AUX_RECORD) {
			record_binding(st, r);
			st->press_row = SET_NONE;
		}
		break;
	case SP_ALT:
	case SP_ALT2:
		r->alt[part - SP_ALT].action(st, r);
		break;
	case SP_CONTROL:
		if (clicks(r))
			set_activate(st, row);
		else
			field_press(st, r, row, bx, same_field);
		break;
	case SP_ROW:
	case SP_LABEL:
		if (!r->button) set_activate(st, row);
		break;
	case SP_NONE:
		break;
	}
	ui_window_damage(st->win);
}

void set_on_button(struct ui_window *w, double x, double y, uint32_t button, bool pressed) {
	struct settings_state *st = w->user;
	set_hide_tip(st);
	if (set_capture_button(st, button, pressed)) return;
	if (set_dialog_press(st, x, y, button, pressed)) return;
	if (pressed && st->popup != POPUP_NONE && set_hist_view_shown(st)) {
		popup_press(st, x - set_body_x(st), y + st->body.offset, button);
		return;
	}
	if (set_hist_view_press(st, x, y, button, pressed)) return;
	if (!pressed) {
		st->text_drag = false;
		ui_scroll_release(w, &st->side);
		ui_scroll_release(w, &st->body);
		ui_scroll_release(w, &st->popup_scroll);
		if (st->press_row != SET_NONE) {
			st->press_row = SET_NONE;
			ui_window_damage(w);
		}
		return;
	}
	if (st->focus_visible) ui_window_damage(w);
	st->focus_visible = false;
	if (button == BTN_LEFT && st->toast_until && set_rect_contains(&st->toast_btn, x, y)) {
		set_toast_run(st);
		return;
	}
	if (button == BTN_LEFT && set_rect_contains(&st->page_reset_btn, x, y)) {
		set_close_popup(st);
		if (!set_leave_edit(st)) set_cancel_edit(st);
		set_reset_page_ask(st);
		return;
	}
	if (x < set_sidebar_w(st->view_w)) {
		set_close_popup(st);
		if (button == BTN_LEFT) sidebar_press(st, x, y);
	} else {
		body_press(st, x, y, button);
	}
}

static enum grabit_cursor_kind cursor_for(struct settings_state *st, const struct set_row *r,
										  enum set_part part) {
	if (part == SP_RESET) return GCUR_HAND;
	if (set_inactive_reason(st, r)) return GCUR_DEFAULT;
	switch (part) {
	case SP_MINUS:
		return set_step_enabled(st, r, -1) ? GCUR_HAND : GCUR_DEFAULT;
	case SP_PLUS:
		return set_step_enabled(st, r, 1) ? GCUR_HAND : GCUR_DEFAULT;
	case SP_AUX:
		return r->aux == AUX_SWATCH ? GCUR_DEFAULT : GCUR_HAND;
	case SP_ALT:
	case SP_ALT2:
		return GCUR_HAND;
	case SP_CONTROL:
		return clicks(r) ? GCUR_HAND : GCUR_TEXT;
	case SP_ROW:
	case SP_LABEL:
		if (r->button) return GCUR_DEFAULT;
		return r->info.kind == CFG_KIND_BOOL || r->info.kind == CFG_KIND_ENUM ? GCUR_HAND
																			  : GCUR_DEFAULT;
	case SP_RESET:
	case SP_NONE:
		break;
	}
	return GCUR_DEFAULT;
}

static void update_tip(struct settings_state *st, size_t row, enum set_part part) {
	if (row == st->tip_row && part == st->tip_part) return;
	set_hide_tip(st);
	st->tip_row = row;
	st->tip_part = part;
	if (!set_tip_text(st, row, part)) return;
	st->tip_at = grabit_now_ms() + SET_TIP_DELAY_MS;
	set_arm_timer(st);
}

void set_on_timer(struct ui_window *w) {
	struct settings_state *st = w->user;
	int64_t now = grabit_now_ms();
	if (st->tip_at && now >= st->tip_at) ui_window_damage(w);
	if (st->icons_state == ICONS_SCANNING && set_icons_poll(st)) ui_window_damage(w);
	if (set_jobs_poll(st)) ui_window_damage(w);
	if (set_history_poll(st)) ui_window_damage(w);
	if (set_upload_poll(st)) ui_window_damage(w);
	if (set_hist_poll_thumbs(st)) ui_window_damage(w);
	if (set_tests_poll(st)) ui_window_damage(w);
	if (st->toast_until && now >= st->toast_until) {
		st->toast_until = 0;
		ui_window_damage(w);
	}
	set_arm_timer(st);
}

static bool drag_text(struct settings_state *st, double x) {
	struct set_row *r = set_focus_row(st);
	if (!st->text_drag || !st->editing || !r) return false;
	struct set_rect c;
	set_part_rect(st, r, SP_CONTROL, &c);
	ui_text_click(&st->edit, x - set_body_x(st) - c.x, set_masked(r), true);
	ui_window_damage(st->win);
	return true;
}

void set_on_motion(struct ui_window *w, double x, double y) {
	struct settings_state *st = w->user;
	double sw = set_sidebar_w(st->view_w);
	size_t side = SET_NONE, row = SET_NONE, item = st->popup_hover;
	enum set_part part = SP_NONE;
	enum grabit_cursor_kind cur = GCUR_DEFAULT;

	if (set_dialog_motion(st, x, y)) return;
	if (st->popup != POPUP_NONE && set_hist_view_shown(st)) {
		size_t hit = set_popup_item_at(st, x - set_body_x(st), y + st->body.offset);
		if (hit == SET_POPUP_PAD_HIT) hit = SET_NONE;
		st->win->content_cursor = hit == SET_NONE ? GCUR_DEFAULT : GCUR_HAND;
		if (hit != st->popup_hover) {
			st->popup_hover = hit;
			ui_window_damage(w);
		}
		return;
	}
	if (set_hist_view_motion(st, x, y)) return;
	if (drag_text(st, x)) return;
	struct set_rect outer, list;
	double px = x - set_body_x(st), py = y + st->body.offset;
	if (st->popup != POPUP_NONE && set_popup_sync(st, &outer, &list) &&
		ui_scroll_motion(w, &st->popup_scroll, list.w - 2,
						 set_rect_contains(&list, px, py) ? px - list.x : -1, py - list.y)) {
		st->win->content_cursor = GCUR_DEFAULT;
		return;
	}
	if (st->body.drag) {
		ui_scroll_motion(w, &st->body, set_body_w(st), x - set_body_x(st), y);
		return;
	}
	bool in_side = x >= 0 && x < sw && y >= 0;
	bool on_bar = ui_scroll_motion(w, &st->side, sw, in_side ? x : -1, y);
	if (st->side.drag) return;
	if (in_side && !on_bar) {
		side = set_side_hit(st, x, y);
		cur = side == SET_SIDE_SEARCH ? GCUR_TEXT : side == SET_NONE ? GCUR_DEFAULT
																	 : GCUR_HAND;
		if (side == SET_SIDE_CLEAR) part = SP_AUX;
	} else if (!in_side) {
		double bx = x - set_body_x(st), by = y + st->body.offset;
		ui_scroll_motion(w, &st->body, set_body_w(st), bx, y);
		size_t hit = set_popup_item_at(st, bx, by);
		if (hit != SET_NONE) {
			if (hit != SET_POPUP_PAD_HIT) item = hit;
			cur = GCUR_HAND;
		} else if (y >= 0) {
			row = set_captured_row(st, bx, by);
			if (row == SET_NONE) row = set_row_at(st, by);
			if (row != SET_NONE) {
				struct set_row *r = set_current_page(st)->rows[row];
				part = set_part_at(st, r, bx, by);
				cur = cursor_for(st, r, part);
				int gc = r->custom ? r->custom->motion(st, r, bx, by) : 0;
				if (gc) cur = gc == 2 ? GCUR_TEXT : GCUR_HAND;
			}
		}
	}
	struct set_page *cp = set_current_page(st);
	for (size_t k = 0; k < cp->n; k++)
		if (cp->rows[k]->custom && k != row) cp->rows[k]->custom->motion(st, NULL, 0, 0);
	bool reset_hot = set_rect_contains(&st->page_reset_btn, x, y) && st->popup == POPUP_NONE;
	if ((st->toast_until && set_rect_contains(&st->toast_btn, x, y)) || reset_hot)
		cur = GCUR_HAND;
	if (reset_hot != st->page_reset_hover) {
		st->page_reset_hover = reset_hot;
		ui_window_damage(w);
	}
	w->content_cursor = cur;
	update_tip(st, side == SET_SIDE_CLEAR ? side : row, part);
	if (side != st->side_hover || row != st->hover_row || part != st->hover_part ||
		item != st->popup_hover) {
		st->side_hover = side;
		st->hover_row = row;
		st->hover_part = part;
		st->popup_hover = item;
		ui_window_damage(w);
	}
}

void set_on_leave(struct ui_window *w) {
	struct settings_state *st = w->user;
	ui_scroll_motion(w, &st->side, 0, -1, -1);
	ui_scroll_motion(w, &st->popup_scroll, 0, -1, -1);
	struct set_page *p = set_current_page(st);
	for (size_t i = 0; i < p->n; i++)
		if (p->rows[i]->custom) p->rows[i]->custom->motion(st, NULL, 0, 0);
	ui_scroll_release(w, &st->side);
	ui_scroll_release(w, &st->body);
	ui_scroll_release(w, &st->popup_scroll);
	st->text_drag = false;
	st->press_row = SET_NONE;
	st->press_part = SP_NONE;
	st->page_reset_hover = false;
	st->dialog.hover = SET_NONE;
	update_tip(st, SET_NONE, SP_NONE);
	st->side_hover = SET_NONE;
	st->hover_row = SET_NONE;
	st->hover_part = SP_NONE;
	ui_window_damage(w);
}

void set_on_scroll(struct ui_window *w, double x, double y, double units) {
	struct settings_state *st = w->user;
	set_hide_tip(st);
	if (st->dialog.open || set_hist_view_shown(st)) return;
	if (x < set_sidebar_w(st->view_w)) {
		ui_scroll_wheel(w, &st->side, units, SET_SIDE_ROW_H * 3.0);
		return;
	}
	struct set_rect d, list;
	double bx = x - set_body_x(st), by = y + st->body.offset;
	if (set_popup_sync(st, &d, &list) && set_rect_contains(&d, bx, by)) {
		double items = st->popup_scroll.offset / SET_POPUP_ITEM_H + units / 10.0;
		items = units > 0 ? ceil(items) : floor(items);
		ui_scroll_to(w, &st->popup_scroll, items * SET_POPUP_ITEM_H);
		return;
	}
	set_close_popup(st);
	ui_scroll_wheel(w, &st->body, units, 60.0);
}
