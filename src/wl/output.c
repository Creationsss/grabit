// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "wl/wl.h"

#include "log.h"
#include "region/region.h"
#include "wl/internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "xdg-output-unstable-v1-client-protocol.h"

static void output_geometry(void *data, struct wl_output *wo, int32_t x, int32_t y,
							int32_t pw, int32_t ph, int32_t subpixel,
							const char *make, const char *model, int32_t transform) {
	(void)wo;
	(void)pw;
	(void)ph;
	(void)subpixel;
	(void)make;
	(void)model;
	struct grabit_output *o = data;
	o->x = x;
	o->y = y;
	o->transform = transform;
}

static void output_mode(void *data, struct wl_output *wo, uint32_t flags,
						int32_t w, int32_t h, int32_t refresh) {
	(void)wo;
	(void)refresh;
	struct grabit_output *o = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		o->width = w;
		o->height = h;
	}
}

static void output_done(void *data, struct wl_output *wo) {
	(void)data;
	(void)wo;
}

static void output_scale(void *data, struct wl_output *wo, int32_t factor) {
	(void)wo;
	struct grabit_output *o = data;
	o->scale = factor;
}

static void output_name(void *data, struct wl_output *wo, const char *name) {
	(void)wo;
	struct grabit_output *o = data;
	free(o->name);
	o->name = name ? strdup(name) : NULL;
}

static void output_description(void *data, struct wl_output *wo, const char *desc) {
	(void)data;
	(void)wo;
	(void)desc;
}

const struct wl_output_listener grabit_wl_output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *xo,
										int32_t x, int32_t y) {
	(void)xo;
	struct grabit_output *o = data;
	o->x = x;
	o->y = y;
}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *xo,
									int32_t w, int32_t h) {
	(void)xo;
	struct grabit_output *o = data;
	o->logical_width = w;
	o->logical_height = h;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *xo) {
	(void)data;
	(void)xo;
}

static void xdg_output_xname(void *data, struct zxdg_output_v1 *xo, const char *name) {
	(void)xo;
	struct grabit_output *o = data;
	free(o->name);
	o->name = name ? strdup(name) : NULL;
}

static void xdg_output_xdescription(void *data, struct zxdg_output_v1 *xo,
									const char *desc) {
	(void)data;
	(void)xo;
	(void)desc;
}

const struct zxdg_output_v1_listener grabit_xdg_output_listener = {
	.logical_position = xdg_output_logical_position,
	.logical_size = xdg_output_logical_size,
	.done = xdg_output_done,
	.name = xdg_output_xname,
	.description = xdg_output_xdescription,
};

int gwl_outputs_push(struct grabit_wl_state *s, struct grabit_output *o) {
	if (s->n_outputs == s->cap_outputs) {
		size_t cap = s->cap_outputs ? s->cap_outputs * 2 : 4;
		struct grabit_output **p = realloc(s->outputs, cap * sizeof *p);
		if (!p) return -1;
		s->outputs = p;
		s->cap_outputs = cap;
	}
	s->outputs[s->n_outputs++] = o;
	return 0;
}

struct grabit_output *grabit_wl_primary_output(struct grabit_wl_state *s) {
	return s->n_outputs > 0 ? s->outputs[0] : NULL;
}

struct grabit_output *grabit_wl_output_by_name(struct grabit_wl_state *s, const char *name) {
	if (!name) return NULL;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->name && strcmp(s->outputs[i]->name, name) == 0)
			return s->outputs[i];
	}
	return NULL;
}

void grabit_output_rect(const struct grabit_output *o, struct rect *r) {
	r->x = o->x;
	r->y = o->y;
	r->w = o->logical_width;
	r->h = o->logical_height;
}

static void grabit_wl_log_monitors(const struct grabit_wl_state *s) {
	for (size_t i = 0; i < s->n_outputs; i++) {
		const struct grabit_output *o = s->outputs[i];
		log_info("  %zu: %s (%dx%d)", i + 1,
				 o->name ? o->name : "?", o->logical_width, o->logical_height);
	}
}

