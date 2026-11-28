// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/keybinds.h"

#include "config/config.h"
#include "util/util.h"
#include "wl/xkb.h"

#include <stdio.h>
#include <string.h>
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
	[KA_REDO] = "keys.redo",
	[KA_DELETE] = "keys.delete",
	[KA_EDIT_MODE] = "keys.edit_mode",
	[KA_REGION_MODE] = "keys.region_mode",
	[KA_NUDGE_LEFT] = "keys.nudge_left",
	[KA_NUDGE_RIGHT] = "keys.nudge_right",
	[KA_NUDGE_UP] = "keys.nudge_up",
	[KA_NUDGE_DOWN] = "keys.nudge_down",
	[KA_MAGNIFIER] = "keys.magnifier",
	[KA_POP_OUT] = "keys.pop_out",
	[KA_ZOOM_IN] = "keys.zoom_in",
	[KA_ZOOM_OUT] = "keys.zoom_out",
	[KA_ZOOM_FIT] = "keys.zoom_fit",
	[KA_ZOOM_ACTUAL] = "keys.zoom_actual",
};

static const char *const ACTION_DEFAULTS[KA_COUNT] = {
	[KA_CONFIRM] = "Enter, Ctrl+s",
	[KA_CANCEL] = "Escape, Ctrl+w, Ctrl+q, mouse:right",
	[KA_SELECT_ALL] = "Ctrl+a",
	[KA_UNDO] = "u, Ctrl+z",
	[KA_REDO] = "Ctrl+y, Ctrl+Shift+z",
	[KA_DELETE] = "Delete, BackSpace",
	[KA_EDIT_MODE] = "s",
	[KA_REGION_MODE] = "q",
	[KA_NUDGE_LEFT] = "Left",
	[KA_NUDGE_RIGHT] = "Right",
	[KA_NUDGE_UP] = "Up",
	[KA_NUDGE_DOWN] = "Down",
	[KA_MAGNIFIER] = "Alt_L, Alt_R",
	[KA_POP_OUT] = "w",
	[KA_ZOOM_IN] = "Ctrl+plus, Ctrl+equal",
	[KA_ZOOM_OUT] = "Ctrl+minus",
	[KA_ZOOM_FIT] = "Ctrl+0",
	[KA_ZOOM_ACTUAL] = "Ctrl+1",
};

static const char *const TOOL_DEFAULTS[TOOL_COUNT] = {
	[TOOL_PEN] = "p, 1",
	[TOOL_MARKER] = "m, 2",
	[TOOL_LINE] = "l, 3",
	[TOOL_RECT] = "r, 4",
	[TOOL_RRECT] = "",
	[TOOL_ELLIPSE] = "o, 5",
	[TOOL_ARROW] = "a, 6",
	[TOOL_ARROW_PEN] = "",
	[TOOL_BLUR] = "b, 7",
	[TOOL_PIXELATE] = "x, 0",
	[TOOL_SPOTLIGHT] = "h",
	[TOOL_TEXT] = "t, 8",
	[TOOL_COUNTER] = "c",
	[TOOL_CALLOUT] = "k",
	[TOOL_ERASER] = "e, 9",
};

static bool load(struct config *cfg, const char *key, const char *def,
				 struct keybind_list *list) {
	const char *v = cfg ? config_get(cfg, key) : NULL;
	bool explicit_bind = v != NULL;
	if (!explicit_bind) v = def;
	gkb_parse_list(v, list);
	if (list->n == 0) {
		gkb_parse_list(def, list);
		explicit_bind = false;
	}
	return explicit_bind;
}

bool region_keybind_eq(const struct keybind *a, const struct keybind *b) {
	if (a->is_button != b->is_button) return false;
	if (a->is_button) return a->button == b->button;
	return a->sym == b->sym && a->mods == b->mods;
}

bool region_keybind_list_add(struct keybind_list *list, const struct keybind *b) {
	for (uint8_t i = 0; i < list->n; i++)
		if (region_keybind_eq(&list->items[i], b)) return false;
	if (list->n >= KB_MAX_BINDS) return false;
	list->items[list->n++] = *b;
	return true;
}

void region_keybind_list_join(const struct keybind_list *list, char *out, size_t n) {
	out[0] = '\0';
	size_t used = 0;
	for (uint8_t i = 0; i < list->n; i++) {
		char one[REGION_KEYBIND_STR * 2];
		region_keybind_format(&list->items[i], one, sizeof one);
		if (!grabit_join_appendf(out, n, &used, ", ", "%s", one)) break;
	}
}

enum region_capture region_capture_key(struct keybind_list *list, xkb_keysym_t sym, uint8_t mods) {
	if (sym == XKB_KEY_Escape) return REGION_CAPTURE_CANCEL;
	if (sym == XKB_KEY_Return) return REGION_CAPTURE_SAVE;
	if (sym == XKB_KEY_BackSpace) {
		if (list->n == 0) return REGION_CAPTURE_IGNORED;
		list->n--;
		return REGION_CAPTURE_CHANGED;
	}
	if (sym == XKB_KEY_NoSymbol || grabit_xkb_is_modifier(sym)) return REGION_CAPTURE_IGNORED;
	struct keybind b = {.sym = gkb_norm_sym(sym), .mods = mods};
	return region_keybind_list_add(list, &b) ? REGION_CAPTURE_CHANGED : REGION_CAPTURE_IGNORED;
}

