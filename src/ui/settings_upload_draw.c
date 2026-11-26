// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_upload.h"

#include "cairo_util.h"
#include "ui/settings_grid.h"
#include "ui/widgets.h"
#include "ui_theme.h"
#include "util/util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double inner_w(const struct settings_state *st) {
	return set_body_w(st) - SET_BODY_PAD * 2;
}

static bool field_shown(const struct set_upq *q) {
	return q->typing || q->input.len > 0;
}

void upq_go_label(const struct set_upq *q, char *buf, size_t n) {
	char size[32];
	grid_size_text(upq_total(q), size, sizeof size);
	if (q->n == 1)
		snprintf(buf, n, "Upload 1 file (%s)", size);
	else
		snprintf(buf, n, "Upload %zu files (%s)", q->n, size);
}

enum glyph { G_NONE,
			 G_UPLOAD,
			 G_TRASH,
			 G_GEAR,
			 G_OPEN,
			 G_COPY,
			 G_CLOSE };

#define BTN_PAD 28.0
#define BTN_ICON 24.0
#define BTN_PILL 28.0

static double content_w(cairo_t *cr, const char *label, enum glyph g, size_t badge) {
	return ui_text_width(cr, label, 13, CAIRO_FONT_WEIGHT_NORMAL) + (g == G_NONE ? 0 : BTN_ICON) +
		   (badge ? BTN_PILL : 0);
}

static double button_w(cairo_t *cr, const char *label, enum glyph g, size_t badge) {
	return content_w(cr, label, g, badge) + BTN_PAD;
}

static size_t ok_results(const struct set_upq *q) {
	size_t ok = 0;
	for (size_t i = 0; i < q->n_res; i++)
		ok += !q->res[i].failed;
	return ok;
}

static void copy_all_label(const struct set_upq *q, char *out, size_t n) {
	size_t ok = ok_results(q);
	snprintf(out, n, "Copy %zu link%s", ok, ok == 1 ? "" : "s");
}

static void hidden_label(const struct set_upq *q, size_t shown, char *out, size_t n) {
	snprintf(out, n, "%zu more files hidden", q->n - shown);
}

void upq_layout(cairo_t *cr, struct settings_state *st, const struct set_row *r,
				struct upq_layout *l) {
	struct set_upq *q = st->upq;
	double x = SET_BODY_PAD, w = inner_w(st), y = r->y + 6;
	*l = (struct upq_layout){0};
	l->zone = (struct set_rect){x, y, w, UPQ_ZONE_H};
	if (field_shown(q)) {
		double fw = w - 80 < 460 ? w - 80 : 460;
		l->field = (struct set_rect){x + (w - fw) / 2, y + UPQ_ZONE_H - 52, fw, 32};
	}
	y += UPQ_ZONE_H + 12;
	if (upq_busy(q)) {
		l->bar = (struct set_rect){x, y, w, 24};
		y += 24 + 10;
		l->info = (struct set_rect){x, y, w, 34};
		y += 34 + 12;
	}
	if (q->n_res) {
		double h = 50 + (double)q->n_res * UPQ_RES_ROW + 50;
		l->res = (struct set_rect){x, y, w, h};
		l->res_close = (struct set_rect){x + w - 36, y + 10, 26, 26};
		l->res_rows = y + 50;
		char all[48];
		copy_all_label(q, all, sizeof all);
		double cw = ok_results(q) ? button_w(cr, all, G_NONE, 0) : 0;
		l->copy_all = (struct set_rect){x + 16, y + h - 44, cw, 32};
		y += h + 12;
	}
	double n = floor((w + UPQ_GAP) / (UPQ_CARD_MIN_W + UPQ_GAP));
	l->cols = n < 1 ? 1 : n > 4 ? 4
								: (size_t)n;
	l->card_w = (w - (double)(l->cols - 1) * UPQ_GAP) / (double)l->cols;
	l->cards_top = y;
	l->shown = q->n < q->shown ? q->n : q->shown;
	size_t rows = (l->shown + l->cols - 1) / l->cols;
	if (rows) y += (double)rows * (UPQ_CARD_H + UPQ_GAP);
	if (l->shown < q->n) {
		double mw = button_w(cr, "Show more", G_NONE, 0), aw = button_w(cr, "Show all", G_NONE, 0);
		char hid[64];
		hidden_label(q, l->shown, hid, sizeof hid);
		double tw = ui_text_width(cr, hid, 12.5, CAIRO_FONT_WEIGHT_NORMAL);
		double gx = x + (w - (tw + 12 + mw + 8 + aw)) / 2;
		l->hidden_x = gx;
		l->more = (struct set_rect){gx + tw + 12, y, mw, 26};
		l->all = (struct set_rect){l->more.x + mw + 8, y, aw, 26};
		y += 26 + 12;
	}
	y += 4;
	char label[96];
	upq_go_label(q, label, sizeof label);
	double gw = button_w(cr, label, G_UPLOAD, 0);
	double ow = button_w(cr, "Options", G_GEAR, upq_changes(q));
	double cw = button_w(cr, "Clear all", G_TRASH, 0);
	l->go = (struct set_rect){x + w - gw, y, gw, UPQ_BTN_H};
	l->options = (struct set_rect){l->go.x - 10 - ow, y, ow, UPQ_BTN_H};
	l->clear = (struct set_rect){l->options.x - 10 - cw, y, cw, UPQ_BTN_H};
	y += UPQ_BTN_H + 12;
	l->bottom = y;
}

