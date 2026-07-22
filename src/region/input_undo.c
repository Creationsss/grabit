// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_input_state.h"

#include "region/annotate.h"
#include "region/wlr_state.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define UNDO_HOLD_DELAY_MS 600
#define UNDO_HOLD_REPEAT_MS 80
#define TOOLTIP_DELAY_MS 1000
#define PEN_POINTS_MAX (1u << 18)
#define NUDGE_DELAY_MS 300
#define NUDGE_REPEAT_MS 30
#define NUDGE_STEP_MAX 10
#define NUDGE_ACCEL_TICKS 4

#include "region/input_state_internal.h"

struct annotation *region_anno_selected(const struct ro_state *st) {
	if (!st->out_annos || st->sel_anno < 0 ||
		(size_t)st->sel_anno >= st->out_annos->n)
		return NULL;
	return &st->out_annos->items[st->sel_anno];
}

void region_undo_arm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = true;
	struct itimerspec it = {
		.it_value = {.tv_sec = UNDO_HOLD_DELAY_MS / 1000,
					 .tv_nsec = (UNDO_HOLD_DELAY_MS % 1000) * 1000000L},
		.it_interval = {.tv_sec = UNDO_HOLD_REPEAT_MS / 1000,
						.tv_nsec = (UNDO_HOLD_REPEAT_MS % 1000) * 1000000L},
	};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

void region_undo_disarm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = false;
	struct itimerspec it = {0};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

void region_undo_begin(struct ro_state *st) {
	if (st->undo_snap_armed) return;
	st->undo_snap = (struct rect){st->sel_x, st->sel_y, st->sel_w, st->sel_h};
	st->undo_snap_has = st->has_selection;
	st->undo_snap_armed = true;
}

static void undo_push(struct ro_state *st, struct undo_item item) {
	if (st->undo_n == st->undo_cap) {
		size_t cap = st->undo_cap ? st->undo_cap * 2 : 64;
		struct undo_item *p = realloc(st->undo_items, cap * sizeof *p);
		if (!p) return;
		st->undo_items = p;
		st->undo_cap = cap;
	}
	st->undo_items[st->undo_n++] = item;
}

void region_undo_commit(struct ro_state *st) {
	if (!st->undo_snap_armed) return;
	st->undo_snap_armed = false;
	struct rect cur = {st->sel_x, st->sel_y, st->sel_w, st->sel_h};
	if (st->undo_snap_has == st->has_selection &&
		memcmp(&cur, &st->undo_snap, sizeof cur) == 0)
		return;
	undo_push(st, (struct undo_item){
					  .kind = UNDO_REGION,
					  .u.region = {.has = st->undo_snap_has, .r = st->undo_snap}});
}

void gist_undo_record_anno(struct ro_state *st) {
	undo_push(st, (struct undo_item){.kind = UNDO_ANNO_ADD});
}

void region_undo_record_anno_move(struct ro_state *st, size_t idx,
								  int32_t dx, int32_t dy) {
	if (dx == 0 && dy == 0) return;
	undo_push(st, (struct undo_item){
					  .kind = UNDO_ANNO_MOVE,
					  .u.move = {.idx = idx, .dx = dx, .dy = dy}});
}

void region_undo_record_anno_geom(struct ro_state *st, size_t idx,
								  const int32_t g[4]) {
	if (!st->out_annos || idx >= st->out_annos->n) return;
	const struct annotation *a = &st->out_annos->items[idx];
	if (a->x0 == g[0] && a->y0 == g[1] && a->x1 == g[2] && a->y1 == g[3]) return;
	struct undo_item it = {.kind = UNDO_ANNO_GEOM, .u.geom = {.idx = idx}};
	memcpy(it.u.geom.g, g, sizeof it.u.geom.g);
	undo_push(st, it);
}

void gist_undo_apply(struct ro_state *st, const struct undo_item *it) {
	struct annotation_list *annos = st->out_annos;
	switch (it->kind) {
	case UNDO_REGION:
		st->sel_x = it->u.region.r.x;
		st->sel_y = it->u.region.r.y;
		st->sel_w = it->u.region.r.w;
		st->sel_h = it->u.region.r.h;
		st->has_selection = it->u.region.has;
		break;
	case UNDO_ANNO_ADD:
		if (annos) annotation_list_pop(annos);
		break;
	case UNDO_ANNO_MOVE:
		if (annos && it->u.move.idx < annos->n) {
			annotation_translate(&annos->items[it->u.move.idx],
								 -it->u.move.dx, -it->u.move.dy);
			annos->gen++;
		}
		break;
	case UNDO_ANNO_GEOM:
		if (annos && it->u.geom.idx < annos->n) {
			struct annotation *a = &annos->items[it->u.geom.idx];
			a->x0 = it->u.geom.g[0];
			a->y0 = it->u.geom.g[1];
			a->x1 = it->u.geom.g[2];
			a->y1 = it->u.geom.g[3];
			annotation_update_bbox(a);
			annos->gen++;
		}
		break;
	}
	if (annos && st->sel_anno >= 0 && (size_t)st->sel_anno >= annos->n)
		st->sel_anno = -1;
}

void region_undo_pop(struct ro_state *st) {
	if (st->undo_n == 0) return;
	gist_undo_apply(st, &st->undo_items[--st->undo_n]);
}