static void list_drop(struct region_keymap *km, struct keybind_list *list, int from, int to,
					  const struct keybind *b) {
	uint8_t w = 0;
	for (uint8_t i = 0; i < list->n; i++) {
		if (region_keybind_eq(&list->items[i], b)) continue;
		list->items[w++] = list->items[i];
	}
	if (w != list->n && km->n_drops < KB_DROPS_MAX)
		km->drops[km->n_drops++] = (struct keybind_drop){(uint8_t)from, (uint8_t)to, *b};
	list->n = w;
}

void region_keymap_init(struct region_keymap *km, struct config *cfg) {
	bool set_a[KA_COUNT];
	bool set_t[TOOL_COUNT];

	for (int a = 0; a < KA_COUNT; a++)
		set_a[a] = load(cfg, ACTION_KEYS[a], ACTION_DEFAULTS[a], &km->actions[a]);
	for (int t = 0; t < TOOL_COUNT; t++)
		set_t[t] = load(cfg, region_keybind_tool_key(t), TOOL_DEFAULTS[t], &km->tools[t]);

	km->n_drops = 0;
	for (int a = 0; a < KA_COUNT; a++) {
		if (!set_a[a]) continue;
		for (uint8_t i = 0; i < km->actions[a].n; i++) {
			const struct keybind b = km->actions[a].items[i];
			for (int o = 0; o < KA_COUNT; o++)
				if (o != a && !set_a[o]) list_drop(km, &km->actions[o], a, o, &b);
			for (int o = 0; o < TOOL_COUNT; o++)
				if (!set_t[o]) list_drop(km, &km->tools[o], a, KA_COUNT + o, &b);
		}
	}
}

#define KB_SLOTS (KA_COUNT + TOOL_COUNT)

static const char *slot_key(int i) {
	return i < KA_COUNT ? region_keybind_action_key(i) : region_keybind_tool_key(i - KA_COUNT);
}

static const struct keybind_list *slot_list(const struct region_keymap *km, int i) {
	return i < KA_COUNT ? &km->actions[i] : &km->tools[i - KA_COUNT];
}

static size_t add_clash(struct region_keybind_clash *out, size_t n, size_t max, int a, int b,
						const struct keybind *bind, bool taken) {
	if (n >= max) return n;
	out[n] = (struct region_keybind_clash){.a = slot_key(a), .b = slot_key(b), .taken = taken};
	region_keybind_format(bind, out[n].bind, sizeof out[n].bind);
	return n + 1;
}

size_t region_keymap_clashes(struct config *cfg, struct region_keybind_clash *out, size_t max) {
	struct region_keymap km;
	region_keymap_init(&km, cfg);
	size_t n = 0;
	for (uint8_t d = 0; d < km.n_drops; d++)
		n = add_clash(out, n, max, km.drops[d].from, km.drops[d].to, &km.drops[d].bind, true);
	for (int i = 0; i < KB_SLOTS; i++) {
		const struct keybind_list *li = slot_list(&km, i);
		for (int j = i + 1; j < KB_SLOTS; j++) {
			const struct keybind_list *lj = slot_list(&km, j);
			for (uint8_t x = 0; x < li->n; x++)
				for (uint8_t y = 0; y < lj->n; y++)
					if (region_keybind_eq(&li->items[x], &lj->items[y]))
						n = add_clash(out, n, max, i, j, &li->items[x], false);
		}
	}
	return n;
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

static void first_key(const struct keybind_list *list, char *out, size_t n) {
	out[0] = '\0';
	for (uint8_t i = 0; i < list->n; i++) {
		if (list->items[i].is_button) continue;
		region_keybind_format(&list->items[i], out, n);
		return;
	}
}

void region_keybind_first(const struct region_keymap *km, enum region_action act,
						  char *out, size_t n) {
	out[0] = '\0';
	if (act >= KA_COUNT) return;
	first_key(&km->actions[act], out, n);
}

void region_keybind_first_tool(const struct region_keymap *km, int tool, char *out,
							   size_t n) {
	out[0] = '\0';
	if (tool < 0 || tool >= TOOL_COUNT) return;
	first_key(&km->tools[tool], out, n);
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

const char *region_keybind_tool_key(int tool) {
	static char keys[TOOL_COUNT][48];
	if (tool < 0 || tool >= TOOL_COUNT) return NULL;
	if (!keys[tool][0])
		snprintf(keys[tool], sizeof keys[tool], "keys.tool.%s", grabit_tool_names[tool]);
	return keys[tool];
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
	const char *alias = gkb_alias_name(b->sym);
	if (alias)
		snprintf(keyname, sizeof keyname, "%s", alias);
	else if (xkb_keysym_get_name(b->sym, keyname, sizeof keyname) < 0)
		keyname[0] = '\0';
	snprintf(out, n, "%s%s%s%s%s", (b->mods & KB_MOD_CTRL) ? "Ctrl+" : "",
			 (b->mods & KB_MOD_ALT) ? "Alt+" : "",
			 (b->mods & KB_MOD_SUPER) ? "Super+" : "",
			 (b->mods & KB_MOD_SHIFT) ? "Shift+" : "", keyname);
}
