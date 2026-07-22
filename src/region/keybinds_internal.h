// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_KEYBINDS_INTERNAL_H
#define GRABIT_REGION_KEYBINDS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xkbcommon/xkbcommon.h>

struct keybind;
struct keybind_list;

xkb_keysym_t gkb_norm_sym(xkb_keysym_t s);
bool gkb_mods_match(uint8_t want, uint8_t have);
bool gkb_parse_bind(const char *tok, size_t len, struct keybind *out);
void gkb_parse_list(const char *value, struct keybind_list *list);
bool gkb_tok_next(const char **p, const char **tok, size_t *len);
bool gkb_tok_blank(const char *tok, size_t len);

#endif