struct set_rect upq_card(const struct upq_layout *l, size_t i) {
	return (struct set_rect){SET_BODY_PAD + (double)(i % l->cols) * (l->card_w + UPQ_GAP),
							 l->cards_top + (double)(i / l->cols) * (UPQ_CARD_H + UPQ_GAP),
							 l->card_w, UPQ_CARD_H};
}

void upq_res_icons(const struct upq_layout *l, size_t i, struct set_rect *open,
				   struct set_rect *copy) {
	double y = l->res_rows + (double)i * UPQ_RES_ROW;
	*copy = (struct set_rect){l->res.x + l->res.w - 44, y + 1, 28, 28};
	*open = (struct set_rect){copy->x - 32, y + 1, 28, 28};
}

bool upq_popover(const struct settings_state *st, const struct upq_layout *l, struct set_rect *box,
				 struct set_rect *remove) {
	const struct set_upq *q = st->upq;
	if (q->pop >= l->shown || upq_busy(q)) return false;
	struct set_rect c = upq_card(l, q->pop);
	double w = UPQ_POP_W < inner_w(st) ? UPQ_POP_W : inner_w(st);
	double h = 14 + UPQ_POP_PREVIEW + 12 + 20 + 18 + 12 + 30 + 14;
	double x = c.x + c.w / 2 - w / 2;
	if (x < SET_BODY_PAD) x = SET_BODY_PAD;
	if (x + w > SET_BODY_PAD + inner_w(st)) x = SET_BODY_PAD + inner_w(st) - w;
	double y = c.y - 8 - h;
	if (y < st->body.offset + 4) y = c.y + c.h + 8;
	*box = (struct set_rect){x, y, w, h};
	*remove = (struct set_rect){x + 14, y + h - 14 - 30, w - 28, 30};
	return true;
}

static void stroke_style(cairo_t *cr, double width) {
	cairo_set_line_width(cr, width);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

static void doc_path(cairo_t *cr, double x, double y, double w, double h) {
	double f = w * 0.32;
	cairo_move_to(cr, x, y + 2);
	cairo_line_to(cr, x, y + h - 2);
	cairo_curve_to(cr, x, y + h, x, y + h, x + 2, y + h);
	cairo_line_to(cr, x + w - 2, y + h);
	cairo_curve_to(cr, x + w, y + h, x + w, y + h, x + w, y + h - 2);
	cairo_line_to(cr, x + w, y + f);
	cairo_line_to(cr, x + w - f, y);
	cairo_line_to(cr, x + 2, y);
	cairo_curve_to(cr, x, y, x, y, x, y + 2);
	cairo_move_to(cr, x + w - f, y);
	cairo_line_to(cr, x + w - f, y + f);
	cairo_line_to(cr, x + w, y + f);
}

static void files_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, s / 22);
	double w = s * 0.5, h = s * 0.66;
	doc_path(cr, cx - w / 2 + s * 0.08, cy - h / 2 - s * 0.06, w, h);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - w / 2 - s * 0.08, cy - h / 2 + s * 0.12);
	cairo_line_to(cr, cx - w / 2 - s * 0.08, cy + h / 2 + s * 0.08);
	cairo_line_to(cr, cx + w / 2 - s * 0.14, cy + h / 2 + s * 0.08);
	cairo_stroke(cr);
}

