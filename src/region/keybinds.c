// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "config.h"
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

static xkb_keysym_t norm_sym(xkb_keysym_t s) {
	if (s >= XKB_KEY_A && s <= XKB_KEY_Z) s += XKB_KEY_a - XKB_KEY_A;
	return s;
}

static bool mods_match(uint8_t want, uint8_t have) {
	const uint8_t exact = KB_MOD_CTRL | KB_MOD_ALT | KB_MOD_SUPER;
	if ((want & exact) != (have & exact)) return false;
	if ((want & KB_MOD_SHIFT) && !(have & KB_MOD_SHIFT)) return false;
	return true;
}

static bool parse_mouse_button(const char *name, uint32_t *out) {
	if (isdigit((unsigned char)name[0])) {
		char *end = NULL;
		unsigned long n = strtoul(name, &end, 0);
		if (!end || *end != '\0' || n < BTN_MISC || n > 0x2ff) return false;
		*out = (uint32_t)n;
		return true;
	}
	if (strcasecmp(name, "left") == 0)
		*out = BTN_LEFT;
	else if (strcasecmp(name, "right") == 0)
		*out = BTN_RIGHT;
	else if (strcasecmp(name, "middle") == 0)
		*out = BTN_MIDDLE;
	else if (strcasecmp(name, "back") == 0)
		*out = BTN_BACK;
	else if (strcasecmp(name, "forward") == 0)
		*out = BTN_FORWARD;
	else if (strcasecmp(name, "side") == 0)
		*out = BTN_SIDE;
	else if (strcasecmp(name, "extra") == 0)
		*out = BTN_EXTRA;
	else
		return false;
	return true;
}

static bool eat_mod(const char **p, uint8_t *mods) {
	static const struct {
		const char *name;
		uint8_t bit;
	} prefixes[] = {
		{"ctrl+", KB_MOD_CTRL},
		{"control+", KB_MOD_CTRL},
		{"shift+", KB_MOD_SHIFT},
		{"alt+", KB_MOD_ALT},
		{"super+", KB_MOD_SUPER},
		{"logo+", KB_MOD_SUPER},
		{"meta+", KB_MOD_ALT},
	};
	for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
		size_t len = strlen(prefixes[i].name);
		if (strncasecmp(*p, prefixes[i].name, len) == 0) {
			*mods |= prefixes[i].bit;
			*p += len;
			return true;
		}
	}
	return false;
}

static bool parse_bind(const char *tok, size_t len, struct keybind *out) {
	char buf[64];
	while (len > 0 && isspace((unsigned char)*tok)) {
		tok++;
		len--;
	}
	while (len > 0 && isspace((unsigned char)tok[len - 1]))
		len--;
	if (len == 0 || len >= sizeof buf) return false;
	memcpy(buf, tok, len);
	buf[len] = '\0';

	*out = (struct keybind){0};

	static const char mouse_prefix[] = "mouse:";
	if (strncasecmp(buf, mouse_prefix, sizeof mouse_prefix - 1) == 0) {
		if (!parse_mouse_button(buf + sizeof mouse_prefix - 1, &out->button))
			return false;
		out->is_button = true;
		return true;
	}

	const char *p = buf;
	while (eat_mod(&p, &out->mods))
		;
	if (!*p) return false;
	xkb_keysym_t sym = xkb_keysym_from_name(p, XKB_KEYSYM_NO_FLAGS);
	if (sym == XKB_KEY_NoSymbol)
		sym = xkb_keysym_from_name(p, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) return false;
	out->sym = norm_sym(sym);
	return true;
}

static bool tok_next(const char **p, const char **tok, size_t *len) {
	if (!*p) return false;
	const char *comma = strchr(*p, ',');
	*tok = *p;
	*len = comma ? (size_t)(comma - *p) : strlen(*p);
	*p = comma ? comma + 1 : NULL;
	return true;
}

static bool tok_blank(const char *tok, size_t len) {
	for (size_t i = 0; i < len; i++)
		if (!isspace((unsigned char)tok[i])) return false;
	return true;
}

static void parse_list(const char *value, struct keybind_list *list) {
	list->n = 0;
	const char *p = value, *tok;
	size_t len;
	while (tok_next(&p, &tok, &len)) {
		struct keybind kb;
		if (parse_bind(tok, len, &kb)) {
			if (list->n < KB_MAX_BINDS) list->items[list->n++] = kb;
		} else if (!tok_blank(tok, len)) {
			log_warn("keys: ignoring invalid binding `%.*s`", (int)len, tok);
		}
	}
}

static bool load(struct config *cfg, const char *key, const char *def,
				 struct keybind_list *list) {
	const char *v = cfg ? config_get(cfg, key) : NULL;
	bool explicit_bind = v && *v;
	if (!explicit_bind) v = def;
	parse_list(v, list);
	if (list->n == 0) {
		parse_list(def, list);
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
	xkb_keysym_t ns = norm_sym(sym);
	for (uint8_t i = 0; i < list->n; i++) {
		const struct keybind *b = &list->items[i];
		if (!b->is_button && b->sym == ns && mods_match(b->mods, mods))
			return true;
	}
	return false;
}

bool region_key_action(const struct region_keymap *km, enum region_action act,
					   xkb_keysym_t sym, uint8_t mods) {
	if (act >= KA_COUNT) return false;
	return list_has_key(&km->actions[act], sym, mods);
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
	while (tok_next(&p, &tok, &len)) {
		if (tok_blank(tok, len)) continue;
		struct keybind kb;
		if (!parse_bind(tok, len, &kb)) return false;
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
