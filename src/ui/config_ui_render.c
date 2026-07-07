// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ui/config_ui_internal.h"

#include <string.h>

#include <cairo/cairo.h>

static const char *const TAB_NAMES[NTAB] = {
	"General",
	"Recording",
	"Editor",
	"Image",
	"OCR",
	"Notify",
	"Services",
};

static void set_accent(cairo_t *cr, double a) {
	cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, a);
}

static void text(cairo_t *cr, double x, double y, const char *s, double size,
				 cairo_font_weight_t weight, double r, double g, double b, double a) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, weight);
	cairo_set_font_size(cr, size);
	cairo_set_source_rgba(cr, r, g, b, a);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, s);
}

static void text_centered(cairo_t *cr, double cx, double y, const char *s, double size,
						  cairo_font_weight_t weight, double r, double g, double b, double a) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, weight);
	cairo_set_font_size(cr, size);
	cairo_text_extents_t e;
	cairo_text_extents(cr, s, &e);
	text(cr, cx - (e.width / 2.0 + e.x_bearing), y, s, size, weight, r, g, b, a);
}

static void chevron(cairo_t *cr, const struct rect *rc, int dir) {
	double cx = rc->x + rc->w / 2.0, cy = rc->y + rc->h / 2.0, tw = 4.5, th = 6;
	cairo_set_line_width(cr, 2.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_source_rgba(cr, 0.92, 0.92, 0.94, 1);
	cairo_move_to(cr, cx - dir * tw, cy - th);
	cairo_line_to(cr, cx + dir * tw, cy);
	cairo_line_to(cr, cx - dir * tw, cy + th);
	cairo_stroke(cr);
}

static void ellipsis_dots(cairo_t *cr, const struct rect *rc) {
	double cy = rc->y + rc->h / 2.0, cx = rc->x + rc->w / 2.0;
	cairo_set_source_rgba(cr, 0.9, 0.9, 0.92, 1);
	for (int k = -1; k <= 1; k++) {
		cairo_arc(cr, cx + k * 6, cy, 1.6, 0, 6.2832);
		cairo_fill(cr);
	}
}

static double draw_hint(cairo_t *cr, double x, double y, const char *key, const char *label,
						bool measure) {
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 11);
	cairo_text_extents_t ke;
	cairo_text_extents(cr, key, &ke);
	double cw = ke.width + 11;
	if (!measure) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
		cairo_rectangle(cr, x, y - 12, cw, 16);
		cairo_fill(cr);
		text(cr, x + 5.5, y, key, 11, CAIRO_FONT_WEIGHT_BOLD, 0.9, 0.9, 0.93, 1);
	}
	x += cw + 6;
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 11.5);
	cairo_text_extents_t le;
	cairo_text_extents(cr, label, &le);
	if (!measure)
		text(cr, x, y, label, 11.5, CAIRO_FONT_WEIGHT_NORMAL, 0.56, 0.56, 0.6, 1);
	return x + le.width + 15;
}

static void draw_masked(cairo_t *cr, double x, double y, const char *s) {
	char dots[3 * 64 + 1];
	size_t b = 0;
	int n = 0;
	for (const char *p = s; *p && n < 64; p++) {
		if (((unsigned char)*p & 0xC0) == 0x80) continue;
		dots[b++] = (char)0xE2;
		dots[b++] = (char)0x80;
		dots[b++] = (char)0xA2;
		n++;
	}
	dots[b] = '\0';
	text(cr, x, y, dots, 13, CAIRO_FONT_WEIGHT_NORMAL, 0.88, 0.88, 0.9, 1);
}

static bool row_revealed(struct cfg_ui *u, int i) {
	if (u->editing == i) return true;
	if (u->sel >= 0 && u->sel < u->tab_n[u->tab])
		return u->tab_keys[u->tab][u->sel] == i;
	return false;
}

