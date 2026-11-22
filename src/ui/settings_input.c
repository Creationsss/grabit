// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/settings_internal.h"

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

static struct set_row *focus_row(struct settings_state *st) {
	struct set_page *p = set_current_page(st);
	return st->focus < p->n ? p->rows[st->focus] : NULL;
}

static void close_popup(struct settings_state *st) {
	if (st->popup == POPUP_NONE) return;
	st->popup = POPUP_NONE;
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
	if (st->plugin_jobs.pid > 0 || st->upload_test) {
		int64_t poll = grabit_now_ms() + SET_JOB_POLL_MS;
		if (!due || poll < due) due = poll;
	}
	if (!due) {
		ui_window_set_timer(st->win, -1);
		return;
	}
	int64_t left = due - grabit_now_ms();
	ui_window_set_timer(st->win, left < 0 ? 0 : (int)left);
}

static void hide_tip(struct settings_state *st) {
	if (set_tip_visible(st)) ui_window_damage(st->win);
	st->tip_at = 0;
	set_arm_timer(st);
}

static void reveal_focus(struct settings_state *st) {
	struct set_row *r = focus_row(st);
	if (!r) return;
	set_layout(st);
	ui_scroll_set(&st->body, set_content_h(st), st->view_h);
	double head = r->section_head ? SET_SECTION_H : 0;
	ui_scroll_reveal(st->win, &st->body, r->y - 8 - head, r->h + 16 + head);
}

static void popup_reveal(struct settings_state *st) {
	struct set_rect d;
	size_t vis = SET_POPUP_MAX_ITEMS;
	set_popup_rect(st, &d, &vis);
	double top = (double)st->popup_hover * SET_POPUP_ITEM_H;
	double view = (double)vis * SET_POPUP_ITEM_H;
	if (top < st->popup_offset) st->popup_offset = top;
	if (top + SET_POPUP_ITEM_H > st->popup_offset + view)
		st->popup_offset = top + SET_POPUP_ITEM_H - view;
}

static size_t popup_index_of(const struct settings_state *st, size_t choice) {
	for (size_t i = 0; i < st->popup_map_n; i++)
		if (st->popup_map[i] == choice) return i;
	return 0;
}

static void open_popup(struct settings_state *st, enum set_popup kind, size_t row) {
	st->popup = kind;
	st->popup_row = row;
	st->popup_offset = 0;
	struct set_row *r = set_current_page(st)->rows[row];
	ui_text_set(&st->popup_filter, "");
	st->popup_hover = SET_NONE;
	if (kind == POPUP_SELECT) {
		set_icons_ensure(st, r, true);
		size_t cur = set_dd_index_of(st, r);
		set_popup_refilter(st);
		st->popup_hover = popup_index_of(st, cur);
		popup_reveal(st);
	}
	hide_tip(st);
	ui_window_damage(st->win);
}

static void open_menu(struct settings_state *st, size_t row, double bx, double by) {
	st->popup_x = bx;
	st->popup_y = by;
	open_popup(st, POPUP_MENU, row);
}

static void begin_edit(struct settings_state *st, struct set_row *r) {
	if (st->editing) return;
	ui_text_set(&st->edit, set_effective(st, r));
	ui_text_select_all(&st->edit);
	st->editing = true;
	ui_window_damage(st->win);
}

static void cancel_edit(struct settings_state *st) {
	if (!st->editing) return;
	st->editing = false;
	struct set_row *r = focus_row(st);
	if (r) r->error[0] = '\0';
	ui_window_damage(st->win);
}

static bool leave_edit(struct settings_state *st) {
	if (!st->editing) return true;
	set_commit_edit(st);
	return !st->editing;
}

