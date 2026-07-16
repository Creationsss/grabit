// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_EDIT_PERSIST_H
#define GRABIT_REGION_EDIT_PERSIST_H

#include <stddef.h>
#include <stdint.h>

struct config;

#include <stdbool.h>

uint32_t edit_color_from_str(const char *s);
void edit_color_to_str(uint32_t hex, char *buf, size_t cap);
int32_t edit_width_from_str(const char *s);
int32_t edit_tool_from_str(const char *s);
void persist_edit_choices(struct config *cfg, uint32_t color, int32_t width,
						  int32_t tool);
bool edit_toolbar_pos_parse(const char *s, char *name_out, size_t name_cap,
							int32_t *rx, int32_t *ry);
void persist_toolbar_pos(struct config *cfg, const char *output,
						 int32_t rx, int32_t ry);

#endif
