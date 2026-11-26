// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_XKB_H
#define GRABIT_WL_XKB_H

#include <stdbool.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

enum kb_mod {
	KB_MOD_CTRL = 1u << 0,
	KB_MOD_SHIFT = 1u << 1,
	KB_MOD_ALT = 1u << 2,
	KB_MOD_SUPER = 1u << 3,
};

bool grabit_xkb_keymap_from_fd(struct xkb_context *ctx, int fd, uint32_t size,
							   struct xkb_keymap **km, struct xkb_state **state);
uint8_t grabit_xkb_mods(struct xkb_state *state);
xkb_keysym_t grabit_xkb_unkeypad(xkb_keysym_t sym);
bool grabit_xkb_is_modifier(xkb_keysym_t sym);

#endif