static void step_int(struct settings_state *st, struct set_row *r, int dir, bool big) {
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

static void step_enum(struct settings_state *st, struct set_row *r, int dir) {
	set_icons_ensure(st, r, true);
	size_t n = set_dd_count(r);
	size_t i = set_dd_index_of(st, r);
	if (dir < 0 && i == 0) return;
	if (dir > 0 && i + 1 >= n) return;
	set_apply(st, r, set_dd_value(r, dir < 0 ? i - 1 : i + 1));
}

static void toggle(struct settings_state *st, struct set_row *r) {
	bool on = strcmp(set_effective(st, r), "true") == 0;
	set_apply(st, r, on ? "false" : "true");
}

static void record_binding(struct settings_state *st, struct set_row *r) {
	close_popup(st);
	if (!leave_edit(st)) return;
	set_capture_start(st, r);
}

static bool clicks(const struct set_row *r) {
	return r->button || r->info.kind == CFG_KIND_BOOL || r->info.kind == CFG_KIND_ENUM;
}

static void activate(struct settings_state *st, size_t row) {
	struct set_row *r = set_current_page(st)->rows[row];
	if (r->action) {
		r->action(st, r);
		return;
	}
	if (set_inactive_reason(st, r)) return;
	switch (r->info.kind) {
	case CFG_KIND_BOOL:
		toggle(st, r);
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

static void popup_choose(struct settings_state *st, size_t idx) {
	if (idx >= set_popup_count(st) || !set_popup_enabled(st, idx)) return;
	struct set_row *r = set_current_page(st)->rows[st->popup_row];
	enum set_popup kind = st->popup;
	size_t choice = set_popup_choice(st, idx);
	close_popup(st);
	if (kind == POPUP_SELECT) {
		set_apply(st, r, set_dd_value(r, choice));
	} else if (idx == MENU_RESET) {
		st->editing = false;
		set_apply(st, r, NULL);
	} else if (idx == MENU_COPY_KEY) {
		clipboard_set_text(r->key);
	}
}

static void select_page(struct settings_state *st, size_t i) {
	if (i >= st->n_pages) return;
	bool searching = st->search.len > 0;
	if (i == st->page && !searching) return;
	if (!leave_edit(st)) return;
	close_popup(st);
	set_capture_end(st, false);
	if (searching) ui_text_set(&st->search, "");
	st->page = i;
	st->focus = 0;
	st->body.offset = 0;
	st->hover_row = SET_NONE;
	ui_window_damage(st->win);
}

static void search_changed(struct settings_state *st) {
	settings_filter(st);
	st->focus = 0;
	st->body.offset = 0;
	st->hover_row = SET_NONE;
	close_popup(st);
	ui_window_damage(st->win);
}

static void sidebar_press(struct settings_state *st, double x, double y) {
	double sw = set_sidebar_w(st->view_w);
	if (ui_scroll_press(st->win, &st->side, sw, x, y)) return;
	size_t hit = set_side_hit(st, x, y);
	if (hit == SET_NONE || !leave_edit(st)) return;
	if (hit == SET_SIDE_CLEAR) {
		ui_text_set(&st->search, "");
		search_changed(st);
		st->area = AREA_SEARCH;
	} else if (hit == SET_SIDE_SEARCH) {
		bool extend = st->area == AREA_SEARCH && (ui_window_mods(st->win) & KB_MOD_SHIFT);
		st->area = AREA_SEARCH;
		ui_text_click(&st->search, x - SET_PAD, false, extend);
	} else {
		st->area = AREA_SIDEBAR;
		select_page(st, hit);
	}
	ui_window_damage(st->win);
}

static bool popup_press(struct settings_state *st, double bx, double by, uint32_t button) {
	if (st->popup == POPUP_NONE) return false;
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
		close_popup(st);
	else if (item != SET_POPUP_PAD_HIT && button == BTN_LEFT)
		popup_choose(st, item);
	return true;
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
	size_t row = set_row_at(st, by);
	if (row == SET_NONE) {
		leave_edit(st);
		return;
	}
	struct set_row *r = set_current_page(st)->rows[row];
	enum set_part part = set_part_at(st, r, bx, by);
	bool here = st->editing && st->focus == row;
	bool same_field = here && part == SP_CONTROL;
	if (here && part == SP_RESET && button == BTN_LEFT) cancel_edit(st);
	bool keep = same_field || (here && part == SP_AUX && r->aux == AUX_EYE) ||
				(here && part == SP_RESET && button == BTN_RIGHT);
	if (!keep && !leave_edit(st)) return;

	st->area = AREA_BODY;
	st->focus = row;
	if (button == BTN_RIGHT) {
		open_menu(st, row, bx, by);
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
		step_int(st, r, -1, big);
		break;
	case SP_PLUS:
		step_int(st, r, 1, big);
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
			activate(st, row);
		else
			field_press(st, r, row, bx, same_field);
		break;
	case SP_ROW:
	case SP_LABEL:
		if (!r->button) activate(st, row);
		break;
	case SP_NONE:
		break;
	}
	ui_window_damage(st->win);
}

void set_on_button(struct ui_window *w, double x, double y, uint32_t button, bool pressed) {
	struct settings_state *st = w->user;
	hide_tip(st);
	if (set_capture_button(st, button, pressed)) return;
	if (set_dialog_press(st, x, y, button, pressed)) return;
	if (!pressed) {
		st->text_drag = false;
		ui_scroll_release(w, &st->side);
		ui_scroll_release(w, &st->body);
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
		close_popup(st);
		if (!leave_edit(st)) cancel_edit(st);
		set_reset_page_ask(st);
		return;
	}
	if (x < set_sidebar_w(st->view_w)) {
		close_popup(st);
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
	hide_tip(st);
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
	if (set_tests_poll(st)) ui_window_damage(w);
	if (st->toast_until && now >= st->toast_until) {
		st->toast_until = 0;
		ui_window_damage(w);
	}
	set_arm_timer(st);
}

static bool drag_text(struct settings_state *st, double x) {
	struct set_row *r = focus_row(st);
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
	if (drag_text(st, x)) return;
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
			row = set_row_at(st, by);
			if (row != SET_NONE) {
				struct set_row *r = set_current_page(st)->rows[row];
				part = set_part_at(st, r, bx, by);
				cur = cursor_for(st, r, part);
			}
		}
	}
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
	ui_scroll_release(w, &st->side);
	ui_scroll_release(w, &st->body);
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
	hide_tip(st);
	if (st->dialog.open) return;
	if (x < set_sidebar_w(st->view_w)) {
		ui_scroll_wheel(w, &st->side, units, SET_SIDE_ROW_H * 3.0);
		return;
	}
	struct set_rect d;
	size_t vis;
	double bx = x - set_body_x(st), by = y + st->body.offset;
	if (set_popup_rect(st, &d, &vis) && set_rect_contains(&d, bx, by)) {
		size_t n = set_popup_count(st);
		double max = n > vis ? (double)(n - vis) * SET_POPUP_ITEM_H : 0;
		double items = st->popup_offset / SET_POPUP_ITEM_H + units / 10.0;
		items = units > 0 ? ceil(items) : floor(items);
		st->popup_offset = items * SET_POPUP_ITEM_H;
		if (st->popup_offset < 0) st->popup_offset = 0;
		if (st->popup_offset > max) st->popup_offset = max;
		ui_window_damage(w);
		return;
	}
	close_popup(st);
	ui_scroll_wheel(w, &st->body, units, 60.0);
}

static bool popup_filter_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	switch (sym) {
	case XKB_KEY_Up:
	case XKB_KEY_Down:
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down:
	case XKB_KEY_Return:
	case XKB_KEY_Tab:
		return false;
	case XKB_KEY_Escape:
		if (!st->popup_filter.len) return false;
		ui_text_set(&st->popup_filter, "");
		break;
	default: {
		char before[UI_TEXT_MAX];
		memcpy(before, st->popup_filter.buf, st->popup_filter.len + 1);
		if (ui_text_key(&st->popup_filter, sym, mods, false) != UI_TEXT_HANDLED) return true;
		if (strcmp(before, st->popup_filter.buf) == 0) {
			ui_window_damage(st->win);
			return true;
		}
		break;
	}
	}
	set_popup_refilter(st);
	st->popup_hover = 0;
	st->popup_offset = 0;
	ui_window_damage(st->win);
	return true;
}

static bool popup_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	if (set_popup_filtered(st) && popup_filter_key(st, sym, mods)) return true;
	size_t n = set_popup_count(st);
	if (n == 0 && sym != XKB_KEY_Escape && sym != XKB_KEY_Tab) return true;
	switch (sym) {
	case XKB_KEY_Escape:
		close_popup(st);
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_space:
		popup_choose(st, st->popup_hover);
		return true;
	case XKB_KEY_Tab:
		if (st->popup == POPUP_SELECT) popup_choose(st, st->popup_hover);
		close_popup(st);
		return false;
	default:
		break;
	}
	size_t want = st->popup_hover < n ? st->popup_hover : (sym == XKB_KEY_Up ? 0 : n - 1);
	if (!ui_list_key(sym, n, SET_POPUP_MAX_ITEMS, &want)) return true;
	st->popup_hover = want;
	popup_reveal(st);
	ui_window_damage(st->win);
	return true;
}

