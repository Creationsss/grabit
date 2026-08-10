// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "wl/wl.h"

#include <stdlib.h>

#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void pin_output_destroy(struct pin_output *o) {
	pin_render_output_free(o);
	if (o->fractional) wp_fractional_scale_v1_destroy(o->fractional);
	if (o->viewport) wp_viewport_destroy(o->viewport);
	if (o->layer) zwlr_layer_surface_v1_destroy(o->layer);
	if (o->surface) wl_surface_destroy(o->surface);
	free(o);
}

static int pin_output_create(struct pin_state *st, struct grabit_output *go) {
	struct pin_output **p = realloc(st->outs, (st->n + 1) * sizeof *p);
	if (!p) return -1;
	st->outs = p;
	struct pin_output *o = calloc(1, sizeof *o);
	if (!o) return -1;
	o->st = st;
	o->go = go;
	o->scale = go->scale > 0 ? go->scale : 1;
	if (o->scale > st->cursor_scale) st->cursor_scale = o->scale;
	o->surface = wl_compositor_create_surface(st->wls->compositor);
	if (!o->surface || pin_render_create_layer(o) != 0) {
		if (o->surface) wl_surface_destroy(o->surface);
		free(o);
		return -1;
	}
	pin_render_create_fractional(o);
	grabit_wl_clear_input_region(st->wls->compositor, o->surface);
	wl_surface_commit(o->surface);
	st->outs[st->n++] = o;
	return 0;
}

static bool pin_has_output(const struct pin_state *st,
						   const struct grabit_output *go) {
	for (size_t i = 0; i < st->n; i++)
		if (st->outs[i]->go == go) return true;
	return false;
}

void pin_sync_outputs(struct pin_state *st) {
	struct grabit_wl_state *s = st->wls;
	for (size_t i = 0; i < st->n;) {
		struct pin_output *o = st->outs[i];
		if (!o->go->dead) {
			i++;
			continue;
		}
		if (st->ptr_on == o) st->ptr_on = NULL;
		pin_output_destroy(o);
		st->outs[i] = st->outs[--st->n];
	}
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *go = s->outputs[i];
		if (go->dead || go->logical_width <= 0) continue;
		if (st->transient && go != st->target) continue;
		if (!pin_has_output(st, go)) pin_output_create(st, go);
	}
	if (st->n == 0) {
		st->finished = true;
		return;
	}
	grabit_wl_outputs_bbox(s, &st->bounds);
	if (st->bounds.w > 0 && st->bounds.h > 0) {
		struct rect r = rect_clamp_into(pin_rect(st), st->bounds);
		st->px = r.x;
		st->py = r.y;
	}
	pin_render_redraw_all(st);
}

void pin_outputs_finish(struct pin_state *st) {
	for (size_t i = 0; i < st->n; i++)
		pin_output_destroy(st->outs[i]);
	free(st->outs);
	st->outs = NULL;
	st->n = 0;
}