static void arrow_up(cairo_t *cr, double cx, double cy, double s) {
	cairo_move_to(cr, cx, cy + s / 2);
	cairo_line_to(cr, cx, cy - s / 2);
	cairo_move_to(cr, cx - s * 0.38, cy - s * 0.12);
	cairo_line_to(cr, cx, cy - s / 2);
	cairo_line_to(cr, cx + s * 0.38, cy - s * 0.12);
}

static void upload_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, s / 22);
	arrow_up(cr, cx, cy - s * 0.12, s * 0.55);
	cairo_move_to(cr, cx - s * 0.4, cy + s * 0.12);
	cairo_line_to(cr, cx - s * 0.4, cy + s * 0.36);
	cairo_line_to(cr, cx + s * 0.4, cy + s * 0.36);
	cairo_line_to(cr, cx + s * 0.4, cy + s * 0.12);
	cairo_stroke(cr);
}

static void file_upload_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, s / 24);
	double w = s * 0.62, h = s * 0.8;
	doc_path(cr, cx - w / 2, cy - h / 2, w, h);
	arrow_up(cr, cx, cy + h * 0.12, h * 0.36);
	cairo_stroke(cr);
}

static void trash_icon(cairo_t *cr, double cx, double cy, double s) {
	cairo_rectangle(cr, cx - s * 0.36, cy - s * 0.3, s * 0.72, s * 0.1);
	cairo_fill(cr);
	cairo_rectangle(cr, cx - s * 0.12, cy - s * 0.42, s * 0.24, s * 0.1);
	cairo_fill(cr);
	cairo_move_to(cr, cx - s * 0.28, cy - s * 0.16);
	cairo_line_to(cr, cx + s * 0.28, cy - s * 0.16);
	cairo_line_to(cr, cx + s * 0.22, cy + s * 0.42);
	cairo_line_to(cr, cx - s * 0.22, cy + s * 0.42);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void gear_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, s / 12);
	for (int i = 0; i < 8; i++) {
		double a = i * M_PI / 4;
		cairo_move_to(cr, cx + cos(a) * s * 0.28, cy + sin(a) * s * 0.28);
		cairo_line_to(cr, cx + cos(a) * s * 0.44, cy + sin(a) * s * 0.44);
	}
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy, s * 0.28, 0, 2 * M_PI);
	cairo_stroke(cr);
	cairo_arc(cr, cx, cy, s * 0.1, 0, 2 * M_PI);
	cairo_fill(cr);
}

static void copy_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, 1.5);
	grabit_cairo_rect_r(cr, cx - s * 0.2, cy - s * 0.2, s * 0.6, s * 0.6, 2);
	cairo_stroke(cr);
	cairo_move_to(cr, cx - s * 0.4, cy + s * 0.2);
	cairo_line_to(cr, cx - s * 0.4, cy - s * 0.4);
	cairo_line_to(cr, cx + s * 0.2, cy - s * 0.4);
	cairo_stroke(cr);
}

static void open_icon(cairo_t *cr, double cx, double cy, double s) {
	stroke_style(cr, 1.5);
	cairo_move_to(cr, cx, cy - s * 0.4);
	cairo_line_to(cr, cx - s * 0.4, cy - s * 0.4);
	cairo_line_to(cr, cx - s * 0.4, cy + s * 0.4);
	cairo_line_to(cr, cx + s * 0.4, cy + s * 0.4);
	cairo_line_to(cr, cx + s * 0.4, cy);
	cairo_move_to(cr, cx - s * 0.05, cy + s * 0.05);
	cairo_line_to(cr, cx + s * 0.45, cy - s * 0.45);
	cairo_move_to(cr, cx + s * 0.12, cy - s * 0.45);
	cairo_line_to(cr, cx + s * 0.45, cy - s * 0.45);
	cairo_line_to(cr, cx + s * 0.45, cy - s * 0.12);
	cairo_stroke(cr);
}