static bool search_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	enum ui_text_result res = ui_text_key(&st->search, sym, mods, false);
	switch (res) {
	case UI_TEXT_HANDLED:
		search_changed(st);
		return true;
	case UI_TEXT_CANCEL:
		if (st->search.len > 0) {
			ui_text_set(&st->search, "");
			search_changed(st);
		} else {
			st->area = AREA_SIDEBAR;
		}
		ui_window_damage(st->win);
		return true;
	case UI_TEXT_COMMIT:
		break;
	case UI_TEXT_IGNORED:
		if (sym != XKB_KEY_Down && sym != XKB_KEY_Tab) return true;
		break;
	}
	if (set_current_page(st)->n == 0) return true;
	st->area = AREA_BODY;
	st->focus = 0;
	reveal_focus(st);
	ui_window_damage(st->win);
	return true;
}

static bool sidebar_key(struct settings_state *st, xkb_keysym_t sym) {
	if (sym == XKB_KEY_Right || sym == XKB_KEY_Return ||
		sym == XKB_KEY_Tab) {
		if (set_current_page(st)->n == 0) return true;
		st->area = AREA_BODY;
		reveal_focus(st);
		ui_window_damage(st->win);
		return true;
	}
	if (sym == XKB_KEY_slash) {
		st->area = AREA_SEARCH;
		ui_window_damage(st->win);
		return true;
	}
	if (sym == XKB_KEY_ISO_Left_Tab) return true;
	size_t want = st->page;
	if (!ui_list_key(sym, st->n_pages, 8, &want)) return false;
	select_page(st, want);
	ui_scroll_reveal(st->win, &st->side, SET_SIDE_LIST_Y + (double)want * SET_SIDE_ROW_H,
					 SET_SIDE_ROW_H);
	return true;
}

