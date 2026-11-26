// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/scroll.h"

#include "cairo_util.h"
#include "ui/window.h"
#include "ui_colors.h"

#define UI_THUMB_MIN 28
#define UI_WHEEL_DETENT 10.0

void ui_scroll_set(struct ui_scroll *s, double content_h, double view_h) {
	s->content_h = content_h;
	s->view_h = view_h;
	double max = ui_scroll_max(s);
	if (s->offset > max) s->offset = max;
}

double ui_scroll_max(const struct ui_scroll *s) {
	double over = s->content_h - s->view_h;
	return over > 0 ? over : 0;
}

bool ui_scroll_thumb(const struct ui_scroll *s, double *y, double *h) {
	double max = ui_scroll_max(s);
	if (max <= 0 || s->view_h <= 0) return false;
	double th = s->view_h * (s->view_h / s->content_h);
	if (th < UI_THUMB_MIN) th = UI_THUMB_MIN;
	if (th >= s->view_h) return false;
	*h = th;
	*y = (s->view_h - th) * (s->offset / max);
	return true;
}

static bool ui_scroll_gutter(const struct ui_scroll *s, double width, double x, double *ty,
							 double *th) {
	if (x < width - UI_BAR_GRAB || x >= width) return false;
	return ui_scroll_thumb(s, ty, th);
}

static bool ui_scroll_on_thumb(const struct ui_scroll *s, double width, double x, double y) {
	double ty, th;
	return ui_scroll_gutter(s, width, x, &ty, &th) && y >= ty && y < ty + th;
}

void ui_scroll_to(struct ui_window *w, struct ui_scroll *s, double want) {
	double max = ui_scroll_max(s);
	if (want < 0) want = 0;
	if (want > max) want = max;
	if (want == s->offset) return;
	s->offset = want;
	ui_window_damage(w);
}

void ui_scroll_wheel(struct ui_window *w, struct ui_scroll *s, double units,
					 double step) {
	ui_scroll_to(w, s, s->offset + units / UI_WHEEL_DETENT * step);
}

void ui_scroll_reveal(struct ui_window *w, struct ui_scroll *s, double top,
					  double height) {
	double want = s->offset;
	if (top < want) want = top;
	if (top + height > want + s->view_h) want = top + height - s->view_h;
	ui_scroll_to(w, s, want);
}

bool ui_scroll_press(struct ui_window *w, struct ui_scroll *s, double width,
					 double x, double y) {
	double ty, th;
	if (!ui_scroll_gutter(s, width, x, &ty, &th)) return false;
	if (y >= ty && y < ty + th) {
		s->drag = true;
		s->grab_dy = y - ty;
		ui_window_damage(w);
	} else {
		ui_scroll_to(w, s, s->offset + (y < ty ? -s->view_h : s->view_h));
	}
	return true;
}

bool ui_scroll_motion(struct ui_window *w, struct ui_scroll *s, double width,
					  double x, double y) {
	if (s->drag) {
		double ty, th;
		if (!ui_scroll_thumb(s, &ty, &th)) return true;
		double frac = (y - s->grab_dy) / (s->view_h - th);
		ui_scroll_to(w, s, frac * ui_scroll_max(s));
		return true;
	}
	bool hover = ui_scroll_on_thumb(s, width, x, y);
	if (hover != s->hover) {
		s->hover = hover;
		ui_window_damage(w);
	}
	return hover;
}

void ui_scroll_release(struct ui_window *w, struct ui_scroll *s) {
	if (!s->drag) return;
	s->drag = false;
	ui_window_damage(w);
}

void ui_scroll_draw(cairo_t *cr, const struct ui_scroll *s, double width) {
	double y, th;
	if (!ui_scroll_thumb(s, &y, &th)) return;
	cairo_set_source_rgba(cr, GUI_C_INK, s->drag ? 0.45 : (s->hover ? 0.34 : 0.20));
	grabit_cairo_rect_r(cr, width - UI_BAR_W - 2, y + 2, UI_BAR_W, th - 4,
						UI_BAR_W / 2.0);
	cairo_fill(cr);
}

bool ui_list_key(xkb_keysym_t sym, size_t count, size_t page, size_t *selected) {
	if (count == 0) return false;
	if (page < 1) page = 1;
	size_t want = *selected;
	if (sym == XKB_KEY_Down) {
		want = (*selected + 1) % count;
	} else if (sym == XKB_KEY_Up || sym == XKB_KEY_ISO_Left_Tab) {
		want = *selected == 0 ? count - 1 : *selected - 1;
	} else if (sym == XKB_KEY_Page_Down) {
		want = *selected + page < count ? *selected + page : count - 1;
	} else if (sym == XKB_KEY_Page_Up) {
		want = *selected > page ? *selected - page : 0;
	} else if (sym == XKB_KEY_Home) {
		want = 0;
	} else if (sym == XKB_KEY_End) {
		want = count - 1;
	} else {
		return false;
	}
	*selected = want;
	return true;
}