static void glyph(cairo_t *cr, enum glyph g, double cx, double cy) {
	if (g == G_UPLOAD)
		upload_icon(cr, cx, cy, 16);
	else if (g == G_TRASH)
		trash_icon(cr, cx, cy, 15);
	else if (g == G_GEAR)
		gear_icon(cr, cx, cy, 15);
	else if (g == G_OPEN)
		open_icon(cr, cx, cy, 14);
	else if (g == G_COPY)
		copy_icon(cr, cx + 1, cy + 1, 14);
	else if (g == G_CLOSE)
		grabit_cairo_cross(cr, cx, cy, 5, 1.6);
}

static void button(cairo_t *cr, struct set_rect b, const char *label, enum glyph g, bool hot,
				   bool enabled, size_t badge) {
	static const double RAISED[] = {GUI_C_RAISED};
	double a = enabled ? 1.0 : 0.4;
	if (!enabled) cairo_push_group(cr);
	ui_button_draw_color(cr, b.x, b.y, b.w, b.h, NULL, RAISED, enabled && hot, false);
	if (!enabled) {
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, 0.55);
	}
	char n[24] = "";
	if (badge) snprintf(n, sizeof n, "%zu", badge);
	double tw = ui_text_width(cr, label, 13, CAIRO_FONT_WEIGHT_NORMAL);
	double icon = g == G_NONE ? 0 : BTN_ICON;
	double x = b.x + (b.w - content_w(cr, label, g, badge)) / 2, cy = b.y + b.h / 2;
	cairo_set_source_rgba(cr, GUI_C_TEXT, 0.9 * a);
	glyph(cr, g, x + 8, cy);
	ui_draw_text_fit(cr, x + icon, cy - 9, tw + 2, label, 13, CAIRO_FONT_WEIGHT_NORMAL, a);
	if (!badge) return;
	double px = x + icon + tw + 8;
	grabit_ui_accent(cr, 1.0);
	grabit_cairo_rect_r(cr, px, cy - 9, 20, 18, 9);
	cairo_fill(cr);
	double nw = ui_text_width(cr, n, 10.5, CAIRO_FONT_WEIGHT_BOLD);
	ui_draw_text_fit_rgba(cr, px + (20 - nw) / 2, cy - 7.5, 20, n, 10.5, CAIRO_FONT_WEIGHT_BOLD,
						  GUI_C_SHADE, 0.9);
}

static double kbd(cairo_t *cr, double x, double y, const char *key) {
	double w = ui_text_width(cr, key, 11, CAIRO_FONT_WEIGHT_BOLD) + 12;
	cairo_set_source_rgba(cr, GUI_C_INK, 0.08);
	grabit_cairo_rect_r(cr, x, y, w, 20, 4);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.22);
	cairo_set_line_width(cr, 1);
	grabit_cairo_rect_r_inset(cr, x, y, w, 20, 4, 1);
	cairo_stroke(cr);
	ui_draw_text_fit(cr, x + 6, y + 2, w, key, 11, CAIRO_FONT_WEIGHT_BOLD, 0.85);
	return w;
}

