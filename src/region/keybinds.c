// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "config/config.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <xkbcommon/xkbcommon-names.h>

#include "region/keybinds_internal.h"

static const char *const ACTION_KEYS[KA_COUNT] = {
	[KA_CONFIRM] = "keys.confirm",
	[KA_CANCEL] = "keys.cancel",
	[KA_SELECT_ALL] = "keys.select_all",
	[KA_UNDO] = "keys.undo",
	[KA_EDIT_MODE] = "keys.edit_mode",
	[KA_REGION_MODE] = "keys.region_mode",
	[KA_NUDGE_LEFT] = "keys.nudge_left",
	[KA_NUDGE_RIGHT] = "keys.nudge_right",
	[KA_NUDGE_UP] = "keys.nudge_up",
	[KA_NUDGE_DOWN] = "keys.nudge_down",
	[KA_MAGNIFIER] = "keys.magnifier",
};

static const char *const ACTION_DEFAULTS[KA_COUNT] = {
	[KA_CONFIRM] = "Return, KP_Enter, Ctrl+c",
	[KA_CANCEL] = "Escape, mouse:right",
	[KA_SELECT_ALL] = "Ctrl+a",
	[KA_UNDO] = "u, Ctrl+z",
	[KA_EDIT_MODE] = "s",
	[KA_REGION_MODE] = "q",
	[KA_NUDGE_LEFT] = "Left, KP_Left",
	[KA_NUDGE_RIGHT] = "Right, KP_Right",
	[KA_NUDGE_UP] = "Up, KP_Up",
	[KA_NUDGE_DOWN] = "Down, KP_Down",
	[KA_MAGNIFIER] = "Alt_L, Alt_R",
};

static const char *const TOOL_DEFAULTS[TOOL_COUNT] = {
	[TOOL_PEN] = "p, 1, KP_1",
	[TOOL_MARKER] = "m, 2, KP_2",
	[TOOL_LINE] = "l, 3, KP_3",
	[TOOL_RECT] = "r, 4, KP_4",
	[TOOL_ELLIPSE] = "o, 5, KP_5",
	[TOOL_ARROW] = "a, 6, KP_6",
	[TOOL_BLUR] = "b, 7, KP_7",
	[TOOL_TEXT] = "t, 8, KP_8",
	[TOOL_ERASER] = "e, 9, KP_9",
};

static bool load(struct config *cfg, const char *key, const char *def,
				 struct keybind_list *list) {
	const char *v = cfg ? config_get(cfg, key) : NULL;
	bool explicit_bind = v && *v;
	if (!explicit_bind) v = def;
	gkb_parse_list(v, list);
	if (list->n == 0) {
		gkb_parse_list(def, list);
		explicit_bind = false;
	}
	return explicit_bind;
}

static bool bind_eq(const struct keybind *a, const struct keybind *b) {
	if (a->is_button != b->is_button) return false;
	if (a->is_button) return a->button == b->button;
	return a->sym == b->sym && a->mods == b->mods;
}

static void list_drop(struct keybind_list *list, const struct keybind *b) {
	uint8_t w = 0;
	for (uint8_t i = 0; i < list->n; i++) {
		if (bind_eq(&list->items[i], b)) continue;
		list->items[w++] = list->items[i];
	}
	list->n = w;
}

void region_keymap_init(struct region_keymap *km, struct config *cfg) {
	bool set_a[KA_COUNT];
	bool set_t[TOOL_COUNT];

	for (int a = 0; a < KA_COUNT; a++)
		set_a[a] = load(cfg, ACTION_KEYS[a], ACTION_DEFAULTS[a], &km->actions[a]);
	for (int t = 0; t < TOOL_COUNT; t++) {
		char key[64];
		snprintf(key, sizeof key, "keys.tool.%s", grabit_tool_names[t]);
		set_t[t] = load(cfg, key, TOOL_DEFAULTS[t], &km->tools[t]);
	}

	for (int a = 0; a < KA_COUNT; a++) {
		if (!set_a[a]) continue;
		for (uint8_t i = 0; i < km->actions[a].n; i++) {
			const struct keybind b = km->actions[a].items[i];
			for (int o = 0; o < KA_COUNT; o++)
				if (o != a && !set_a[o]) list_drop(&km->actions[o], &b);
			for (int o = 0; o < TOOL_COUNT; o++)
				if (!set_t[o]) list_drop(&km->tools[o], &b);
		}
	}
}

