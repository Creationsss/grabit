// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_grid.h"

#include "cairo_util.h"
#include "cursor.h"
#include "paths.h"
#include "ui/widgets.h"
#include "ui/window.h"
#include "ui_theme.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define VIEW_PX 2400
#define VIEW_CLOSE ((size_t)100)
#define VIEW_PREV ((size_t)101)
#define VIEW_NEXT ((size_t)102)
#define VIEW_PANEL ((size_t)103)

struct view_job {
	atomic_int refs;
	atomic_bool stop;
	atomic_bool done;
	char *id;
	char *path;
	char *url;
	cairo_surface_t *out;
};

static void job_unref(struct view_job *j) {
	if (atomic_fetch_sub(&j->refs, 1) != 1) return;
	if (j->out) cairo_surface_destroy(j->out);
	free(j->id);
	free(j->path);
	free(j->url);
	free(j);
}

static void *view_thread(void *arg) {
	struct view_job *j = arg;
	if (j->path) {
		j->out = grid_load_fit(j->path, VIEW_PX);
	} else if (j->url && paths_mkdir_p(history_thumb_dir()) == 0) {
		char orig[PATH_MAX], part[PATH_MAX + 16];
		history_thumb_path(j->id, ".orig", orig, sizeof orig);
		if (orig[0] && access(orig, R_OK) != 0) {
			grid_temp_path(orig, part, sizeof part);
			if (grid_fetch(&j->stop, j->url, part) == 0) rename(part, orig);
			unlink(part);
		}
		if (orig[0] && !atomic_load(&j->stop) && access(orig, R_OK) == 0)
			j->out = grid_load_fit(orig, VIEW_PX);
	}
	atomic_store(&j->done, true);
	job_unref(j);
	grid_thread_leave();
	return NULL;
}

static void job_end(struct set_hist_grid *g) {
	if (!g->job) return;
	atomic_store(&g->job->stop, true);
	job_unref(g->job);
	g->job = NULL;
}

static void job_start(struct set_hist_grid *g, const struct history_entry *e) {
	job_end(g);
	bool file = history_has_file(e);
	bool local = !e->video && grid_is_image(e->path) && file;
	bool link = !e->video && !file && e->kind == HISTORY_UPLOAD && e->url;
	if (!local && !link) return;
	struct view_job *j = calloc(1, sizeof *j);
	if (!j) return;
	atomic_store(&j->refs, 2);
	j->id = strdup(e->id);
	j->path = local ? strdup(e->path) : NULL;
	j->url = link ? strdup(e->url) : NULL;
	pthread_t th;
	grid_thread_enter();
	if (!j->id || pthread_create(&th, NULL, view_thread, j) != 0) {
		grid_thread_leave();
		atomic_store(&j->refs, 1);
		job_unref(j);
		return;
	}
	pthread_detach(th);
	g->job = j;
}

static void drop_image(struct set_hist_grid *g) {
	if (g->view_img) cairo_surface_destroy(g->view_img);
	g->view_img = NULL;
	free(g->view_img_id);
	g->view_img_id = NULL;
}

void viewer_free(struct set_hist_grid *g) {
	job_end(g);
	drop_image(g);
	free(g->view_id);
	g->view_id = NULL;
}

void viewer_show(struct settings_state *st, size_t entry) {
	struct set_hist_grid *g = st->grid;
	if (!g || entry >= st->hist.n) return;
	free(g->view_id);
	g->view_id = strdup(st->hist.v[entry].id);
	g->view = g->view_id ? entry : SET_NONE;
	g->sel = entry;
	g->view_hover = g->view_press = g->view_focus = SET_NONE;
	g->view_tip_at = 0;
	if (st->win) ui_window_damage(st->win);
}

void viewer_close(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	job_end(g);
	drop_image(g);
	free(g->view_id);
	g->view_id = NULL;
	g->view = SET_NONE;
	if (st->win) ui_window_damage(st->win);
}

