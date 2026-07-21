// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/edit_persist.h"

#include "config.h"
#include "region/region.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDIT_DEFAULT_COLOR 0xff3030u
#define EDIT_DEFAULT_WIDTH 4
#define EDIT_MIN_WIDTH 1
#define EDIT_MAX_WIDTH 20

static const struct {
	const char *name;
	uint32_t hex;
} EDIT_COLORS[] = {
	{"red", 0xff3030u},
	{"yellow", 0xfff030u},
	{"green", 0x40ff40u},
	{"blue", 0x4080ffu},
	{"black", 0x000000u},
	{"white", 0xffffffu},
};

uint32_t edit_color_from_str(const char *s) {
	if (!s || !*s) return EDIT_DEFAULT_COLOR;
	uint32_t parsed = 0;
	if (grabit_parse_hex_color(s, &parsed)) return parsed;
	for (size_t i = 0; i < sizeof EDIT_COLORS / sizeof EDIT_COLORS[0]; i++) {
		if (strcmp(EDIT_COLORS[i].name, s) == 0) return EDIT_COLORS[i].hex;
	}
	return EDIT_DEFAULT_COLOR;
}

void edit_color_to_str(uint32_t hex, char *buf, size_t cap) {
	snprintf(buf, cap, "#%06X", hex & 0xFFFFFFu);
}

int32_t edit_width_from_str(const char *s) {
	if (!s) return EDIT_DEFAULT_WIDTH;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s || v < EDIT_MIN_WIDTH || v > EDIT_MAX_WIDTH) return EDIT_DEFAULT_WIDTH;
	return (int32_t)v;
}

int32_t edit_tool_from_str(const char *s) {
	if (!s || !*s) return TOOL_PEN;
	for (int32_t i = 0; grabit_tool_names[i]; i++) {
		if (strcmp(grabit_tool_names[i], s) == 0) return i;
	}
	return TOOL_PEN;
}

void persist_edit_choices(struct config *cfg, uint32_t color, int32_t width,
						  int32_t tool) {
	char cn[10];
	edit_color_to_str(color, cn, sizeof cn);
	char wn[16];
	snprintf(wn, sizeof wn, "%d", width);
	const char *tn = (tool >= 0 && tool < TOOL_COUNT) ? grabit_tool_names[tool]
													  : grabit_tool_names[TOOL_PEN];
	const char *keys[] = {"edit.color", "edit.width", "edit.tool"};
	const char *vals[] = {cn, wn, tn};
	for (size_t i = 0; i < 3; i++) {
		const char *cur = config_get(cfg, keys[i]);
		if (!cur || strcmp(cur, vals[i]) != 0) {
			(void)config_state_put(cfg, keys, vals, 3);
			return;
		}
	}
}

bool edit_toolbar_pos_parse(const char *s, char *name_out, size_t name_cap,
							int32_t *rx, int32_t *ry) {
	const char *colon = strrchr(s, ':');
	if (!colon || colon == s) return false;
	size_t nlen = (size_t)(colon - s);
	if (nlen >= name_cap) return false;
	memcpy(name_out, s, nlen);
	name_out[nlen] = '\0';
	char *end = NULL;
	long x = strtol(colon + 1, &end, 10);
	if (end == colon + 1 || *end != ',') return false;
	const char *ys = end + 1;
	long y = strtol(ys, &end, 10);
	if (end == ys || *end != '\0') return false;
	*rx = (int32_t)x;
	*ry = (int32_t)y;
	return true;
}

void persist_toolbar_pos(struct config *cfg, const char *output,
						 int32_t rx, int32_t ry) {
	char val[96];
	snprintf(val, sizeof val, "%s:%d,%d", output, rx, ry);
	const char *key = "edit.toolbar_pos";
	const char *cur = config_get(cfg, key);
	if (cur && strcmp(cur, val) == 0) return;
	const char *valp = val;
	(void)config_state_put(cfg, &key, &valp, 1);
}
