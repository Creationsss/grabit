// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_REGION_H
#define GRABIT_REGION_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct config;
struct grabit_wl_state;
struct image;

struct rect {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
};

static inline bool rect_contains(struct rect r, int32_t x, int32_t y) {
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static inline bool rects_overlap(struct rect a, struct rect b) {
	return a.x < b.x + b.w && b.x < a.x + a.w &&
		   a.y < b.y + b.h && b.y < a.y + a.h;
}

static inline int32_t i32min(int32_t a, int32_t b) {
	return a < b ? a : b;
}

static inline int32_t i32max(int32_t a, int32_t b) {
	return a > b ? a : b;
}

static inline struct rect rect_clamp_into(struct rect r, struct rect b) {
	r.x = i32max(b.x, i32min(r.x, b.x + b.w - r.w));
	r.y = i32max(b.y, i32min(r.y, b.y + b.h - r.h));
	return r;
}

#define ANNO_DEFAULT_FONT 18
#define ANNO_DEFAULT_WIDTH 3

enum tool_kind {
	TOOL_PEN = 0,
	TOOL_MARKER,
	TOOL_LINE,
	TOOL_RECT,
	TOOL_ELLIPSE,
	TOOL_ARROW,
	TOOL_BLUR,
	TOOL_PIXELATE,
	TOOL_TEXT,
	TOOL_COUNTER,
	TOOL_ERASER,
	TOOL_COUNT,
};

static inline bool tool_uses_points(enum tool_kind t) {
	return t == TOOL_PEN || t == TOOL_MARKER || t == TOOL_ERASER;
}

static inline bool tool_is_rect_region(enum tool_kind t) {
	return t == TOOL_RECT || t == TOOL_ELLIPSE || t == TOOL_BLUR ||
		   t == TOOL_PIXELATE;
}

static inline bool tool_samples_backdrop(enum tool_kind t) {
	return t == TOOL_BLUR || t == TOOL_PIXELATE;
}

extern const char *const grabit_tool_names[];

struct annotation {
	enum tool_kind tool;
	int32_t x0, y0, x1, y1;
	int32_t *points;
	size_t n_points;
	char *text;
	uint32_t color;
	int32_t width;
	int32_t font_size;
	struct rect bbox;
};

static inline int32_t annotation_corner_x(const struct annotation *a, int c) {
	return (c & 1) ? a->x1 : a->x0;
}

static inline int32_t annotation_corner_y(const struct annotation *a, int c) {
	return (c & 2) ? a->y1 : a->y0;
}

struct annotation_list {
	struct annotation *items;
	size_t n;
	size_t cap;
	size_t gen;
};

void annotation_list_free(struct annotation_list *list);

int region_select(struct grabit_wl_state *s, struct config *cfg,
				  const struct image *frozen_per_output,
				  bool annotate_mode, struct rect *out,
				  struct annotation_list *out_annos,
				  uint32_t *inout_color, int32_t *inout_width,
				  int32_t *inout_tool,
				  bool *out_choices_dirty, const struct rect *preset,
				  const struct rect *snap_rects, size_t n_snap_rects);

#endif