bool set_hist_view_shown(const struct settings_state *st) {
	return st->grid && st->grid->view_id && st->grid->view < st->hist.n;
}

bool viewer_poll(struct settings_state *st) {
	struct set_hist_grid *g = st->grid;
	bool redraw = false;
	if (g->job && atomic_load(&g->job->done)) {
		struct view_job *j = g->job;
		if (j->out && g->view_img_id && strcmp(g->view_img_id, j->id) == 0) {
			if (g->view_img) cairo_surface_destroy(g->view_img);
			g->view_img = j->out;
			j->out = NULL;
		}
		job_unref(j);
		g->job = NULL;
		redraw = true;
	}
	if (g->view_tip_at && !g->tip_drawn && grabit_now_ms() >= g->view_tip_at) {
		g->tip_drawn = true;
		redraw = true;
	}
	return redraw;
}

bool viewer_busy(const struct settings_state *st) {
	const struct set_hist_grid *g = st->grid;
	return g->job || (g->view_tip_at && !g->tip_drawn && set_hist_view_shown(st));
}

static cairo_surface_t *view_image(struct set_hist_grid *g, const struct history_entry *e) {
	if (g->view_img_id && strcmp(g->view_img_id, e->id) == 0) return g->view_img;
	drop_image(g);
	g->view_img_id = strdup(e->id);
	char cache[PATH_MAX];
	history_thumb_path(e->id, ".png", cache, sizeof cache);
	cairo_surface_t *t = thumbs_get(g->th, g->view);
	if (access(cache, R_OK) == 0)
		g->view_img = grid_load_fit(cache, VIEW_PX);
	else if (t)
		g->view_img = cairo_surface_reference(t);
	job_start(g, e);
	return g->view_img;
}

static void arrow(cairo_t *cr, double cx, double cy, double size, int dir) {
	cairo_set_line_width(cr, 2.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to(cr, cx - dir * size * 0.25, cy - size / 2);
	cairo_line_to(cr, cx + dir * size * 0.25, cy);
	cairo_line_to(cr, cx - dir * size * 0.25, cy + size / 2);
	cairo_stroke(cr);
}

static void round_button(cairo_t *cr, struct set_rect r, bool hot) {
	cairo_set_source_rgba(cr, GUI_C_SHADE, hot ? 0.85 : 0.6);
	cairo_arc(cr, r.x + r.w / 2, r.y + r.h / 2, r.w / 2, 0, 6.283185307);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_TEXT, hot ? 1.0 : 0.85);
}

static const char *icon_tip(size_t what) {
	return what == VIEW_PREV ? "Previous (Left)" : what == VIEW_NEXT ? "Next (Right)"
																	 : "Close (Esc)";
}

static size_t neighbour(const struct settings_state *st, int dir) {
	const struct set_hist_grid *g = st->grid;
	size_t pos = grid_order_pos(g, g->view);
	if (pos == SET_NONE) return SET_NONE;
	if (dir < 0) return pos > 0 ? g->items[pos - 1].entry : SET_NONE;
	return pos + 1 < g->n_items ? g->items[pos + 1].entry : SET_NONE;
}

