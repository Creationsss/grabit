// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TEMPLATE_H
#define GRABIT_TEMPLATE_H

#include <stdbool.h>
#include <stddef.h>

struct template_ctx {
	const char *window_class;
	const char *window_title;
};

char *template_expand(const char *tpl, const struct template_ctx *ctx);
char *template_sanitize(const char *s);
const char *template_for_preset(const char *preset);
bool template_uses_window(const char *tpl);

#endif
