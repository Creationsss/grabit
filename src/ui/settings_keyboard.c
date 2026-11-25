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
static bool popup_filter_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	switch (sym) {
	case XKB_KEY_Return: {
		struct set_row *r = set_current_page(st)->rows[st->popup_row];
		const char *v = st->popup_filter.buf;
		if (!set_row_has_icons(r) || !ui_icon_is_file(v) || !ui_icon_supported(v)) return false;
		set_close_popup(st);
		set_apply(st, r, st->popup_filter.buf);
		return true;
	}
	case XKB_KEY_Up:
	case XKB_KEY_Down:
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down:
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
	st->popup_scroll = (struct ui_scroll){0};
	ui_window_damage(st->win);
	return true;
}

static bool popup_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	if (set_popup_filtered(st) && popup_filter_key(st, sym, mods)) return true;
	size_t n = set_popup_count(st);
	if (n == 0 && sym != XKB_KEY_Escape && sym != XKB_KEY_Tab) return true;
	switch (sym) {
	case XKB_KEY_Escape:
		set_close_popup(st);
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_space:
		set_popup_choose(st, st->popup_hover);
		return true;
	case XKB_KEY_Tab:
		if (st->popup == POPUP_SELECT) set_popup_choose(st, st->popup_hover);
		set_close_popup(st);
		return false;
	default:
		break;
	}
	size_t want = st->popup_hover < n ? st->popup_hover : (sym == XKB_KEY_Up ? 0 : n - 1);
	if (!ui_list_key(sym, n, SET_POPUP_MAX_ITEMS, &want)) return true;
	st->popup_hover = want;
	set_popup_reveal(st);
	ui_window_damage(st->win);
	return true;
}

static bool search_key(struct settings_state *st, xkb_keysym_t sym, uint8_t mods) {
	enum ui_text_result res = ui_text_key(&st->search, sym, mods, false);
	switch (res) {
	case UI_TEXT_HANDLED:
		set_search_changed(st);
		return true;
	case UI_TEXT_CANCEL:
		if (st->search.len > 0) {
			ui_text_set(&st->search, "");
			set_search_changed(st);
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
	set_reveal_focus(st);
	ui_window_damage(st->win);
	return true;
}

static bool sidebar_key(struct settings_state *st, xkb_keysym_t sym) {
	if (sym == XKB_KEY_Right || sym == XKB_KEY_Return ||
		sym == XKB_KEY_Tab) {
		if (set_current_page(st)->n == 0) return true;
		st->area = AREA_BODY;
		set_reveal_focus(st);
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
	set_select_page(st, want);
	ui_scroll_reveal(st->win, &st->side, set_side_row_y(st, want), SET_SIDE_ROW_H);
	return true;
}

static void move_focus(struct settings_state *st, long delta) {
	struct set_page *p = set_current_page(st);
	if (p->n == 0) return;
	long f = (long)st->focus + delta;
	if (f < 0) f = 0;
	if (f >= (long)p->n) f = (long)p->n - 1;
	st->focus = (size_t)f;
	set_reveal_focus(st);
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
	struct set_row *r = set_focus_row(st);
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
			set_cancel_edit(st);
			return true;
		}
		if (!is_menu_key(sym, mods) && !set_leave_edit(st)) return true;
		if (res == UI_TEXT_COMMIT) return true;
	}
	if (r->custom && r->custom->key(st, r, sym, mods)) return true;
	if (r->custom && (is_menu_key(sym, mods) || sym == XKB_KEY_Return || sym == XKB_KEY_space ||
					  sym == XKB_KEY_Left || sym == XKB_KEY_Right)) {
		if (sym == XKB_KEY_Left) to_sidebar(st);
		return true;
	}
	if (is_menu_key(sym, mods)) {
		set_open_menu(st, st->focus, SET_BODY_PAD, r->y + r->h - 6);
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
			set_step_int(st, r, dir, big);
		else if (r->info.kind == CFG_KIND_ENUM)
			set_step_enum(st, r, dir);
		else if (r->info.kind == CFG_KIND_BOOL && on == (dir < 0))
			set_toggle(st, r);
		else if (dir < 0)
			to_sidebar(st);
		return true;
	}
	case XKB_KEY_Return:
	case XKB_KEY_space:
		set_activate(st, st->focus);
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
	set_hide_tip(st);
	if (!st->focus_visible) ui_window_damage(w);
	st->focus_visible = true;
	if (set_capture_key(st, sym, mods)) return true;
	if (set_dialog_key(st, sym, mods)) return true;
	if (st->popup != POPUP_NONE && set_hist_view_shown(st)) {
		popup_key(st, sym, mods);
		return true;
	}
	if (set_hist_view_key(st, sym)) return true;
	bool ctrl = (mods & KB_MOD_CTRL) != 0;
	xkb_keysym_t lower = xkb_keysym_to_lower(sym);
	struct set_row *fr = st->area == AREA_BODY ? set_focus_row(st) : NULL;
	bool typing = st->area == AREA_SEARCH || set_popup_filtered(st) ||
				  (fr && fr->custom && fr->custom->typing(st));
	if (ctrl && !typing && (lower == XKB_KEY_z || lower == XKB_KEY_y)) {
		bool redo = lower == XKB_KEY_y || (mods & KB_MOD_SHIFT);
		if (st->editing) {
			if (!redo) set_cancel_edit(st);
			return true;
		}
		set_close_popup(st);
		if (redo)
			set_redo(st);
		else
			set_undo(st);
		return true;
	}
	if (ctrl && lower == XKB_KEY_f && st->search.len == 0 && st->page == st->history_page &&
		st->area != AREA_SEARCH && !(mods & KB_MOD_SHIFT) && set_hist_focus_search(st)) {
		set_close_popup(st);
		return true;
	}
	if (ctrl && lower == XKB_KEY_f) {
		if (!set_leave_edit(st)) return true;
		set_close_popup(st);
		st->area = AREA_SEARCH;
		ui_text_select_all(&st->search);
		ui_window_damage(w);
		return true;
	}
	if (ctrl && (sym == XKB_KEY_Page_Down || sym == XKB_KEY_Page_Up)) {
		size_t n = st->n_pages;
		size_t next = sym == XKB_KEY_Page_Down ? (st->page + 1) % n : (st->page + n - 1) % n;
		set_select_page(st, next);
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
	set_hide_tip(st);
	set_capture_end(st, false);
	set_close_popup(st);
}
