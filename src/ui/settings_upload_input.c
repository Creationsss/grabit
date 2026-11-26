// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_upload.h"

#include "clipboard/clipboard.h"
#include "ui/widgets.h"
#include "ui/window.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static bool in(const struct set_rect *r, double x, double y) {
	return r->w > 0 && set_rect_contains(r, x, y);
}

size_t upq_hit(struct settings_state *st, const struct set_row *r, double bx, double by) {
	struct set_upq *q = st->upq;
	struct upq_layout l;
	upq_layout(ui_measure_cr(), st, r, &l);
	struct set_rect box, rm;
	if (upq_popover(st, &l, &box, &rm) && in(&box, bx, by))
		return in(&rm, bx, by) ? UH_POP_REMOVE : UH_POP;
	if (in(&l.field, bx, by)) return UH_FIELD;
	if (in(&l.zone, bx, by)) return UH_ZONE;
	if (in(&l.res, bx, by)) {
		if (in(&l.res_close, bx, by)) return UH_RES_CLOSE;
		if (in(&l.copy_all, bx, by)) return UH_COPY_ALL;
		for (size_t i = 0; i < q->n_res; i++) {
			if (!q->res[i].url[0]) continue;
			struct set_rect open, copy;
			upq_res_icons(&l, i, &open, &copy);
			if (in(&open, bx, by)) return UH_RES_OPEN + i;
			if (in(&copy, bx, by)) return UH_RES_COPY + i;
		}
		return UH_NONE;
	}
	for (size_t i = 0; i < l.shown; i++) {
		struct set_rect c = upq_card(&l, i);
		if (in(&c, bx, by)) return UH_CARD + i;
	}
	if (in(&l.more, bx, by)) return UH_MORE;
	if (in(&l.all, bx, by)) return UH_ALL;
	if (in(&l.clear, bx, by)) return UH_CLEAR;
	if (in(&l.options, bx, by)) return UH_OPTIONS;
	if (in(&l.go, bx, by)) return UH_GO;
	return UH_NONE;
}

static double height(struct settings_state *st, struct set_row *r) {
	struct upq_layout l;
	upq_layout(ui_measure_cr(), st, r, &l);
	return l.bottom - r->y;
}

static void add_input(struct settings_state *st) {
	struct set_upq *q = st->upq;
	if (!q->input.len) return;
	char err[200];
	size_t n = upq_add_text(st, q->input.buf, err, sizeof err);
	if (err[0])
		set_toast(st, "%s", err);
	else if (n == 0)
		set_toast(st, "That file is already in the list");
	if (n && !err[0]) ui_text_set(&q->input, "");
}

static bool enabled(const struct set_upq *q, size_t h) {
	bool idle = !upq_busy(q);
	if (h == UH_CLEAR || h == UH_GO) return idle && q->n > 0;
	if (h == UH_MORE || h == UH_ALL || h == UH_POP_REMOVE) return idle;
	return true;
}

static void run(struct settings_state *st, struct set_row *r, size_t h, double bx) {
	struct set_upq *q = st->upq;
	if (!enabled(q, h)) return;
	if (h == UH_FIELD) {
		struct upq_layout l;
		upq_layout(ui_measure_cr(), st, r, &l);
		ui_text_click(&q->input, bx - l.field.x, false,
					  (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0);
	} else if (h == UH_GO) {
		upq_start(st);
	} else if (h == UH_CLEAR) {
		upq_clear_files(st);
	} else if (h == UH_OPTIONS) {
		upq_set_options(st, !q->options_open);
	} else if (h == UH_MORE) {
		q->shown += UPQ_SHOWN;
	} else if (h == UH_ALL) {
		q->shown = q->n;
	} else if (h == UH_RES_CLOSE) {
		upq_close_results(st);
	} else if (h == UH_COPY_ALL) {
		upq_copy_all(st);
	} else if (h == UH_POP_REMOVE) {
		upq_remove_at(st, q->pop);
	} else if (UH_IN(h, UH_RES_OPEN)) {
		set_open_target(st, q->res[h - UH_RES_OPEN].url);
	} else if (UH_IN(h, UH_RES_COPY)) {
		set_copy_text(st, q->res[h - UH_RES_COPY].url, "link");
	}
}

static void press(struct settings_state *st, struct set_row *r, double bx, double by,
				  uint32_t button) {
	struct set_upq *q = st->upq;
	if (button != BTN_LEFT) return;
	size_t h = upq_hit(st, r, bx, by);
	q->typing = h == UH_ZONE || h == UH_FIELD;
	run(st, r, h, bx);
	ui_window_damage(st->win);
}

static int motion(struct settings_state *st, struct set_row *r, double bx, double by) {
	struct set_upq *q = st->upq;
	size_t h = r ? upq_hit(st, r, bx, by) : UH_NONE;
	size_t pop = q->pop;
	if (UH_IN(h, UH_CARD))
		pop = h - UH_CARD;
	else if (h != UH_POP && h != UH_POP_REMOVE)
		pop = SET_NONE;
	if (h != q->hover || pop != q->pop) {
		q->hover = h;
		q->pop = pop;
		ui_window_damage(st->win);
	}
	if (h == UH_FIELD) return 2;
	if (h == UH_ZONE) return 1;
	if (h == UH_NONE || h == UH_POP || UH_IN(h, UH_CARD)) return 0;
	return enabled(q, h) ? 1 : 0;
}

static bool capture(struct settings_state *st, struct set_row *r, double bx, double by) {
	struct upq_layout l;
	struct set_rect box, rm;
	upq_layout(ui_measure_cr(), st, r, &l);
	return upq_popover(st, &l, &box, &rm) && in(&box, bx, by);
}

static bool key(struct settings_state *st, struct set_row *r, xkb_keysym_t sym, uint8_t mods) {
	(void)r;
	struct set_upq *q = st->upq;
	bool ctrl = (mods & KB_MOD_CTRL) != 0;
	if (ctrl && xkb_keysym_to_lower(sym) == XKB_KEY_v && !q->typing) {
		upq_paste(st, r);
		return true;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (q->typing && q->input.len)
			add_input(st);
		else if (q->typing)
			q->typing = false;
		else
			upq_start(st);
		ui_window_damage(st->win);
		return true;
	}
	if (sym == XKB_KEY_Escape && q->typing) {
		if (q->input.len)
			ui_text_set(&q->input, "");
		else
			q->typing = false;
		ui_window_damage(st->win);
		return true;
	}
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab || sym == XKB_KEY_Up ||
		sym == XKB_KEY_Down || sym == XKB_KEY_Page_Up || sym == XKB_KEY_Page_Down) {
		q->typing = false;
		return false;
	}
	if (!q->typing) {
		if (ctrl || sym == XKB_KEY_Escape || sym == XKB_KEY_space) return false;
		if (xkb_keysym_to_utf32(sym) < 0x20) return false;
	}
	enum ui_text_result res = ui_text_key(&q->input, sym, mods, false);
	if (res == UI_TEXT_HANDLED) {
		q->typing = true;
		ui_window_damage(st->win);
		return true;
	}
	return false;
}

static bool typing(const struct settings_state *st) {
	return st->upq && st->upq->typing;
}

static void drop(struct settings_state *st, struct set_row *r, const char *uri_list) {
	(void)r;
	set_upload_drop(st, uri_list);
}

const struct set_custom SET_UPQ_CUSTOM = {
	.height = height,
	.draw = upq_draw,
	.press = press,
	.motion = motion,
	.key = key,
	.typing = typing,
	.overlay = upq_draw_overlay,
	.capture = capture,
	.drop = drop,
	.paste = upq_paste,
};