void set_hist_view_draw(struct settings_state *st, cairo_t *cr, double w, double h) {
	if (!set_hist_view_shown(st)) return;
	struct set_hist_grid *g = st->grid;
	const struct history_entry *e = &st->hist.v[g->view];
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.6);
	cairo_paint(cr);
	double m = w < 700 ? 12 : 32, rad = grabit_ui_radius(GUI_R_PANEL), pad = 16;
	g->panel = (struct set_rect){m, m, w - m * 2, h - m * 2};
	struct set_rect p = g->panel;
	cairo_set_source_rgba(cr, GUI_C_POPOVER, 1.0);
	grabit_cairo_rect_r(cr, p.x, p.y, p.w, p.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.14);
	cairo_set_line_width(cr, 1.0);
	grabit_cairo_rect_r_inset(cr, p.x, p.y, p.w, p.h, rad, 1.0);
	cairo_stroke(cr);

	double tw = p.w - pad * 2;
	size_t pos = grid_order_pos(g, g->view);
	char count[48], when[32], where[512], meta[200], size[32] = "";
	if (pos != SET_NONE)
		snprintf(count, sizeof count, "%zu of %zu", pos + 1, g->n_items);
	else
		count[0] = '\0';
	double cw = ui_text_width(cr, count, 11.5, CAIRO_FONT_WEIGHT_NORMAL);
	set_hist_when(e->time, when, sizeof when);
	set_hist_tilde(e->path, where, sizeof where);
	if (g->view < g->n_sizes && g->sizes[g->view] >= 0) grid_size_text(g->sizes[g->view], size, sizeof size);
	snprintf(meta, sizeof meta, "%s%s%s%s%s", when, e->service ? " \xc2\xb7 " : "",
			 e->service ? e->service : "", size[0] ? " \xc2\xb7 " : "", size);
	char url[2][GRID_WRAP_W], path[2][GRID_WRAP_W];
	size_t nu = e->url ? grid_wrap(cr, e->url, tw, 11.5, url, 2) : 0;
	if (e->path && !history_has_file(e))
		strncat(where, " (file is gone)", sizeof where - strlen(where) - 1);
	size_t np = e->path ? grid_wrap(cr, where, tw, 11.5, path, 2) : 0;

	g->n_acts = set_hist_actions(e, g->acts);
	double bh = 30, gap = 8, bx = 0, rows = 1;
	double bw[SET_MAX_ACTIONS];
	for (size_t i = 0; i < g->n_acts; i++) {
		bw[i] = ui_text_width(cr, g->acts[i].label, 12.5, CAIRO_FONT_WEIGHT_NORMAL) + 26;
		if (bx > 0 && bx + bw[i] > tw) {
			rows++;
			bx = 0;
		}
		bx += bw[i] + gap;
	}
	double info = 22 + 18 + (double)(nu + np) * 17 + 10 + rows * (bh + gap);
	struct set_rect img = {p.x + pad, p.y + pad, tw, p.h - pad * 2 - info};
	bool was = g->job != NULL;
	cairo_surface_t *s = img.h > 40 ? view_image(g, e) : NULL;
	if (g->job && !was) set_arm_timer(st);
	if (s) grid_draw_image(cr, s, img.x, img.y, img.w, img.h, 0, false);
	if (s && g->job)
		ui_draw_text_fit(cr, img.x + 12, img.y + 12, img.w / 2, "loading full size\xe2\x80\xa6", 11.5,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.7);
	if (!s && img.h > 40) {
		if (e->video)
			grid_play(cr, img.x + img.w / 2, img.y + img.h / 2, 56, 0.5);
		else
			grid_centered(cr, img.x, img.y + img.h / 2, img.w,
						  thumbs_pending(g->th, g->view) || g->job ? "loading\xe2\x80\xa6"
						  : history_has_file(e)					   ? "this file could not be read"
						  : e->kind == HISTORY_UPLOAD			   ? "no preview for this link"
																   : "file is gone",
						  14, 0.5);
	}

	double nav = 40;
	bool keys = st->focus_visible;
	g->prev = g->next = (struct set_rect){0};
	if (img.h > nav && neighbour(st, -1) != SET_NONE) {
		g->prev = (struct set_rect){img.x + 10, img.y + img.h / 2 - nav / 2, nav, nav};
		round_button(cr, g->prev, g->view_hover == VIEW_PREV);
		arrow(cr, g->prev.x + nav / 2, g->prev.y + nav / 2, 16, -1);
		if (keys && g->view_focus == VIEW_PREV) grid_focus_ring(cr, g->prev, true);
	}
	if (img.h > nav && neighbour(st, 1) != SET_NONE) {
		g->next = (struct set_rect){img.x + img.w - 10 - nav, img.y + img.h / 2 - nav / 2, nav, nav};
		round_button(cr, g->next, g->view_hover == VIEW_NEXT);
		arrow(cr, g->next.x + nav / 2, g->next.y + nav / 2, 16, 1);
		if (keys && g->view_focus == VIEW_NEXT) grid_focus_ring(cr, g->next, true);
	}
	g->close = (struct set_rect){p.x + p.w - pad - 32 - 6, p.y + pad + 6, 32, 32};
	round_button(cr, g->close, g->view_hover == VIEW_CLOSE);
	grabit_cairo_cross(cr, g->close.x + 16, g->close.y + 16, 6, 2.0);
	if (keys && g->view_focus == VIEW_CLOSE) grid_focus_ring(cr, g->close, true);

	double iy = img.y + img.h + 12;
	ui_draw_text_fit(cr, img.x + img.w - cw, iy + 2, cw + 1, count, 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	ui_draw_text_fit(cr, img.x, iy, tw - cw - 16, set_hist_name(e), 15, CAIRO_FONT_WEIGHT_BOLD, 0.95);
	iy += 22;
	ui_draw_text_fit(cr, img.x, iy, tw, meta, 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.6);
	iy += 18;
	for (size_t i = 0; i < nu; i++, iy += 17)
		ui_draw_text_fit(cr, img.x, iy, tw, url[i], 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.6);
	for (size_t i = 0; i < np; i++, iy += 17)
		ui_draw_text_fit(cr, img.x, iy, tw, path[i], 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.6);
	iy += 10;
	bx = img.x;
	for (size_t i = 0; i < g->n_acts; i++) {
		if (bx > img.x && bx + bw[i] > img.x + tw) {
			bx = img.x;
			iy += bh + gap;
		}
		g->btn[i] = (struct set_rect){bx, iy, bw[i], bh};
		ui_button_draw(cr, bx, iy, bw[i], bh, g->acts[i].label, g->view_hover == i,
					   g->view_press == i);
		if (keys && g->view_focus == i) grid_focus_ring(cr, g->btn[i], false);
		bx += bw[i] + gap;
	}

	size_t tip = g->view_hover;
	if ((tip == VIEW_PREV || tip == VIEW_NEXT || tip == VIEW_CLOSE) && g->view_tip_at &&
		grabit_now_ms() >= g->view_tip_at) {
		struct set_rect b = tip == VIEW_PREV ? g->prev : tip == VIEW_NEXT ? g->next
																		  : g->close;
		const char *text = icon_tip(tip);
		double lw = ui_text_width(cr, text, 11.5, CAIRO_FONT_WEIGHT_NORMAL) + 16;
		double lx = b.x + b.w / 2 - lw / 2;
		if (lx + lw > p.x + p.w - 4) lx = p.x + p.w - 4 - lw;
		if (lx < p.x + 4) lx = p.x + 4;
		double ly = b.y + b.h + 6;
		cairo_set_source_rgba(cr, GUI_C_SURFACE, 1.0);
		grabit_cairo_rect_r(cr, lx, ly, lw, 24, grabit_ui_radius(GUI_R_BTN));
		cairo_fill(cr);
		ui_draw_text_fit(cr, lx + 8, ly + 4, lw, text, 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.9);
	}
}