static void draw_field(cairo_t *cr, struct cfg_ui *u, int i, double row_y) {
	bool editing = u->editing == i;
	struct rect fr;
	field_rect(&fr, u->keys[i].is_path, row_y);

	cairo_set_source_rgba(cr, 1, 1, 1, editing ? 0.10 : 0.05);
	cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
	cairo_fill(cr);
	if (editing) {
		set_accent(cr, 0.9);
		cairo_set_line_width(cr, 1.5);
		cairo_rectangle(cr, fr.x + 0.75, fr.y + 0.75, fr.w - 1.5, fr.h - 1.5);
		cairo_stroke(cr);
	}

	const char *shown = editing ? u->edit_buf : u->val[i];
	double tx = fr.x + 7;
	double ty = fr.y + fr.h / 2.0 + 5;
	cairo_save(cr);
	cairo_rectangle(cr, fr.x + 4, fr.y, fr.w - 8, fr.h);
	cairo_clip(cr);
	cairo_text_extents_t e = {0};
	bool masked = u->keys[i].is_secret && !editing && !row_revealed(u, i);
	if (shown[0]) {
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 13);
		cairo_text_extents(cr, shown, &e);
		double over = (tx + e.width) - (fr.x + fr.w - 7);
		if (editing && over > 0) tx -= over;
		if (masked)
			draw_masked(cr, tx, ty, shown);
		else
			text(cr, tx, ty, shown, 13, CAIRO_FONT_WEIGHT_NORMAL, 0.88, 0.88, 0.9, 1);
	}
	if (editing) {
		double caret = tx + e.width + 1;
		set_accent(cr, 1);
		cairo_set_line_width(cr, 1.5);
		cairo_move_to(cr, caret, fr.y + 5);
		cairo_line_to(cr, caret, fr.y + fr.h - 5);
		cairo_stroke(cr);
	}
	cairo_restore(cr);

	if (u->keys[i].is_path) {
		struct rect br;
		right_btn_rect(&br, row_y);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
		cairo_rectangle(cr, br.x, br.y, br.w, br.h);
		cairo_fill(cr);
		ellipsis_dots(cr, &br);
	}
}

