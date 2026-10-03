// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "exit.h"
#include "log.h"
#include "subcmd.h"
#include "util/util.h"

#include <stdio.h>
#include <string.h>

static const struct subcmd SUBCMDS[] = {
	{"add", "install", "<git-url>", "install a plugin"},
	{"list", "ls", "", "list installed plugins"},
	{"show", NULL, "<name>", "print parsed manifest"},
	{"update", NULL, "[<name>]", "update one plugin, or all if omitted"},
	{"remove", "rm", "<name> [-y]", "uninstall a plugin"},
};
#define N_SUBCMDS (sizeof SUBCMDS / sizeof SUBCMDS[0])

static int usage(void) {
	return subcmd_usage(SUBCMDS, N_SUBCMDS, "plugin");
}

static int help(void) {
	return subcmd_help(SUBCMDS, N_SUBCMDS, "plugin");
}

static bool is_sub(const char *name, const char *sub) {
	return subcmd_is(SUBCMDS, N_SUBCMDS, name, sub);
}

static int list_one_cb(const char *name, void *ud) {
	int *n = ud;
	puts(name);
	(*n)++;
	return 0;
}

static int do_list(void) {
	int n = 0;
	if (plugin_foreach_installed(list_one_cb, &n) != 0) return 1;
	return 0;
}

static int do_show(const char *name) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return 1;
	}
	char manifest_path[1024];
	int n = snprintf(manifest_path, sizeof manifest_path, "%s/%s/manifest.toml",
					 plugin_dir_path(), name);
	if (n <= 0 || (size_t)n >= sizeof manifest_path) return 1;
	struct plugin_manifest m;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) return 1;

	printf("name:        %s\n", m.name);
	if (m.description) printf("description: %s\n", m.description);
	if (m.homepage) printf("homepage:    %s\n", m.homepage);
	printf("kind:        %s\n", m.kind == PLUGIN_KIND_BUILD ? "build" : "prebuilt");
	if (m.kind == PLUGIN_KIND_BUILD) {
		printf("build cmd:   %s\n", m.build_cmd);
		printf("binary:      %s\n", m.build_binary);
	} else {
		printf("prebuilt:    %s\n", m.prebuilt_url);
		if (m.prebuilt_sha256) printf("sha256:      %s\n", m.prebuilt_sha256);
	}
	if (m.update_check_hours > 0)
		printf("auto-update: every %dh\n", m.update_check_hours);
	else
		printf("auto-update: off\n");
	if (m.branch) printf("branch:      %s\n", m.branch);
	printf("capture:     %s\n", m.capture_auto ? "yes" : "no");
	for (size_t i = 0; i < m.n_actions; i++) {
		printf("action:      %s%s%s\n", m.actions[i].name,
			   m.actions[i].description ? ": " : "",
			   m.actions[i].description ? m.actions[i].description : "");
	}

	plugin_manifest_free(&m);
	return 0;
}

int cmd_plugin(int argc, char **argv) {
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return help();
	}
	if (argc < 1) {
		help();
		return 2;
	}
	const char *sub = argv[0];
	if (is_sub("add", sub)) {
		if (argc != 2) {
			log_error("usage: grabit plugin add <git-url>");
			return 2;
		}
		return plugin_install_git(argv[1]) == 0 ? 0 : 1;
	}
	if (is_sub("list", sub)) return do_list();
	if (is_sub("show", sub)) {
		if (argc != 2) return usage();
		return do_show(argv[1]);
	}
	if (is_sub("update", sub)) {
		if (argc == 1) return plugin_update_all() == 0 ? 0 : 1;
		if (argc == 2) return plugin_update(argv[1]) == 0 ? 0 : 1;
		return usage();
	}
	if (is_sub("remove", sub)) {
		bool yes = false;
		const char *name = NULL;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0)
				yes = true;
			else if (!name)
				name = argv[i];
			else
				return usage();
		}
		if (!name) return usage();
		char what[256];
		snprintf(what, sizeof what, "remove the plugin %s and everything in its "
									"install directory",
				 name);
		if (!grabit_confirm(yes, what)) return 1;
		return plugin_remove(name) == 0 ? 0 : 1;
	}
	return usage();
}
