// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_SUBCMD_H
#define GRABIT_SUBCMD_H

#include <stdbool.h>
#include <stddef.h>

struct subcmd {
	const char *name;
	const char *alias;
	const char *args;
	const char *help;
};

bool subcmd_is(const struct subcmd *list, size_t n, const char *name, const char *sub);
int subcmd_usage(const struct subcmd *list, size_t n, const char *cmd);
int subcmd_help(const struct subcmd *list, size_t n, const char *cmd);

#endif
