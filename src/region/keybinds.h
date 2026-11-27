// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_KEYBINDS_H
#define GRABIT_REGION_KEYBINDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

#include "region/region.h"
#include "wl/xkb.h"

struct config;

enum region_action {
	KA_CONFIRM = 0,
	KA_CANCEL,
	KA_SELECT_ALL,
	KA_UNDO,
	KA_REDO,
	KA_DELETE,
	KA_EDIT_MODE,
	KA_REGION_MODE,
	KA_NUDGE_LEFT,
	KA_NUDGE_RIGHT,
	KA_NUDGE_UP,
	KA_NUDGE_DOWN,
	KA_MAGNIFIER,
	KA_ZOOM_IN,
	KA_ZOOM_OUT,
	KA_ZOOM_FIT,
	KA_ZOOM_ACTUAL,
	KA_COUNT,
};

#define KB_MAX_BINDS 6

struct keybind {
	bool is_button;
	xkb_keysym_t sym;
	uint32_t button;
	uint8_t mods;
};

struct keybind_list {
	struct keybind items[KB_MAX_BINDS];
	uint8_t n;
};

#define REGION_KEYBIND_STR 32
#define KB_DROPS_MAX 32

struct keybind_drop {
	uint8_t from;
	uint8_t to;
	struct keybind bind;
};

struct region_keymap {
	struct keybind_list actions[KA_COUNT];
	struct keybind_list tools[TOOL_COUNT];
	struct keybind_drop drops[KB_DROPS_MAX];
	uint8_t n_drops;
};

void region_keymap_init(struct region_keymap *km, struct config *cfg);

bool region_key_action(const struct region_keymap *km, enum region_action act,
					   xkb_keysym_t sym, uint8_t mods);
bool region_action_matches_sym(const struct region_keymap *km, enum region_action act,
							   xkb_keysym_t sym);
bool region_button_action(const struct region_keymap *km, enum region_action act,
						  uint32_t button);
int32_t region_key_tool(const struct region_keymap *km, xkb_keysym_t sym, uint8_t mods);

struct region_keybind_clash {
	const char *a;
	const char *b;
	char bind[REGION_KEYBIND_STR];
	bool taken;
};

bool region_keybind_validate(const char *value);
size_t region_keymap_clashes(struct config *cfg, struct region_keybind_clash *out, size_t max);
const char *region_keybind_action_key(enum region_action act);
const char *region_keybind_tool_key(int tool);
const char *region_tool_label(int tool);
const char *region_tool_group_label(int tool);
const char *region_keybind_default(const char *key);
void region_keybind_format(const struct keybind *b, char *out, size_t n);
void region_keybind_first(const struct region_keymap *km, enum region_action act,
						  char *out, size_t n);
void region_keybind_first_tool(const struct region_keymap *km, int tool, char *out,
							   size_t n);

uint8_t gkb_mod_from_name(const char *name);

struct grabit_wl_state;
enum region_capture {
	REGION_CAPTURE_IGNORED,
	REGION_CAPTURE_CHANGED,
	REGION_CAPTURE_SAVE,
	REGION_CAPTURE_CANCEL,
};

bool region_keybind_eq(const struct keybind *a, const struct keybind *b);
bool region_keybind_list_add(struct keybind_list *list, const struct keybind *b);
void region_keybind_list_join(const struct keybind_list *list, char *out, size_t n);
enum region_capture region_capture_key(struct keybind_list *list, xkb_keysym_t sym, uint8_t mods);

int region_keybind_watch(struct grabit_wl_state *s, const char *action_key,
						 char *out, size_t out_size);

#endif