void grabit_wl_monitor_rects(struct grabit_wl_state *s, struct rect **out, size_t *n_out) {
	*out = NULL;
	*n_out = 0;
	if (s->n_outputs == 0) return;
	struct rect *r = malloc(s->n_outputs * sizeof *r);
	if (!r) return;
	for (size_t i = 0; i < s->n_outputs; i++)
		grabit_output_rect(s->outputs[i], &r[i]);
	*out = r;
	*n_out = s->n_outputs;
}

void grabit_wl_outputs_bbox(struct grabit_wl_state *s, struct rect *out) {
	memset(out, 0, sizeof *out);
	if (s->n_outputs == 0) return;
	grabit_output_rect(s->outputs[0], out);
	int32_t max_x = out->x + out->w, max_y = out->y + out->h;
	for (size_t i = 1; i < s->n_outputs; i++) {
		struct rect r;
		grabit_output_rect(s->outputs[i], &r);
		if (r.x < out->x) out->x = r.x;
		if (r.y < out->y) out->y = r.y;
		if (r.x + r.w > max_x) max_x = r.x + r.w;
		if (r.y + r.h > max_y) max_y = r.y + r.h;
	}
	out->w = max_x - out->x;
	out->h = max_y - out->y;
}

int grabit_wl_fullscreen_plan(struct grabit_wl_state *s, const char *spec, struct rect *out) {
	if (s->n_outputs == 0) {
		log_error("fullscreen: no outputs");
		return -1;
	}
	if (spec && strcmp(spec, "all") == 0) {
		grabit_wl_outputs_bbox(s, out);
		return 0;
	}
	if (spec && spec[0]) {
		struct grabit_output *target = NULL;
		char *end = NULL;
		long n = strtol(spec, &end, 10);
		if (end && *end == '\0' && n >= 1 && (size_t)n <= s->n_outputs)
			target = s->outputs[n - 1];
		else
			target = grabit_wl_output_by_name(s, spec);
		if (!target) {
			log_error("fullscreen: no monitor matches `%s`; available:", spec);
			grabit_wl_log_monitors(s);
			return -1;
		}
		grabit_output_rect(target, out);
		return 0;
	}
	if (s->n_outputs == 1) {
		grabit_output_rect(grabit_wl_primary_output(s), out);
		return 0;
	}
	return 1;
}

struct grabit_output *grabit_wl_output_at(struct grabit_wl_state *s, int32_t x, int32_t y) {
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (x >= o->x && y >= o->y &&
			x < o->x + o->logical_width &&
			y < o->y + o->logical_height)
			return o;
	}
	return NULL;
}

bool grabit_output_rect_intersect(const struct grabit_output *o, const struct rect *r,
								  int32_t *out_x, int32_t *out_y,
								  int32_t *out_w, int32_t *out_h) {
	int32_t lx = r->x > o->x ? r->x : o->x;
	int32_t ly = r->y > o->y ? r->y : o->y;
	int32_t rx = (r->x + r->w) < (o->x + o->logical_width)
					 ? (r->x + r->w)
					 : (o->x + o->logical_width);
	int32_t ry = (r->y + r->h) < (o->y + o->logical_height)
					 ? (r->y + r->h)
					 : (o->y + o->logical_height);
	int32_t iw = rx - lx;
	int32_t ih = ry - ly;
	if (iw <= 0 || ih <= 0) return false;
	if (out_x) *out_x = lx;
	if (out_y) *out_y = ly;
	if (out_w) *out_w = iw;
	if (out_h) *out_h = ih;
	return true;
}

bool grabit_output_overlaps(const struct grabit_output *o, struct rect r) {
	return grabit_output_rect_intersect(o, &r, NULL, NULL, NULL, NULL);
}