static void draw_zone(struct settings_state *st, cairo_t *cr, const struct upq_layout *l,
					  bool focused) {
	struct set_upq *q = st->upq;
	struct set_rect z = l->zone;
	double rad = grabit_ui_radius(GUI_R_PANEL);
	bool drag = st->drag_over, hot = q->hover == UH_ZONE || drag;
	cairo_set_source_rgba(cr, GUI_C_INK, hot ? 0.06 : 0.03);
	if (drag) grabit_ui_accent(cr, 0.1);
	grabit_cairo_rect_r(cr, z.x, z.y, z.w, z.h, rad);
	cairo_fill(cr);
	cairo_save(cr);
	double dash[] = {8, 6};
	cairo_set_dash(cr, dash, 2, 0);
	cairo_set_line_width(cr, 1.5);
	if (drag)
		grabit_ui_accent(cr, 0.9);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, hot ? 0.4 : 0.25);
	grabit_cairo_rect_r_inset(cr, z.x, z.y, z.w, z.h, rad, 1.5);
	cairo_stroke(cr);
	cairo_restore(cr);

	const char *title = "Drag files here or click to type a path";
	const char *desc = "Add as many files as you like, they show up below to review before "
					   "uploading.";
	double tw = ui_text_width(cr, title, 19, CAIRO_FONT_WEIGHT_NORMAL);
	double dw = ui_text_width(cr, desc, 13, CAIRO_FONT_WEIGHT_NORMAL);
	double block = tw > dw ? tw : dw, icon = 52, gap = 28;
	double avail = z.w - 48 - icon - gap;
	if (block > avail) block = avail;
	double gx = z.x + (z.w - (icon + gap + block)) / 2;
	double area = l->field.w > 0 ? z.h - 60 : z.h;
	double cy = z.y + area / 2;
	if (drag)
		grabit_ui_accent(cr, 1.0);
	else
		cairo_set_source_rgba(cr, GUI_C_TEXT, 0.85);
	if (drag)
		upload_icon(cr, gx + icon / 2, cy, icon);
	else
		files_icon(cr, gx + icon / 2, cy, icon);
	double tx = gx + icon + gap;
	ui_draw_text_fit(cr, tx, cy - 38, block, drag ? "Drop to add these files" : title, 19,
					 CAIRO_FONT_WEIGHT_NORMAL, 0.95);
	double kx = tx, ky = cy - 6;
	ui_draw_text_fit(cr, kx, ky + 1, 40, "Or", 13, CAIRO_FONT_WEIGHT_NORMAL, 0.85);
	kx += ui_text_width(cr, "Or ", 13, CAIRO_FONT_WEIGHT_NORMAL) + 2;
	kx += kbd(cr, kx, ky, "Ctrl") + 5;
	ui_draw_text_fit(cr, kx, ky + 1, 20, "+", 13, CAIRO_FONT_WEIGHT_NORMAL, 0.85);
	kx += ui_text_width(cr, "+", 13, CAIRO_FONT_WEIGHT_NORMAL) + 5;
	kx += kbd(cr, kx, ky, "V") + 6;
	ui_draw_text_fit(cr, kx, ky + 1, tx + block - kx, "to paste copied files or paths", 13,
					 CAIRO_FONT_WEIGHT_NORMAL, 0.85);
	ui_draw_text_fit(cr, tx, cy + 22, block, desc, 13, CAIRO_FONT_WEIGHT_NORMAL, 0.5);
	if (l->field.w > 0)
		ui_text_draw(cr, focused && q->typing ? &q->input : NULL, l->field.x, l->field.y, l->field.w,
					 l->field.h, 0, false, q->input.buf, "~/Pictures/screenshot.png, then Enter");
}

static void human_time(double secs, char *out, size_t n) {
	if (secs < 60)
		snprintf(out, n, "%.0fs", secs < 1 ? 1 : secs);
	else if (secs < 3600)
		snprintf(out, n, "%.0fm %.0fs", floor(secs / 60), fmod(secs, 60));
	else
		snprintf(out, n, "%.0fh %.0fm", floor(secs / 3600), fmod(secs / 60, 60));
}

