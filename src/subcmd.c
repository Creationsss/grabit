// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "subcmd.h"

#include "exit.h"
#include "log.h"
#include "util/util.h"

#include <stdio.h>
#include <string.h>

bool subcmd_is(const struct subcmd *list, size_t n, const char *name, const char *sub) {
	for (size_t i = 0; i < n; i++) {
		if (strcmp(list[i].name, name) != 0) continue;
		return strcmp(sub, name) == 0 ||
			   (list[i].alias && strcmp(sub, list[i].alias) == 0);
	}
	return false;
}

int subcmd_usage(const struct subcmd *list, size_t n, const char *cmd) {
	char names[256];
	size_t off = 0;
	for (size_t i = 0; i < n; i++)
		if (!grabit_join_appendf(names, sizeof names, &off, "|", "%s", list[i].name))
			break;
	log_error("usage: grabit %s <%s> [args]", cmd, names);
	return GRABIT_EXIT_USAGE;
}

int subcmd_help(const struct subcmd *list, size_t n, const char *cmd) {
	printf("usage: grabit %s <subcommand> [args]\n\n", cmd);
	for (size_t i = 0; i < n; i++) {
		char left[64];
		snprintf(left, sizeof left, "%s %s", list[i].name, list[i].args);
		if (list[i].alias)
			printf("  %-22s %s (alias: %s)\n", left, list[i].help, list[i].alias);
		else
			printf("  %-22s %s\n", left, list[i].help);
	}
	return 0;
}
