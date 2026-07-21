// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_KEYBINDS_H
#define GRABIT_REGION_KEYBINDS_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

#include "region/region.h"

struct config;

enum kb_mod {
	KB_MOD_CTRL = 1u << 0,
	KB_MOD_SHIFT = 1u << 1,
	KB_MOD_ALT = 1u << 2,
	KB_MOD_SUPER = 1u << 3,
};

enum region_action {
	KA_CONFIRM = 0,
	KA_CANCEL,
	KA_SELECT_ALL,
	KA_UNDO,
	KA_EDIT_MODE,
	KA_REGION_MODE,
	KA_NUDGE_LEFT,
	KA_NUDGE_RIGHT,
	KA_NUDGE_UP,
	KA_NUDGE_DOWN,
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

struct region_keymap {
	struct keybind_list actions[KA_COUNT];
	struct keybind_list tools[TOOL_COUNT];
};

void region_keymap_init(struct region_keymap *km, struct config *cfg);

bool region_key_action(const struct region_keymap *km, enum region_action act,
					   xkb_keysym_t sym, uint8_t mods);
bool region_button_action(const struct region_keymap *km, enum region_action act,
						  uint32_t button);
int32_t region_key_tool(const struct region_keymap *km, xkb_keysym_t sym, uint8_t mods);

bool region_keybind_validate(const char *value);
const char *region_keybind_action_key(enum region_action act);
const char *region_keybind_default(const char *key);
void region_keybind_format(const struct keybind *b, char *out, size_t n);

bool region_xkb_keymap_from_fd(struct xkb_context *ctx, int fd, uint32_t size,
							   struct xkb_keymap **km, struct xkb_state **state);
uint8_t region_xkb_mods(struct xkb_state *state);

struct grabit_wl_state;
int region_keybind_watch(struct grabit_wl_state *s, const char *action_key,
						 char *out, size_t out_size);

#endif