static void draw_progress(struct settings_state *st, cairo_t *cr, const struct upq_layout *l) {
	struct set_upq *q = st->upq;
	struct set_rect b = l->bar;
	double p = upq_progress(q), rad = b.h / 2;
	cairo_set_source_rgba(cr, GUI_C_INK, 0.1);
	grabit_cairo_rect_r(cr, b.x, b.y, b.w, b.h, rad);
	cairo_fill(cr);
	if (p > 0) {
		double fw = b.w * p < b.h ? b.h : b.w * p;
		grabit_ui_accent(cr, 1.0);
		grabit_cairo_rect_r(cr, b.x, b.y, fw, b.h, rad);
		cairo_fill(cr);
		char pct[16];
		snprintf(pct, sizeof pct, "%.0f%%", floor(p * 100));
		double pw = ui_text_width(cr, pct, 11.5, CAIRO_FONT_WEIGHT_BOLD);
		if (fw > pw + 16)
			ui_draw_text_fit_rgba(cr, b.x + fw / 2 - pw / 2, b.y + 4, pw + 2, pct, 11.5,
								  CAIRO_FONT_WEIGHT_BOLD, 1, 1, 1, 1);
	}
	struct set_rect in = l->info;
	cairo_set_source_rgba(cr, GUI_C_INK, 0.14);
	cairo_set_line_width(cr, 1);
	grabit_cairo_rect_r_inset(cr, in.x, in.y, in.w, in.h, grabit_ui_radius(GUI_R_BTN), 1);
	cairo_stroke(cr);
	char text[256], spd[32], left[32];
	const char *name = q->cur < q->n ? grabit_basename(q->files[q->cur].path) : "";
	size_t all = q->done + q->failed;
	for (size_t i = 0; i < q->n; i++)
		all += q->files[i].state == UQ_WAIT || q->files[i].state == UQ_UPLOADING;
	if (upq_finalizing(q)) {
		snprintf(text, sizeof text, "Finalizing %s (%zu of %zu)\xe2\x80\xa6", name,
				 q->done + q->failed + 1, all);
		double w = ui_text_width(cr, text, 12.5, CAIRO_FONT_WEIGHT_BOLD);
		ui_draw_text_fit_rgba(cr, in.x + (in.w - w) / 2, in.y + 9, in.w, text, 12.5,
							  CAIRO_FONT_WEIGHT_BOLD, GUI_C_WARNING, 1);
		return;
	}
	if (q->speed > 1) {
		grid_size_text((int64_t)q->speed, spd, sizeof spd);
		double rest = (double)(q->batch_total - q->batch_sent - q->cur_sent) / q->speed;
		human_time(rest < 0 ? 0 : rest, left, sizeof left);
		snprintf(text, sizeof text, "%s/s, %s remaining \xc2\xb7 %s (%zu of %zu)", spd, left, name,
				 q->done + q->failed + 1, all);
	} else {
		snprintf(text, sizeof text, "Uploading %s (%zu of %zu)\xe2\x80\xa6", name,
				 q->done + q->failed + 1, all);
	}
	grid_centered(cr, in.x, in.y + in.h / 2, in.w, text, 12.5, 0.8);
}

static void icon_button(cairo_t *cr, struct set_rect b, bool hot, enum glyph g) {
	if (hot) {
		cairo_set_source_rgba(cr, GUI_C_INK, 0.1);
		grabit_cairo_rect_r(cr, b.x, b.y, b.w, b.h, 4);
		cairo_fill(cr);
	}
	cairo_set_source_rgba(cr, GUI_C_TEXT, hot ? 0.95 : 0.7);
	glyph(cr, g, b.x + b.w / 2, b.y + b.h / 2);
}

static void draw_results(struct settings_state *st, cairo_t *cr, const struct upq_layout *l) {
	struct set_upq *q = st->upq;
	struct set_rect p = l->res;
	double rad = grabit_ui_radius(GUI_R_PANEL);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.04);
	grabit_cairo_rect_r(cr, p.x, p.y, p.w, p.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.14);
	cairo_set_line_width(cr, 1);
	grabit_cairo_rect_r_inset(cr, p.x, p.y, p.w, p.h, rad, 1);
	cairo_stroke(cr);
	size_t ok = ok_results(q);
	char title[64];
	snprintf(title, sizeof title, "Uploaded %zu file%s", ok, ok == 1 ? "" : "s");
	ui_draw_text_fit(cr, p.x + 16, p.y + 14, p.w - 60, title, 15, CAIRO_FONT_WEIGHT_BOLD, 0.95);
	icon_button(cr, l->res_close, q->hover == UH_RES_CLOSE, G_CLOSE);
	for (size_t i = 0; i < q->n_res; i++) {
		const struct upq_result *res = &q->res[i];
		double y = l->res_rows + (double)i * UPQ_RES_ROW;
		struct set_rect open, copy;
		upq_res_icons(l, i, &open, &copy);
		double tw = open.x - p.x - 28;
		if (res->failed) {
			char line[300];
			snprintf(line, sizeof line, "%s: upload failed; details in grabit.log", res->name);
			ui_draw_text_fit_rgba(cr, p.x + 16, y + 6, p.w - 32, line, 13, CAIRO_FONT_WEIGHT_NORMAL,
								  GUI_C_ERROR, 1);
			continue;
		}
		const char *shown = res->url[0] ? res->url : res->name;
		ui_draw_text_fit_rgba(cr, p.x + 16, y + 6, tw, shown, 13, CAIRO_FONT_WEIGHT_NORMAL,
							  GUI_C_ACCENT, 1);
		if (!res->url[0]) continue;
		icon_button(cr, open, q->hover == UH_RES_OPEN + i, G_OPEN);
		icon_button(cr, copy, q->hover == UH_RES_COPY + i, G_COPY);
	}
	char all[48];
	copy_all_label(q, all, sizeof all);
	if (l->copy_all.w > 0) button(cr, l->copy_all, all, G_NONE, q->hover == UH_COPY_ALL, true, 0);
}

