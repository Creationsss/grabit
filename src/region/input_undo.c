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
	bool was_held = st->undo_held;
	st->undo_held = false;
	struct itimerspec it = {0};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
	if (was_held) region_render_request_redraw_all(st);
}

void region_undo_begin(struct ro_state *st) {
	if (st->undo_snap_armed) return;
	st->undo_snap = (struct rect){st->sel_x, st->sel_y, st->sel_w, st->sel_h};
	st->undo_snap_has = st->has_selection;
	st->undo_snap_armed = true;
}

static void undo_stack_push(struct undo_item **items, size_t *n, size_t *cap,
							struct undo_item item) {
	if (*n == *cap) {
		size_t c = *cap ? *cap * 2 : 64;
		struct undo_item *p = realloc(*items, c * sizeof *p);
		if (!p) return;
		*items = p;
		*cap = c;
	}
	(*items)[(*n)++] = item;
}

static void undo_item_free_owned(struct undo_item *it) {
	if (it->kind == UNDO_ANNO_READD)
		annotation_free(&it->u.readd.a);
	else if (it->kind == UNDO_ANNO_DELETE)
		annotation_free(&it->u.del.a);
}

static void redo_clear(struct ro_state *st) {
	for (size_t i = 0; i < st->redo_n; i++)
		undo_item_free_owned(&st->redo_items[i]);
	st->redo_n = 0;
}

static void undo_push(struct ro_state *st, struct undo_item item) {
	item.group = st->undo_group_active ? st->undo_group_active : ++st->undo_group_seq;
	redo_clear(st);
	undo_stack_push(&st->undo_items, &st->undo_n, &st->undo_cap, item);
}

void region_undo_group_begin(struct ro_state *st) {
	st->undo_group_active = ++st->undo_group_seq;
}

void region_undo_group_end(struct ro_state *st) {
	st->undo_group_active = 0;
}

void region_undo_free(struct ro_state *st) {
	redo_clear(st);
	for (size_t i = 0; i < st->undo_n; i++)
		undo_item_free_owned(&st->undo_items[i]);
	free(st->undo_items);
	free(st->redo_items);
	st->undo_items = NULL;
	st->redo_items = NULL;
	st->undo_n = st->undo_cap = 0;
	st->redo_n = st->redo_cap = 0;
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

void region_undo_record_anno_size(struct ro_state *st, size_t idx) {
	if (!st->out_annos || idx >= st->out_annos->n) return;
	const struct annotation *a = &st->out_annos->items[idx];
	struct undo_item it = {.kind = UNDO_ANNO_SIZE};
	it.u.size.idx = idx;
	it.u.size.width = a->width;
	it.u.size.font_size = a->font_size;
	undo_push(st, it);
}

void region_undo_record_anno_delete(struct ro_state *st, size_t idx,
									struct annotation *a) {
	struct undo_item it = {.kind = UNDO_ANNO_DELETE};
	it.u.del.idx = idx;
	it.u.del.a = *a;
	undo_push(st, it);
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

struct undo_item gist_undo_apply(struct ro_state *st, const struct undo_item *it) {
	struct annotation_list *annos = st->out_annos;
	struct undo_item inv = {.kind = it->kind};
	switch (it->kind) {
	case UNDO_REGION:
		inv.u.region.has = st->has_selection;
		inv.u.region.r = (struct rect){st->sel_x, st->sel_y, st->sel_w, st->sel_h};
		st->sel_x = it->u.region.r.x;
		st->sel_y = it->u.region.r.y;
		st->sel_w = it->u.region.r.w;
		st->sel_h = it->u.region.r.h;
		st->has_selection = it->u.region.has;
		break;
	case UNDO_ANNO_ADD:
		if (annos && annotation_list_pop_take(annos, &inv.u.readd.a))
			inv.kind = UNDO_ANNO_READD;
		break;
	case UNDO_ANNO_READD:
		inv.kind = UNDO_ANNO_ADD;
		if (annos) annotation_list_push(annos, &it->u.readd.a);
		break;
	case UNDO_ANNO_DELETE:
		inv.kind = UNDO_ANNO_REDELETE;
		inv.u.del.idx = it->u.del.idx;
		if (annos) annotation_list_insert(annos, it->u.del.idx, &it->u.del.a);
		break;
	case UNDO_ANNO_REDELETE:
		inv.kind = UNDO_ANNO_DELETE;
		inv.u.del.idx = it->u.del.idx;
		if (annos) annotation_list_remove_at(annos, it->u.del.idx, &inv.u.del.a);
		break;
	case UNDO_ANNO_MOVE:
		inv.u.move.idx = it->u.move.idx;
		inv.u.move.dx = -it->u.move.dx;
		inv.u.move.dy = -it->u.move.dy;
		if (annos && it->u.move.idx < annos->n) {
			annotation_translate(&annos->items[it->u.move.idx],
								 -it->u.move.dx, -it->u.move.dy);
			annos->gen++;
		}
		break;
	case UNDO_ANNO_GEOM:
		inv.u.geom.idx = it->u.geom.idx;
		if (annos && it->u.geom.idx < annos->n) {
			struct annotation *a = &annos->items[it->u.geom.idx];
			inv.u.geom.g[0] = a->x0;
			inv.u.geom.g[1] = a->y0;
			inv.u.geom.g[2] = a->x1;
			inv.u.geom.g[3] = a->y1;
			a->x0 = it->u.geom.g[0];
			a->y0 = it->u.geom.g[1];
			a->x1 = it->u.geom.g[2];
			a->y1 = it->u.geom.g[3];
			annotation_update_bbox(a);
			annos->gen++;
		}
		break;
	case UNDO_ANNO_SIZE:
		inv.u.size.idx = it->u.size.idx;
		if (annos && it->u.size.idx < annos->n) {
			struct annotation *a = &annos->items[it->u.size.idx];
			inv.u.size.width = a->width;
			inv.u.size.font_size = a->font_size;
			a->width = it->u.size.width;
			a->font_size = it->u.size.font_size;
			annotation_update_bbox(a);
			annos->gen++;
		} else {
			inv.u.size.width = it->u.size.width;
			inv.u.size.font_size = it->u.size.font_size;
		}
		break;
	}
	if (annos && st->sel_anno >= 0 && (size_t)st->sel_anno >= annos->n)
		st->sel_anno = -1;
	return inv;
}

static void apply_group(struct ro_state *st, struct undo_item *src, size_t *src_n,
						struct undo_item **dst, size_t *dst_n, size_t *dst_cap) {
	if (*src_n == 0) return;
	uint32_t g = src[*src_n - 1].group;
	do {
		struct undo_item it = src[--*src_n];
		struct undo_item inv = gist_undo_apply(st, &it);
		inv.group = it.group;
		undo_stack_push(dst, dst_n, dst_cap, inv);
	} while (*src_n > 0 && src[*src_n - 1].group == g);
}

void region_undo_pop(struct ro_state *st) {
	apply_group(st, st->undo_items, &st->undo_n,
				&st->redo_items, &st->redo_n, &st->redo_cap);
}

void region_redo_pop(struct ro_state *st) {
	apply_group(st, st->redo_items, &st->redo_n,
				&st->undo_items, &st->undo_n, &st->undo_cap);
}
