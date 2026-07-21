// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "paths.h"
#include "util.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define PLUGIN_PATH_MAX 1024

static char g_plugin_dir[PLUGIN_PATH_MAX];
static char g_plugin_bin[PLUGIN_PATH_MAX];
static bool g_init;

static void init_paths(void) {
	if (g_init) return;
	const char *cfg = paths_config_dir();
	if (cfg && cfg[0]) {
		snprintf(g_plugin_dir, sizeof g_plugin_dir, "%s/plugins", cfg);
		snprintf(g_plugin_bin, sizeof g_plugin_bin, "%s/plugins/.bin", cfg);
	} else {
		g_plugin_dir[0] = '\0';
		g_plugin_bin[0] = '\0';
	}
	g_init = true;
}

bool plugin_name_is_valid(const char *name) {
	if (!name || !*name) return false;
	for (const char *p = name; *p; p++) {
		char c = *p;
		bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				  c == '_' || c == '-';
		if (!ok) return false;
	}
	return true;
}

const char *plugin_dir_path(void) {
	init_paths();
	return g_plugin_dir;
}

const char *plugin_bin_dir_path(void) {
	init_paths();
	return g_plugin_bin;
}

char *plugin_path_for(const char *name, const char *suffix) {
	char *out = NULL;
	int rc = suffix
				 ? grabit_xasprintf(&out, "%s/%s/%s", plugin_dir_path(), name, suffix)
				 : grabit_xasprintf(&out, "%s/%s", plugin_dir_path(), name);
	return rc == 0 ? out : NULL;
}

int plugin_foreach_installed(int (*fn)(const char *name, void *ud), void *ud) {
	const char *root = plugin_dir_path();
	if (!root[0]) return -1;
	DIR *d = opendir(root);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		if (!plugin_name_is_valid(e->d_name)) continue;
		char path[PLUGIN_PATH_MAX];
		int n = snprintf(path, sizeof path, "%s/%s", root, e->d_name);
		if (n <= 0 || (size_t)n >= sizeof path) continue;
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		if (fn(e->d_name, ud) != 0) break;
	}
	closedir(d);
	return 0;
}

int plugin_resolve(const char *name, char *path_out, size_t cap) {
	if (!name || !*name || !path_out || cap == 0) return -1;
	const char *bin = plugin_bin_dir_path();
	if (!bin[0]) return -1;
	int n = snprintf(path_out, cap, "%s/grabit-%s", bin, name);
	if (n <= 0 || (size_t)n >= cap) return -1;
	if (access(path_out, X_OK) != 0) return -1;
	return 0;
}