static void move_focus(struct settings_state *st, long delta) {
	struct set_page *p = set_current_page(st);
	if (p->n == 0) return;
	long f = (long)st->focus + delta;
	if (f < 0) f = 0;
	if (f >= (long)p->n) f = (long)p->n - 1;
	st->focus = (size_t)f;
	reveal_focus(st);
	ui_window_damage(st->win);
}

static void to_sidebar(struct settings_state *st) {
	st->area = st->search.len > 0 ? AREA_SEARCH : AREA_SIDEBAR;
	ui_window_damage(st->win);
}

static bool is_menu_key(xkb_keysym_t sym, uint8_t mods) {
	return sym == XKB_KEY_Menu || (sym == XKB_KEY_F10 && (mods & KB_MOD_SHIFT));
}

static bool body_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	struct set_row *r = focus_row(st);
	if (!r) {
		to_sidebar(st);
		return true;
	}
	if (st->editing) {
		enum ui_text_result res = ui_text_key(&st->edit, sym, mods, set_masked(r));
		if (res == UI_TEXT_HANDLED) {
			ui_window_damage(st->win);
			return true;
		}
		if (res == UI_TEXT_CANCEL) {
			cancel_edit(st);
			return true;
		}
		if (!is_menu_key(sym, mods) && !leave_edit(st)) return true;
		if (res == UI_TEXT_COMMIT) return true;
	}
	if (is_menu_key(sym, mods)) {
		open_menu(st, st->focus, SET_BODY_PAD, r->y + r->h - 6);
		return true;
	}
	bool big = (mods & KB_MOD_SHIFT) != 0;
	switch (sym) {
	case XKB_KEY_Down:
	case XKB_KEY_Tab:
		move_focus(st, 1);
		return true;
	case XKB_KEY_Up:
		move_focus(st, -1);
		return true;
	case XKB_KEY_ISO_Left_Tab:
		if (st->focus == 0)
			to_sidebar(st);
		else
			move_focus(st, -1);
		return true;
	case XKB_KEY_Page_Down:
		move_focus(st, 6);
		return true;
	case XKB_KEY_Page_Up:
		move_focus(st, -6);
		return true;
	case XKB_KEY_Home:
		move_focus(st, -(long)st->focus);
		return true;
	case XKB_KEY_End:
		move_focus(st, (long)set_current_page(st)->n);
		return true;
	case XKB_KEY_Left:
	case XKB_KEY_Right: {
		int dir = sym == XKB_KEY_Left ? -1 : 1;
		if (set_inactive_reason(st, r)) {
			if (dir < 0) to_sidebar(st);
			return true;
		}
		bool on = strcmp(set_effective(st, r), "true") == 0;
		if (r->info.kind == CFG_KIND_INT)
			step_int(st, r, dir, big);
		else if (r->info.kind == CFG_KIND_ENUM)
			step_enum(st, r, dir);
		else if (r->info.kind == CFG_KIND_BOOL && on == (dir < 0))
			toggle(st, r);
		else if (dir < 0)
			to_sidebar(st);
		return true;
	}
	case XKB_KEY_Return:
	case XKB_KEY_space:
		activate(st, st->focus);
		return true;
	case XKB_KEY_Escape:
		to_sidebar(st);
		return true;
	default:
		return false;
	}
}

