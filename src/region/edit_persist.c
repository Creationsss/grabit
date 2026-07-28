// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/edit_persist.h"

#include "config/config.h"
#include "region/region.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDIT_DEFAULT_COLOR 0xff3030u
#define EDIT_DEFAULT_WIDTH 4
#define EDIT_MIN_WIDTH 1
#define EDIT_MAX_WIDTH 12

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

int32_t edit_line_style_from_str(const char *s) {
	if (!s || !*s) return STROKE_SOLID;
	for (int32_t i = 0; grabit_line_style_names[i]; i++) {
		if (strcmp(grabit_line_style_names[i], s) == 0) return i;
	}
	return STROKE_SOLID;
}

static void persist_state_keys(struct config *cfg, const char *const *keys,
							   const char *const *vals, size_t n) {
	for (size_t i = 0; i < n; i++) {
		const char *cur = config_get(cfg, keys[i]);
		if (!cur || strcmp(cur, vals[i]) != 0) {
			(void)config_state_put(cfg, keys, vals, n);
			return;
		}
	}
}

void persist_capture_state(struct config *cfg, const struct edit_choices *ec,
						   const struct rect *last) {
	const char *keys[4];
	const char *vals[4];
	size_t n = 0;

	char cn[10], wn[16], rn[64];
	if (ec) {
		edit_color_to_str(ec->color, cn, sizeof cn);
		snprintf(wn, sizeof wn, "%d", ec->width);
		keys[n] = "edit.color";
		vals[n++] = cn;
		keys[n] = "edit.width";
		vals[n++] = wn;
		keys[n] = "edit.tool";
		vals[n++] = (ec->tool >= 0 && ec->tool < TOOL_COUNT)
						? grabit_tool_names[ec->tool]
						: grabit_tool_names[TOOL_PEN];
	}
	if (last && last->w > 0 && last->h > 0) {
		snprintf(rn, sizeof rn, "%d,%d,%d,%d", last->x, last->y, last->w, last->h);
		keys[n] = "region.last";
		vals[n++] = rn;
	}
	persist_state_keys(cfg, keys, vals, n);
}

static bool scan_i32_csv(const char *s, int32_t *out, int n) {
	if (!s || !*s) return false;
	const char *p = s;
	for (int i = 0; i < n; i++) {
		char *end = NULL;
		long v = strtol(p, &end, 10);
		if (end == p) return false;
		out[i] = (int32_t)v;
		p = end;
		if (i + 1 < n) {
			if (*p != ',') return false;
			p++;
		}
	}
	return *p == '\0';
}

bool edit_toolbar_pos_parse(const char *s, char *name_out, size_t name_cap,
							int32_t *rx, int32_t *ry) {
	const char *colon = strrchr(s, ':');
	if (!colon || colon == s) return false;
	size_t nlen = (size_t)(colon - s);
	if (nlen >= name_cap) return false;
	int32_t xy[2];
	if (!scan_i32_csv(colon + 1, xy, 2)) return false;
	memcpy(name_out, s, nlen);
	name_out[nlen] = '\0';
	*rx = xy[0];
	*ry = xy[1];
	return true;
}

bool last_region_parse(const char *s, struct rect *out) {
	int32_t v[4];
	if (!scan_i32_csv(s, v, 4) || v[2] <= 0 || v[3] <= 0) return false;
	out->x = v[0];
	out->y = v[1];
	out->w = v[2];
	out->h = v[3];
	return true;
}

void persist_toolbar_pos(struct config *cfg, const char *output,
						 int32_t rx, int32_t ry) {
	char val[96];
	snprintf(val, sizeof val, "%s:%d,%d", output, rx, ry);
	const char *key = "edit.toolbar_pos";
	const char *valp = val;
	persist_state_keys(cfg, &key, &valp, 1);
}
