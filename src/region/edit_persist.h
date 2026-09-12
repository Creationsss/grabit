// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_EDIT_PERSIST_H
#define GRABIT_REGION_EDIT_PERSIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "region/ui.h"

struct config;
struct rect;

struct edit_choices {
	uint32_t color;
	int32_t width;
	int32_t tool;
};

#define EDIT_DEFAULT_WIDTH 4
#define EDIT_MIN_WIDTH 1
#define EDIT_MAX_WIDTH 20

_Static_assert(EDIT_MAX_WIDTH >= WIDTH_MAX,
			   "the persisted edit.width range must cover the toolbar slider range");

#define EDIT_SWATCH_COUNT 6
#define EDIT_SWATCHES_STR_MAX (EDIT_SWATCH_COUNT * 8 + 1)
#define EDIT_DEFAULT_COLOR 0xff3030u

bool edit_color_try(const char *s, uint32_t *out);
uint32_t edit_color_from_str(const char *s);
void edit_swatches_default(uint32_t *out);
uint32_t edit_swatch_default(size_t i);
bool edit_swatches_parse(const char *s, uint32_t *out);
bool edit_swatches_set_one(const char *cur, const char *nstr, const char *color,
						   char *buf, size_t cap);
void persist_swatches(struct config *cfg, const uint32_t *sw);
const char *edit_color_names(void);
int32_t edit_width_from_str(const char *s);
int32_t edit_tool_from_str(const char *s);
int32_t edit_line_style_from_str(const char *s);
void persist_capture_state(struct config *cfg, const struct edit_choices *ec,
						   const struct rect *last);
bool edit_toolbar_pos_parse(const char *s, char *name_out, size_t name_cap,
							int32_t *rx, int32_t *ry);
void persist_toolbar_pos(struct config *cfg, const char *output,
						 int32_t rx, int32_t ry);
bool last_region_parse(const char *s, struct rect *out);

#endif