bool set_on_key(struct ui_window *w, xkb_keysym_t sym, uint8_t mods) {
	struct settings_state *st = w->user;
	hide_tip(st);
	if (!st->focus_visible) ui_window_damage(w);
	st->focus_visible = true;
	if (set_capture_key(st, sym, mods)) return true;
	if (set_dialog_key(st, sym, mods)) return true;
	bool ctrl = (mods & KB_MOD_CTRL) != 0;
	xkb_keysym_t lower = xkb_keysym_to_lower(sym);
	bool typing = st->area == AREA_SEARCH || set_popup_filtered(st);
	if (ctrl && !typing && (lower == XKB_KEY_z || lower == XKB_KEY_y)) {
		bool redo = lower == XKB_KEY_y || (mods & KB_MOD_SHIFT);
		if (st->editing) {
			if (!redo) cancel_edit(st);
			return true;
		}
		close_popup(st);
		if (redo)
			set_redo(st);
		else
			set_undo(st);
		return true;
	}
	if (ctrl && lower == XKB_KEY_f) {
		if (!leave_edit(st)) return true;
		close_popup(st);
		st->area = AREA_SEARCH;
		ui_text_select_all(&st->search);
		ui_window_damage(w);
		return true;
	}
	if (ctrl && (sym == XKB_KEY_Page_Down || sym == XKB_KEY_Page_Up)) {
		size_t n = st->n_pages;
		size_t next = sym == XKB_KEY_Page_Down ? (st->page + 1) % n : (st->page + n - 1) % n;
		select_page(st, next);
		return true;
	}
	if (st->popup != POPUP_NONE && popup_key(st, sym, mods)) return true;
	switch (st->area) {
	case AREA_SEARCH:
		return search_key(st, sym, mods);
	case AREA_SIDEBAR:
		return sidebar_key(st, sym);
	case AREA_BODY:
		return body_key(st, sym, mods);
	}
	return false;
}

