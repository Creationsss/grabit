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

xkb_keysym_t gkb_norm_sym(xkb_keysym_t s) {
	if (s >= XKB_KEY_A && s <= XKB_KEY_Z) s += XKB_KEY_a - XKB_KEY_A;
	return s;
}

bool gkb_mods_match(uint8_t want, uint8_t have) {
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

uint8_t gkb_mod_from_name(const char *name) {
	static const struct {
		const char *name;
		uint8_t bit;
	} mods[] = {
		{"ctrl", KB_MOD_CTRL},
		{"control", KB_MOD_CTRL},
		{"shift", KB_MOD_SHIFT},
		{"alt", KB_MOD_ALT},
		{"super", KB_MOD_SUPER},
		{"logo", KB_MOD_SUPER},
		{"meta", KB_MOD_ALT},
	};
	for (size_t i = 0; i < sizeof mods / sizeof mods[0]; i++)
		if (strcasecmp(name, mods[i].name) == 0) return mods[i].bit;
	return 0;
}

static bool eat_mod(const char **p, uint8_t *mods) {
	const char *plus = strchr(*p, '+');
	if (!plus || plus == *p) return false;
	char buf[16];
	size_t len = (size_t)(plus - *p);
	if (len >= sizeof buf) return false;
	memcpy(buf, *p, len);
	buf[len] = '\0';
	uint8_t bit = gkb_mod_from_name(buf);
	if (!bit) return false;
	*mods |= bit;
	*p = plus + 1;
	return true;
}

bool gkb_parse_bind(const char *tok, size_t len, struct keybind *out) {
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
	out->sym = gkb_norm_sym(sym);
	return true;
}

bool gkb_tok_next(const char **p, const char **tok, size_t *len) {
	if (!*p) return false;
	const char *comma = strchr(*p, ',');
	*tok = *p;
	*len = comma ? (size_t)(comma - *p) : strlen(*p);
	*p = comma ? comma + 1 : NULL;
	return true;
}

bool gkb_tok_blank(const char *tok, size_t len) {
	for (size_t i = 0; i < len; i++)
		if (!isspace((unsigned char)tok[i])) return false;
	return true;
}

void gkb_parse_list(const char *value, struct keybind_list *list) {
	list->n = 0;
	const char *p = value, *tok;
	size_t len;
	while (gkb_tok_next(&p, &tok, &len)) {
		struct keybind kb;
		if (gkb_parse_bind(tok, len, &kb)) {
			if (list->n < KB_MAX_BINDS) list->items[list->n++] = kb;
		} else if (!gkb_tok_blank(tok, len)) {
			log_warn("keys: ignoring invalid binding `%.*s`", (int)len, tok);
		}
	}
}
