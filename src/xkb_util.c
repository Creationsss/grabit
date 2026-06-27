// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "xkb_util.h"

#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

void grabit_xkb_load(struct xkb_context *ctx, uint32_t format, int fd, uint32_t size,
					 struct xkb_keymap **keymap, struct xkb_state **state) {
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return;
	if (*state) xkb_state_unref(*state);
	if (*keymap) xkb_keymap_unref(*keymap);
	*keymap = xkb_keymap_new_from_buffer(ctx, map, size > 0 ? size - 1 : 0,
										 XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	*state = *keymap ? xkb_state_new(*keymap) : NULL;
}