static size_t view_hit(const struct set_hist_grid *g, double x, double y) {
	for (size_t i = 0; i < g->n_acts; i++)
		if (g->btn[i].w > 0 && set_rect_contains(&g->btn[i], x, y)) return i;
	if (set_rect_contains(&g->close, x, y)) return VIEW_CLOSE;
	if (g->prev.w > 0 && set_rect_contains(&g->prev, x, y)) return VIEW_PREV;
	if (g->next.w > 0 && set_rect_contains(&g->next, x, y)) return VIEW_NEXT;
	if (set_rect_contains(&g->panel, x, y)) return VIEW_PANEL;
	return SET_NONE;
}

static void step(struct settings_state *st, int dir) {
	struct set_hist_grid *g = st->grid;
	size_t focus = g->view_focus, to = neighbour(st, dir);
	if (to == SET_NONE) return;
	viewer_show(st, to);
	g->view_focus = focus >= SET_MAX_ACTIONS ? focus : SET_NONE;
}

static void run(struct settings_state *st, size_t i) {
	struct set_hist_grid *g = st->grid;
	if (i == VIEW_CLOSE) {
		viewer_close(st);
		return;
	}
	if (i == VIEW_PREV || i == VIEW_NEXT) {
		step(st, i == VIEW_PREV ? -1 : 1);
		return;
	}
	if (i >= g->n_acts) return;
	set_toast_action fn = g->acts[i].fn;
	char *id = strdup(g->view_id);
	if (id && fn) fn(st, id);
	free(id);
}

