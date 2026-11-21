// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wl/xkb.h"

#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

bool grabit_xkb_keymap_from_fd(struct xkb_context *ctx, int fd, uint32_t size,
							   struct xkb_keymap **km, struct xkb_state **state) {
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return false;
	struct xkb_keymap *nk = xkb_keymap_new_from_buffer(ctx, map, size > 0 ? size - 1 : 0,
													   XKB_KEYMAP_FORMAT_TEXT_V1,
													   XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map, size);
	if (!nk) return false;
	struct xkb_state *ns = xkb_state_new(nk);
	if (!ns) {
		xkb_keymap_unref(nk);
		return false;
	}
	if (*state) xkb_state_unref(*state);
	if (*km) xkb_keymap_unref(*km);
	*km = nk;
	*state = ns;
	return true;
}

uint8_t grabit_xkb_mods(struct xkb_state *state) {
	static const struct {
		const char *name;
		uint8_t bit;
	} mods[] = {
		{XKB_MOD_NAME_CTRL, KB_MOD_CTRL},
		{XKB_MOD_NAME_SHIFT, KB_MOD_SHIFT},
		{XKB_MOD_NAME_ALT, KB_MOD_ALT},
		{XKB_MOD_NAME_LOGO, KB_MOD_SUPER},
	};
	if (!state) return 0;
	uint8_t out = 0;
	for (size_t i = 0; i < sizeof mods / sizeof mods[0]; i++) {
		if (xkb_state_mod_name_is_active(state, mods[i].name, XKB_STATE_MODS_EFFECTIVE) > 0)
			out |= mods[i].bit;
	}
	return out;
}

xkb_keysym_t grabit_xkb_unkeypad(xkb_keysym_t s) {
	static const xkb_keysym_t KEYPAD[][2] = {
		{XKB_KEY_KP_Enter, XKB_KEY_Return},
		{XKB_KEY_KP_Left, XKB_KEY_Left},
		{XKB_KEY_KP_Right, XKB_KEY_Right},
		{XKB_KEY_KP_Up, XKB_KEY_Up},
		{XKB_KEY_KP_Down, XKB_KEY_Down},
		{XKB_KEY_KP_Home, XKB_KEY_Home},
		{XKB_KEY_KP_End, XKB_KEY_End},
		{XKB_KEY_KP_Prior, XKB_KEY_Prior},
		{XKB_KEY_KP_Next, XKB_KEY_Next},
		{XKB_KEY_KP_Insert, XKB_KEY_Insert},
		{XKB_KEY_KP_Delete, XKB_KEY_Delete},
		{XKB_KEY_KP_Add, XKB_KEY_plus},
		{XKB_KEY_KP_Subtract, XKB_KEY_minus},
		{XKB_KEY_KP_Multiply, XKB_KEY_asterisk},
		{XKB_KEY_KP_Divide, XKB_KEY_slash},
		{XKB_KEY_KP_Decimal, XKB_KEY_period},
		{XKB_KEY_KP_Equal, XKB_KEY_equal},
		{XKB_KEY_KP_Space, XKB_KEY_space},
		{XKB_KEY_KP_Tab, XKB_KEY_Tab},
	};
	if (s >= XKB_KEY_KP_0 && s <= XKB_KEY_KP_9) return XKB_KEY_0 + (s - XKB_KEY_KP_0);
	for (size_t i = 0; i < sizeof KEYPAD / sizeof *KEYPAD; i++)
		if (KEYPAD[i][0] == s) return KEYPAD[i][1];
	return s;
}

bool grabit_xkb_is_modifier(xkb_keysym_t sym) {
	return (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) ||
		   (sym >= XKB_KEY_ISO_Lock && sym <= XKB_KEY_ISO_Level5_Lock);
}