static bool list_has_key(const struct keybind_list *list, xkb_keysym_t sym,
						 uint8_t mods) {
	xkb_keysym_t ns = gkb_norm_sym(sym);
	for (uint8_t i = 0; i < list->n; i++) {
		const struct keybind *b = &list->items[i];
		if (!b->is_button && b->sym == ns && gkb_mods_match(b->mods, mods))
			return true;
	}
	return false;
}

bool region_key_action(const struct region_keymap *km, enum region_action act,
					   xkb_keysym_t sym, uint8_t mods) {
	if (act >= KA_COUNT) return false;
	return list_has_key(&km->actions[act], sym, mods);
}

bool region_action_matches_sym(const struct region_keymap *km, enum region_action act,
							   xkb_keysym_t sym) {
	if (act >= KA_COUNT) return false;
	xkb_keysym_t ns = gkb_norm_sym(sym);
	const struct keybind_list *list = &km->actions[act];
	for (uint8_t i = 0; i < list->n; i++) {
		if (!list->items[i].is_button && list->items[i].sym == ns) return true;
	}
	return false;
}

bool region_button_action(const struct region_keymap *km, enum region_action act,
						  uint32_t button) {
	if (act >= KA_COUNT) return false;
	const struct keybind_list *list = &km->actions[act];
	for (uint8_t i = 0; i < list->n; i++) {
		if (list->items[i].is_button && list->items[i].button == button)
			return true;
	}
	return false;
}

int32_t region_key_tool(const struct region_keymap *km, xkb_keysym_t sym,
						uint8_t mods) {
	for (int t = 0; t < TOOL_COUNT; t++) {
		if (list_has_key(&km->tools[t], sym, mods)) return t;
	}
	return -1;
}

bool region_keybind_validate(const char *value) {
	if (!value || !*value) return true;
	const char *p = value, *tok;
	size_t len;
	int count = 0;
	while (gkb_tok_next(&p, &tok, &len)) {
		if (gkb_tok_blank(tok, len)) continue;
		struct keybind kb;
		if (!gkb_parse_bind(tok, len, &kb)) return false;
		count++;
	}
	return count > 0;
}

const char *region_keybind_action_key(enum region_action act) {
	return act < KA_COUNT ? ACTION_KEYS[act] : NULL;
}

const char *region_keybind_default(const char *key) {
	for (int a = 0; a < KA_COUNT; a++) {
		if (strcmp(key, ACTION_KEYS[a]) == 0) return ACTION_DEFAULTS[a];
	}
	if (strncmp(key, "keys.tool.", 10) == 0) {
		for (int t = 0; t < TOOL_COUNT; t++) {
			if (strcmp(key + 10, grabit_tool_names[t]) == 0) return TOOL_DEFAULTS[t];
		}
	}
	return NULL;
}

bool region_xkb_keymap_from_fd(struct xkb_context *ctx, int fd, uint32_t size,
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

uint8_t region_xkb_mods(struct xkb_state *state) {
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

void region_keybind_format(const struct keybind *b, char *out, size_t n) {
	if (b->is_button) {
		const char *name = NULL;
		if (b->button == BTN_LEFT)
			name = "left";
		else if (b->button == BTN_RIGHT)
			name = "right";
		else if (b->button == BTN_MIDDLE)
			name = "middle";
		if (name)
			snprintf(out, n, "mouse:%s", name);
		else
			snprintf(out, n, "mouse:%u", b->button);
		return;
	}
	char keyname[64];
	if (xkb_keysym_get_name(b->sym, keyname, sizeof keyname) < 0) keyname[0] = '\0';
	snprintf(out, n, "%s%s%s%s%s", (b->mods & KB_MOD_CTRL) ? "Ctrl+" : "",
			 (b->mods & KB_MOD_ALT) ? "Alt+" : "",
			 (b->mods & KB_MOD_SUPER) ? "Super+" : "",
			 (b->mods & KB_MOD_SHIFT) ? "Shift+" : "", keyname);
}