bool set_hist_view_press(struct settings_state *st, double x, double y, uint32_t button,
						 bool pressed) {
	if (!set_hist_view_shown(st)) return false;
	struct set_hist_grid *g = st->grid;
	size_t hit = view_hit(g, x, y);
	if (hit == VIEW_PANEL) hit = SET_NONE;
	if (pressed) {
		g->view_press = button == BTN_LEFT ? hit : SET_NONE;
		g->view_tip_at = 0;
		if (st->win) ui_window_damage(st->win);
		return true;
	}
	size_t was = g->view_press;
	g->view_press = SET_NONE;
	if (st->win) ui_window_damage(st->win);
	if (hit == was && hit != SET_NONE) run(st, hit);
	return true;
}

bool set_hist_view_motion(struct settings_state *st, double x, double y) {
	if (!set_hist_view_shown(st)) return false;
	struct set_hist_grid *g = st->grid;
	size_t hit = view_hit(g, x, y);
	if (hit == VIEW_PANEL) hit = SET_NONE;
	st->win->content_cursor = hit == SET_NONE ? GCUR_DEFAULT : GCUR_HAND;
	if (hit != g->view_hover) {
		g->view_hover = hit;
		bool icon = hit == VIEW_PREV || hit == VIEW_NEXT || hit == VIEW_CLOSE;
		g->view_tip_at = icon ? grabit_now_ms() + SET_TIP_DELAY_MS : 0;
		g->tip_drawn = false;
		ui_window_damage(st->win);
		set_arm_timer(st);
	}
	return true;
}

static void tab(struct settings_state *st, bool back) {
	struct set_hist_grid *g = st->grid;
	size_t stops[SET_MAX_ACTIONS + 3], n = 0, at;
	for (size_t i = 0; i < g->n_acts; i++)
		stops[n++] = i;
	if (neighbour(st, -1) != SET_NONE) stops[n++] = VIEW_PREV;
	if (neighbour(st, 1) != SET_NONE) stops[n++] = VIEW_NEXT;
	stops[n++] = VIEW_CLOSE;
	for (at = 0; at < n && stops[at] != g->view_focus; at++)
		;
	if (at == n)
		at = back ? n - 1 : 0;
	else
		at = (at + (back ? n - 1 : 1)) % n;
	g->view_focus = stops[at];
}

bool set_hist_view_key(struct settings_state *st, xkb_keysym_t sym) {
	if (!set_hist_view_shown(st)) return false;
	struct set_hist_grid *g = st->grid;
	switch (sym) {
	case XKB_KEY_Escape:
		viewer_close(st);
		break;
	case XKB_KEY_Left:
		step(st, -1);
		break;
	case XKB_KEY_Right:
		step(st, 1);
		break;
	case XKB_KEY_Tab:
		tab(st, (ui_window_mods(st->win) & KB_MOD_SHIFT) != 0);
		break;
	case XKB_KEY_ISO_Left_Tab:
		tab(st, true);
		break;
	case XKB_KEY_Return:
	case XKB_KEY_space:
		if (g->view_focus != SET_NONE) run(st, g->view_focus);
		break;
	default:
		break;
	}
	if (st->win) ui_window_damage(st->win);
	return true;
}