static void draw_card(struct settings_state *st, cairo_t *cr, const struct upq_layout *l,
					  size_t i) {
	struct set_upq *q = st->upq;
	const struct upq_file *f = &q->files[i];
	struct set_rect c = upq_card(l, i);
	double rad = grabit_ui_radius(GUI_R_PANEL);
	bool hot = q->hover == UH_CARD + i || q->pop == i;
	bool busy = upq_busy(q), cur = f->state == UQ_UPLOADING;
	cairo_set_source_rgba(cr, GUI_C_INK, hot ? 0.07 : 0.035);
	grabit_cairo_rect_r(cr, c.x, c.y, c.w, c.h, rad);
	cairo_fill(cr);
	cairo_set_line_width(cr, 1);
	if (cur)
		grabit_ui_accent(cr, 0.9);
	else if (f->state == UQ_FAILED)
		cairo_set_source_rgba(cr, GUI_C_ERROR, 0.8);
	else
		cairo_set_source_rgba(cr, GUI_C_INK, hot ? 0.24 : 0.14);
	grabit_cairo_rect_r_inset(cr, c.x, c.y, c.w, c.h, rad, 1);
	cairo_stroke(cr);
	double icon = 40, gap = 18, maxw = c.w - 32 - icon - gap;
	char shown[256];
	snprintf(shown, sizeof shown, "%s", grabit_basename(f->path));
	shown[grabit_utf8_valid_prefix(shown, strlen(shown))] = '\0';
	ui_text_ellipsize(cr, shown, 13.5, CAIRO_FONT_WEIGHT_NORMAL, maxw);
	cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.5);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, shown, &ext);
	double nw = ext.x_advance < maxw ? ext.x_advance : maxw;
	double gx = c.x + (c.w - (icon + gap + nw)) / 2, cy = c.y + c.h / 2;
	cairo_set_source_rgba(cr, GUI_C_TEXT, 0.85);
	file_upload_icon(cr, gx + icon / 2, cy, icon);
	cairo_set_source_rgba(cr, GUI_C_TEXT, 0.95);
	cairo_move_to(cr, gx + icon + gap, cy + 5);
	cairo_show_text(cr, shown);
	if (busy && !cur) {
		cairo_set_source_rgba(cr, GUI_C_SHADE, 0.2);
		grabit_cairo_rect_r(cr, c.x, c.y, c.w, c.h, rad);
		cairo_fill(cr);
	}
	if (f->state == UQ_FAILED || f->state == UQ_DONE) {
		const char *tag = f->state == UQ_DONE ? "uploaded" : "failed";
		static const double DONE[] = {GUI_C_ACCENT}, FAILED[] = {GUI_C_ERROR};
		const double *rgb = f->state == UQ_DONE ? DONE : FAILED;
		double tw = ui_text_width(cr, tag, 10.5, CAIRO_FONT_WEIGHT_BOLD);
		ui_draw_text_fit_rgba(cr, c.x + c.w - tw - 10, c.y + 6, tw + 2, tag, 10.5,
							  CAIRO_FONT_WEIGHT_BOLD, rgb[0], rgb[1], rgb[2], 1);
	}
}

