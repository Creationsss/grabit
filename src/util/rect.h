// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UTIL_RECT_H
#define GRABIT_UTIL_RECT_H

#include <stdbool.h>
#include <stdint.h>

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

static inline bool rect_equal(struct rect a, struct rect b) {
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static inline struct rect rect_clamp_into(struct rect r, struct rect b) {
	r.x = i32max(b.x, i32min(r.x, b.x + b.w - r.w));
	r.y = i32max(b.y, i32min(r.y, b.y + b.h - r.h));
	return r;
}

static inline struct rect rect_intersect(struct rect a, struct rect b) {
	int32_t x = i32max(a.x, b.x);
	int32_t y = i32max(a.y, b.y);
	int32_t r = i32min(a.x + a.w, b.x + b.w);
	int32_t bot = i32min(a.y + a.h, b.y + b.h);
	if (r <= x || bot <= y) return (struct rect){0, 0, 0, 0};
	return (struct rect){x, y, r - x, bot - y};
}

static inline struct rect rect_inflate(struct rect r, int32_t d) {
	r.x -= d;
	r.y -= d;
	r.w += 2 * d;
	r.h += 2 * d;
	return r;
}

#endif
