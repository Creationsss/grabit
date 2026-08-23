// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_ANNOTATE_H
#define GRABIT_REGION_ANNOTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

struct annotation;
struct annotation_list;
struct rect;

void annotation_paint(cairo_t *cr, const struct annotation *a, double scale);
void annotation_paint_backdrop(cairo_t *cr, const struct annotation *a, double scale,
							   cairo_surface_t *backdrop);
void ganno_paint_spotlights(cairo_t *cr, const struct annotation_list *list,
							const struct annotation *extra);
void annotation_list_paint(cairo_t *cr, const struct annotation_list *list,
						   int32_t origin_x, int32_t origin_y, double scale);

int annotation_list_push(struct annotation_list *list, const struct annotation *a);
void annotation_list_pop(struct annotation_list *list);
bool annotation_list_pop_take(struct annotation_list *list, struct annotation *out);
int annotation_list_insert(struct annotation_list *list, size_t idx,
						   const struct annotation *a);
bool annotation_list_remove_at(struct annotation_list *list, size_t idx,
							   struct annotation *out);
void annotation_free(struct annotation *a);

void annotation_update_bbox(struct annotation *a);
int32_t annotation_counter_radius(const struct annotation *a);
int32_t annotation_width(const struct annotation *a);
int32_t annotation_font_size(const struct annotation *a);
struct rect annotation_text_box(const struct annotation *a);
int annotation_corner_mask(const struct annotation *a);
bool annotation_hit(const struct annotation *a, int32_t x, int32_t y);
void annotation_translate(struct annotation *a, int32_t dx, int32_t dy);

#endif