void upq_draw(struct settings_state *st, cairo_t *cr, struct set_row *r, bool focused) {
	struct set_upq *q = st->upq;
	struct upq_layout l;
	upq_layout(cr, st, r, &l);
	draw_zone(st, cr, &l, focused);
	if (l.bar.w > 0) draw_progress(st, cr, &l);
	if (l.res.w > 0) draw_results(st, cr, &l);
	for (size_t i = 0; i < l.shown; i++)
		draw_card(st, cr, &l, i);
	if (l.more.w > 0) {
		char hid[64];
		hidden_label(q, l.shown, hid, sizeof hid);
		ui_draw_text_fit(cr, l.hidden_x, l.more.y + 5, l.more.x - l.hidden_x, hid, 12.5,
						 CAIRO_FONT_WEIGHT_NORMAL, 0.55);
		button(cr, l.more, "Show more", G_NONE, q->hover == UH_MORE, !upq_busy(q), 0);
		button(cr, l.all, "Show all", G_NONE, q->hover == UH_ALL, !upq_busy(q), 0);
	}
	bool idle = !upq_busy(q), any = q->n > 0;
	char label[96];
	upq_go_label(q, label, sizeof label);
	button(cr, l.clear, "Clear all", G_TRASH, q->hover == UH_CLEAR, any && idle, 0);
	button(cr, l.options, "Options", G_GEAR, q->hover == UH_OPTIONS || q->options_open, true,
		   upq_changes(q));
	button(cr, l.go, label, G_UPLOAD, q->hover == UH_GO, any && idle, 0);
}

void upq_draw_overlay(struct settings_state *st, cairo_t *cr, struct set_row *r) {
	struct set_upq *q = st->upq;
	struct upq_layout l;
	upq_layout(cr, st, r, &l);
	struct set_rect b, rm;
	if (!upq_popover(st, &l, &b, &rm)) return;
	struct upq_file *f = &q->files[q->pop];
	upq_load_preview(f);
	double rad = grabit_ui_radius(GUI_R_PANEL);
	cairo_set_source_rgba(cr, GUI_C_SHADE, 0.35);
	grabit_cairo_rect_r(cr, b.x + 1, b.y + 4, b.w, b.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_SURFACE, 1.0);
	grabit_cairo_rect_r(cr, b.x, b.y, b.w, b.h, rad);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, GUI_C_INK, 0.16);
	cairo_set_line_width(cr, 1);
	grabit_cairo_rect_r_inset(cr, b.x, b.y, b.w, b.h, rad, 1);
	cairo_stroke(cr);
	struct set_rect pv = {b.x + 14, b.y + 14, b.w - 28, UPQ_POP_PREVIEW};
	cairo_set_source_rgba(cr, GUI_C_INK, 0.05);
	grabit_cairo_rect_r(cr, pv.x, pv.y, pv.w, pv.h, 6);
	cairo_fill(cr);
	if (f->thumb) {
		grid_draw_image(cr, f->thumb, pv.x, pv.y, pv.w, pv.h, 6, false);
	} else {
		cairo_set_source_rgba(cr, GUI_C_TEXT, 0.6);
		file_upload_icon(cr, pv.x + pv.w / 2, pv.y + pv.h / 2 - 10, 64);
		grid_centered(cr, pv.x, pv.y + pv.h / 2 + 44, pv.w, "no preview for this file type", 12,
					  0.5);
	}
	double y = pv.y + pv.h + 12;
	char line[512];
	snprintf(line, sizeof line, "%s%s%s%s", grabit_basename(f->path), f->mime ? " (" : "",
			 f->mime ? f->mime : "", f->mime ? ")" : "");
	ui_draw_text_fit(cr, b.x + 14, y, b.w - 28, line, 12.5, CAIRO_FONT_WEIGHT_BOLD, 0.7);
	char size[32];
	grid_size_text(f->size, size, sizeof size);
	ui_draw_text_fit(cr, b.x + 14, y + 20, b.w - 28, size, 12.5, CAIRO_FONT_WEIGHT_NORMAL, 0.55);
	button(cr, rm, "Remove", G_TRASH, q->hover == UH_POP_REMOVE, true, 0);
}