static void tri_down(cairo_t *cr, double cx, double cy) {
	cairo_set_source_rgba(cr, 0.85, 0.85, 0.88, 1);
	cairo_move_to(cr, cx - 4, cy - 2);
	cairo_line_to(cr, cx + 4, cy - 2);
	cairo_line_to(cr, cx, cy + 3);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void draw_combo(cairo_t *cr, struct cfg_ui *u, int i, double row_y, const char *ph) {
	struct rect fr;
	field_rect(&fr, false, row_y);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
	cairo_fill(cr);
	const char *v = u->val[i];
	cairo_save(cr);
	cairo_rectangle(cr, fr.x, fr.y, fr.w - 16, fr.h);
	cairo_clip(cr);
	text(cr, fr.x + 8, fr.y + fr.h / 2.0 + 5, v[0] ? v : ph, 13,
		 CAIRO_FONT_WEIGHT_NORMAL, 0.88, 0.88, 0.9, 1);
	cairo_restore(cr);
	tri_down(cr, fr.x + fr.w - 12, fr.y + fr.h / 2.0);
}

static void draw_service(cairo_t *cr, struct cfg_ui *u, int i, double row_y) {
	struct rect fr;
	field_rect(&fr, true, row_y);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_rectangle(cr, fr.x, fr.y, fr.w, fr.h);
	cairo_fill(cr);
	const char *v = u->val[i];
	cairo_save(cr);
	cairo_rectangle(cr, fr.x, fr.y, fr.w - 16, fr.h);
	cairo_clip(cr);
	text(cr, fr.x + 8, fr.y + fr.h / 2.0 + 5, v[0] ? v : "(none)", 13,
		 CAIRO_FONT_WEIGHT_NORMAL, 0.88, 0.88, 0.9, 1);
	cairo_restore(cr);
	tri_down(cr, fr.x + fr.w - 12, fr.y + fr.h / 2.0);

	struct rect br;
	right_btn_rect(&br, row_y);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_rectangle(cr, br.x, br.y, br.w, br.h);
	cairo_fill(cr);
	double cx = br.x + br.w / 2.0, cy = br.y + br.h / 2.0;
	cairo_set_source_rgba(cr, 0.9, 0.9, 0.92, 1);
	cairo_set_line_width(cr, 2.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, cx - 5, cy);
	cairo_line_to(cr, cx + 5, cy);
	cairo_move_to(cr, cx, cy - 5);
	cairo_line_to(cr, cx, cy + 5);
	cairo_stroke(cr);
}

static void draw_widget(cairo_t *cr, struct cfg_ui *u, int i, double row_y) {
	const struct cfg_key_desc *d = &u->keys[i];
	const char *v = u->val[i];

	if (d->kind == CFG_BOOL) {
		bool on = bool_on(v);
		struct rect t;
		toggle_rect(&t, row_y);
		if (on)
			cairo_set_source_rgba(cr, 0.20, 0.58, 0.32, 1);
		else
			cairo_set_source_rgba(cr, 1, 1, 1, 0.13);
		cairo_rectangle(cr, t.x, t.y, t.w, t.h);
		cairo_fill(cr);
		double kr = t.h - 6, kx = on ? t.x + t.w - kr - 3 : t.x + 3;
		cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
		cairo_rectangle(cr, kx, t.y + 3, kr, kr);
		cairo_fill(cr);
		return;
	}

	if (d->kind == CFG_STRING) {
		if (d->is_monitor)
			draw_combo(cr, u, i, row_y, "(auto)");
		else if (d->is_service)
			draw_service(cr, u, i, row_y);
		else
			draw_field(cr, u, i, row_y);
		return;
	}

	if (d->kind == CFG_ENUM) {
		draw_combo(cr, u, i, row_y, "(none)");
		return;
	}

	struct rect dec, inc;
	dec_inc_rects(&dec, &inc, row_y);
	for (int b = 0; b < 2; b++) {
		const struct rect *rc = b ? &inc : &dec;
		cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
		cairo_rectangle(cr, rc->x, rc->y, rc->w, rc->h);
		cairo_fill(cr);
		chevron(cr, rc, b ? 1 : -1);
	}
	const char *show = v[0] ? v : "(none)";
	text_centered(cr, (dec.x + dec.w + inc.x) / 2.0, row_y + ROW_H / 2.0 + 5, show, 14,
				  CAIRO_FONT_WEIGHT_BOLD, 0.96, 0.96, 0.96, 1);
}

void cfg_ui_draw(cairo_t *cr, int32_t w, int32_t h, void *user) {
	struct cfg_ui *u = user;

	cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 1);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);

	for (int t = 0; t < NTAB; t++) {
		struct rect tr;
		tab_rect(t, &tr);
		bool active = t == u->tab;
		if (active) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
			cairo_rectangle(cr, tr.x, tr.y, tr.w, tr.h);
			cairo_fill(cr);
			set_accent(cr, 1);
			cairo_rectangle(cr, tr.x, tr.h - 2, tr.w, 2);
			cairo_fill(cr);
		}
		text_centered(cr, tr.x + tr.w / 2.0, TABBAR_H / 2.0 + 5, TAB_NAMES[t], 13,
					  active ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL,
					  active ? 0.98 : 0.6, active ? 0.98 : 0.6, active ? 0.98 : 0.62, 1);
	}
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_rectangle(cr, 0, TABBAR_H - 1, w, 1);
	cairo_fill(cr);

	for (int row = 0; row < u->n_visible; row++) {
		int pos = u->scroll + row;
		if (pos >= u->tab_n[u->tab]) break;
		int i = u->tab_keys[u->tab][pos];
		double y = TABBAR_H + row * ROW_H;
		if (pos == u->sel) {
			set_accent(cr, 0.15);
			cairo_rectangle(cr, 0, y, w, ROW_H);
			cairo_fill(cr);
			set_accent(cr, 0.9);
			cairo_rectangle(cr, 0, y, 3, ROW_H);
			cairo_fill(cr);
		}
		const char *label = u->keys[i].label ? u->keys[i].label : u->keys[i].key;
		text(cr, PAD, y + ROW_H / 2.0 + 5, label, 14, CAIRO_FONT_WEIGHT_NORMAL,
			 0.87, 0.87, 0.89, 1);
		draw_widget(cr, u, i, y);
	}

	struct rect tb;
	if (test_btn_rect(u, &tb)) {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
		cairo_rectangle(cr, tb.x, tb.y, tb.w, tb.h);
		cairo_fill(cr);
		text_centered(cr, tb.x + tb.w / 2.0, tb.y + tb.h / 2.0 + 5, "Test notifications", 13,
					  CAIRO_FONT_WEIGHT_BOLD, 1.0, 0.55, 0.32, 1);
	}

	if (u->tab_n[u->tab] > u->n_visible) {
		double track = u->n_visible * ROW_H;
		double th = track * u->n_visible / u->tab_n[u->tab];
		double ty = TABBAR_H + track * u->scroll / u->tab_n[u->tab];
		cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
		cairo_rectangle(cr, w - 4, ty, 3, th);
		cairo_fill(cr);
	}

	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_rectangle(cr, 0, h - FOOTER_H, w, 1);
	cairo_fill(cr);
	if (u->status[0]) {
		text_centered(cr, w / 2.0, h - 9, u->status, 12, CAIRO_FONT_WEIGHT_NORMAL,
					  1.0, 0.7, 0.4, 1);
	} else {
		const char *hints[5][2];
		int n_hints = 0;
		if (u->editing >= 0) {
			hints[n_hints][0] = "type", hints[n_hints++][1] = "edit text";
			hints[n_hints][0] = "enter", hints[n_hints++][1] = "accept";
			hints[n_hints][0] = "esc", hints[n_hints++][1] = "cancel";
		} else if (u->dd_open >= 0) {
			hints[n_hints][0] = "arrows", hints[n_hints++][1] = "move";
			hints[n_hints][0] = "enter", hints[n_hints++][1] = "select";
			hints[n_hints][0] = "esc", hints[n_hints++][1] = "cancel";
		} else {
			const struct cfg_key_desc *sd = NULL;
			if (u->sel >= 0 && u->sel < u->tab_n[u->tab])
				sd = &u->keys[u->tab_keys[u->tab][u->sel]];
			const char *verb = "change";
			if (sd && sd->kind == CFG_BOOL)
				verb = "toggle";
			else if (sd && sd->kind == CFG_ENUM)
				verb = "pick";
			else if (sd && sd->kind == CFG_INT)
				verb = "adjust";
			else if (sd && sd->kind == CFG_STRING)
				verb = sd->is_monitor || sd->is_service ? "pick" : "edit";
			hints[n_hints][0] = "arrows", hints[n_hints++][1] = "move / change";
			hints[n_hints][0] = "enter", hints[n_hints++][1] = verb;
			if (sd && sd->is_service)
				hints[n_hints][0] = "+", hints[n_hints++][1] = "import .sxcu";
			else if (sd && sd->kind == CFG_INT)
				hints[n_hints][0] = "shift", hints[n_hints++][1] = "step x10";
			else
				hints[n_hints][0] = "tab", hints[n_hints++][1] = "switch";
			hints[n_hints][0] = "esc", hints[n_hints++][1] = "done";
		}
		double total = 0;
		for (int i = 0; i < n_hints; i++)
			total = draw_hint(cr, total, 0, hints[i][0], hints[i][1], true);
		double hx = (w - (total - 15)) / 2.0, hy = h - 9;
		for (int i = 0; i < n_hints; i++)
			hx = draw_hint(cr, hx, hy, hints[i][0], hints[i][1], false);
		(void)hx;
	}

	if (u->dd_open >= 0) {
		int count = cfg_ui_dd_count(u);
		struct rect first;
		dropdown_item_rect(u, 0, &first);
		double bh = count * DD_ITEM_H;
		cairo_set_source_rgba(cr, 0.17, 0.18, 0.22, 1);
		cairo_rectangle(cr, first.x, first.y, first.w, bh);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
		cairo_set_line_width(cr, 1);
		cairo_rectangle(cr, first.x + 0.5, first.y + 0.5, first.w - 1, bh - 1);
		cairo_stroke(cr);
		const char *cur = u->val[u->dd_open];
		for (int k = 0; k < count; k++) {
			struct rect ir;
			dropdown_item_rect(u, k, &ir);
			bool sel = strcmp(cfg_ui_dd_value(u, k), cur) == 0;
			if (sel) {
				set_accent(cr, 0.18);
				cairo_rectangle(cr, ir.x, ir.y, ir.w, ir.h);
				cairo_fill(cr);
			} else if (k == u->dd_hover) {
				cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
				cairo_rectangle(cr, ir.x, ir.y, ir.w, ir.h);
				cairo_fill(cr);
			}
			text(cr, ir.x + 8, ir.y + ir.h / 2.0 + 5, cfg_ui_dd_label(u, k), 13,
				 sel ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL, 0.9, 0.9, 0.92, 1);
		}
	}
}
