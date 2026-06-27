// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_XKB_UTIL_H
#define GRABIT_XKB_UTIL_H

#include <stdint.h>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

void grabit_xkb_load(struct xkb_context *ctx, uint32_t format, int fd, uint32_t size,
					 struct xkb_keymap **keymap, struct xkb_state **state);

#endif
