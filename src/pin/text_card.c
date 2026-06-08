// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/text_card.h"

#include "log.h"

#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

#define FONT_FACE "sans-serif"
#define FONT_SIZE 18.0
#define LINE_HEIGHT (FONT_SIZE * 1.4)
#define PAD 20.0
#define MAX_LINE_W 600.0
#define MIN_CARD_W 200.0

struct line {
	char *text;
	double width;
};

struct line_buf {
	struct line *items;
	size_t n;
	size_t cap;
};

static int lb_push(struct line_buf *b, const char *s, size_t n, double w) {
	if (b->n == b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 16;
		struct line *p = realloc(b->items, cap * sizeof *p);
		if (!p) return -1;
		b->items = p;
		b->cap = cap;
	}
	char *copy = malloc(n + 1);
	if (!copy) return -1;
	if (n) memcpy(copy, s, n);
	copy[n] = '\0';
	b->items[b->n++] = (struct line){.text = copy, .width = w};
	return 0;
}

static void lb_free(struct line_buf *b) {
	for (size_t i = 0; i < b->n; i++)
		free(b->items[i].text);
	free(b->items);
}

static double measure(cairo_t *cr, const char *s, size_t n) {
	if (!n) return 0.0;
	char *tmp = malloc(n + 1);
	if (!tmp) return 0.0;
	memcpy(tmp, s, n);
	tmp[n] = '\0';
	cairo_text_extents_t ext;
	cairo_text_extents(cr, tmp, &ext);
	free(tmp);
	return ext.x_advance;
}

static int wrap_hard_line(cairo_t *cr, const char *src, size_t srclen,
						  struct line_buf *out, double max_w) {
	if (srclen == 0) {
		return lb_push(out, "", 0, 0.0);
	}
	if (measure(cr, src, srclen) <= max_w) {
		return lb_push(out, src, srclen, measure(cr, src, srclen));
	}

	size_t cur_start = 0;
	size_t cur_end = 0;
	size_t i = 0;
	while (i < srclen) {
		while (i < srclen && src[i] == ' ')
			i++;
		size_t word_start = i;
		while (i < srclen && src[i] != ' ')
			i++;
		size_t word_end = i;
		if (word_start == word_end) continue;

		size_t candidate_end = word_end;
		double cand_w = measure(cr, src + cur_start, candidate_end - cur_start);
		if (cand_w <= max_w || cur_end == cur_start) {
			cur_end = candidate_end;
			continue;
		}

		if (lb_push(out, src + cur_start, cur_end - cur_start,
					measure(cr, src + cur_start, cur_end - cur_start)) != 0)
			return -1;
		cur_start = word_start;
		cur_end = word_end;
	}

	if (cur_end > cur_start) {
		double w = measure(cr, src + cur_start, cur_end - cur_start);
		if (w > max_w) {
			size_t s = cur_start;
			while (s < cur_end) {
				size_t lo = s + 1, hi = cur_end, best = s + 1;
				while (lo <= hi) {
					size_t mid = (lo + hi) / 2;
					if (measure(cr, src + s, mid - s) <= max_w) {
						best = mid;
						lo = mid + 1;
					} else {
						if (mid == 0) break;
						hi = mid - 1;
					}
				}
				while (best > s + 1 && ((unsigned char)src[best] & 0xC0) == 0x80)
					best--;
				if (best <= s) {
					best = s + 1;
					while (best < cur_end && ((unsigned char)src[best] & 0xC0) == 0x80)
						best++;
				}
				if (lb_push(out, src + s, best - s,
							measure(cr, src + s, best - s)) != 0)
					return -1;
				s = best;
			}
		} else {
			if (lb_push(out, src + cur_start, cur_end - cur_start, w) != 0)
				return -1;
		}
	}
	return 0;
}

static int wrap_text(cairo_t *cr, const char *text, struct line_buf *out) {
	const char *p = text;
	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		if (wrap_hard_line(cr, p, len, out, MAX_LINE_W) != 0) return -1;
		if (!nl) break;
		p = nl + 1;
	}
	if (out->n == 0) return lb_push(out, "", 0, 0.0);
	return 0;
}

int pin_text_card_render_png(const char *text, const char *out_path) {
	if (!text || !out_path) return -1;

	cairo_surface_t *measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *mcr = cairo_create(measure_surf);
	cairo_select_font_face(mcr, FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(mcr, FONT_SIZE);

	struct line_buf lb = {0};
	if (wrap_text(mcr, text, &lb) != 0) {
		cairo_destroy(mcr);
		cairo_surface_destroy(measure_surf);
		lb_free(&lb);
		return -1;
	}

	double max_w = MIN_CARD_W;
	for (size_t i = 0; i < lb.n; i++) {
		if (lb.items[i].width > max_w) max_w = lb.items[i].width;
	}
	cairo_destroy(mcr);
	cairo_surface_destroy(measure_surf);

	int w = (int)(max_w + 2 * PAD + 0.5);
	int h = (int)(lb.n * LINE_HEIGHT + 2 * PAD + 0.5);
	if (w < (int)(MIN_CARD_W + 2 * PAD)) w = (int)(MIN_CARD_W + 2 * PAD);

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		log_error("text_card: surface create failed");
		cairo_surface_destroy(surf);
		lb_free(&lb);
		return -1;
	}
	cairo_t *cr = cairo_create(surf);

	cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.95);
	cairo_paint(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
	cairo_select_font_face(cr, FONT_FACE, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, FONT_SIZE);
	cairo_font_extents_t fe;
	cairo_font_extents(cr, &fe);
	double baseline = PAD + fe.ascent;
	for (size_t i = 0; i < lb.n; i++) {
		cairo_move_to(cr, PAD, baseline + (double)i * LINE_HEIGHT);
		cairo_show_text(cr, lb.items[i].text);
	}

	cairo_destroy(cr);
	cairo_status_t st = cairo_surface_write_to_png(surf, out_path);
	cairo_surface_destroy(surf);
	lb_free(&lb);
	if (st != CAIRO_STATUS_SUCCESS) {
		log_error("text_card: write_to_png %s: %s", out_path, cairo_status_to_string(st));
		return -1;
	}
	return 0;
}