void set_on_focus_lost(struct ui_window *w) {
	struct settings_state *st = w->user;
	hide_tip(st);
	set_capture_end(st, false);
	close_popup(st);
}

void set_on_drag(struct ui_window *w, bool over) {
	struct settings_state *st = w->user;
	st->drag_over = over;
	ui_window_damage(w);
}

static bool ends_with_sxcu(const char *s, size_t n) {
	return n >= 5 && strncasecmp(s + n - 5, ".sxcu", 5) == 0;
}

static void show_row(struct settings_state *st, const struct set_row *r) {
	if (!r) return;
	for (size_t i = 0; i < st->n_pages; i++)
		for (size_t j = 0; j < st->pages[i].n; j++)
			if (st->pages[i].rows[j] == r) {
				select_page(st, i);
				st->area = AREA_BODY;
				st->focus = j;
				reveal_focus(st);
				return;
			}
}

const struct set_drop_hint *set_drop_hint(const struct settings_state *st) {
	static const struct set_drop_hint PLUGINS = {
		"Drop to install a plugin", "a folder with the plugin's git repo and manifest.toml",
		"Drop a folder with a plugin's repo to install it"};
	static const struct set_drop_hint UPLOADS = {
		"Drop to add an uploader", "ShareX .sxcu files are copied into grabit's uploaders",
		"Only ShareX .sxcu files can be added as uploaders"};
	static const struct set_drop_hint ANY = {
		"Drop to add an uploader or install a plugin",
		".sxcu files become uploaders; a plugin's repo folder is installed",
		"Drop .sxcu files to add uploaders, or a plugin folder to install it"};
	if (st->page == st->plugins_page) return &PLUGINS;
	if (st->page == st->uploads_page) return &UPLOADS;
	return &ANY;
}

static char *plugin_folder(const char *line) {
	char *path = set_resolve_path(line);
	char manifest[UI_TEXT_MAX + 16];
	if (path && snprintf(manifest, sizeof manifest, "%s/manifest.toml", path) > 0 &&
		access(manifest, R_OK) == 0)
		return path;
	free(path);
	return NULL;
}

void set_on_drop(struct ui_window *w, const char *uri_list) {
	struct settings_state *st = w->user;
	st->drag_over = false;
	if (!leave_edit(st)) cancel_edit(st);
	close_popup(st);
	set_capture_end(st, false);
	size_t uploaders = 0;
	char *plugin = NULL;
	for (const char *p = uri_list; *p;) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char line[UI_TEXT_MAX];
		size_t len = n < sizeof line ? n : sizeof line - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		len = grabit_rstrip(line, len);
		if (len && line[0] != '#') {
			if (ends_with_sxcu(line, len)) {
				if (!uploaders++) show_row(st, set_sxcu_row(st));
				set_add_sxcu(st, line);
			} else if (!plugin) {
				plugin = plugin_folder(line);
			}
		}
		if (!e) break;
		p = e + 1;
	}
	if (plugin) {
		show_row(st, set_submit_row(st, set_install_plugin));
		set_install_plugin(st, plugin);
		free(plugin);
	} else if (!uploaders) {
		set_toast(st, "%s", set_drop_hint(st)->refused);
	}
	ui_window_damage(w);
}
