// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"
#include "paths.h"
#include "plugin/lock.h"
#include "util/util.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

bool plugin_name_is_reserved(const char *name) {
	static const char *const RESERVED[] = {"set", "get", "unset", "sxcu", "upload",
										   "plugin", "settings", "help", NULL};
	if (!name) return false;
	for (size_t i = 0; RESERVED[i]; i++)
		if (strcmp(name, RESERVED[i]) == 0) return true;
	return false;
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

bool plugin_is_disabled(const char *name) {
	char *marker = plugin_path_for(name, ".disabled");
	bool off = marker && access(marker, F_OK) == 0;
	free(marker);
	return off;
}

void plugin_log_disabled(const char *name) {
	log_error("plugin: %s is disabled; `grabit plugin enable %s` turns it back on", name, name);
}

int plugin_check_installed(const char *name) {
	char *dir = plugin_path_for(name, NULL);
	struct stat st;
	bool ok = dir && stat(dir, &st) == 0 && S_ISDIR(st.st_mode);
	free(dir);
	if (!ok) log_error("plugin: %s is not installed", name);
	return ok ? 0 : -1;
}

static int collect(const char *name, void *ud) {
	struct {
		char **v;
		size_t n, cap;
	} *l = ud;
	if (l->n == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 8;
		char **v = realloc(l->v, cap * sizeof *v);
		if (!v) return -1;
		l->v = v;
		l->cap = cap;
	}
	l->v[l->n] = strdup(name);
	return l->v[l->n] ? (l->n++, 0) : -1;
}

int plugin_list(char ***names_out, size_t *n_out) {
	struct {
		char **v;
		size_t n, cap;
	} l = {0};
	*names_out = NULL;
	*n_out = 0;
	if (plugin_foreach_installed(collect, &l) != 0) {
		grabit_strv_free(l.v, l.n);
		return -1;
	}
	if (l.n > 1) qsort(l.v, l.n, sizeof *l.v, grabit_cmp_str);
	*names_out = l.v;
	*n_out = l.n;
	return 0;
}

int plugin_set_disabled(const char *name, bool disabled) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return -1;
	}
	if (plugin_check_installed(name) != 0) return -1;
	char *dir = plugin_path_for(name, NULL);
	char *marker = plugin_path_for(name, ".disabled");
	int lock_fd = dir && marker ? plugin_lock_acquire() : -1;
	int rc = -1, err = 0;
	if (lock_fd >= 0) {
		if (disabled)
			rc = plugin_touch(dir, ".disabled");
		else
			rc = unlink(marker) == 0 || errno == ENOENT ? 0 : -1;
		err = errno;
		plugin_lock_release(lock_fd);
		if (rc != 0)
			log_error("plugin: cannot %s %s: %s", disabled ? "disable" : "enable", name,
					  strerror(err));
		else
			log_info("plugin: %s %s", name, disabled ? "disabled" : "enabled");
	}
	free(dir);
	free(marker);
	return rc;
}

int plugin_resolve(const char *name, char *path_out, size_t cap) {
	if (!name || !*name || !path_out || cap == 0) return -1;
	const char *bin = plugin_bin_dir_path();
	if (!bin[0]) return -1;
	int n = snprintf(path_out, cap, "%s/grabit-%s", bin, name);
	if (n <= 0 || (size_t)n >= cap) return -1;
	if (access(path_out, X_OK) != 0) return -1;
	return plugin_is_disabled(name) ? -2 : 0;
}
