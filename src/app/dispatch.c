// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/freeze.h"
#include "config/config.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "pin/pin.h"
#include "plugin/dispatch.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "sound/sound.h"
#include "ui/ui.h"
#include "upload/upload.h"
#include "util/util.h"

#include "app/app.h"

static bool is_value_flag(const char *s) {
	return strcmp(s, "-f") == 0 || strcmp(s, "--filename") == 0 ||
		   strcmp(s, "--format") == 0 || strcmp(s, "--delay") == 0;
}

static bool plugin_argv_has_input(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (is_value_flag(argv[i])) i++;
			continue;
		}
		return true;
	}
	return false;
}

char *gapp_plugin_capture(const char *name, int argc, char **argv) {
	bool force_capture = false;
	bool no_capture = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--capture") == 0)
			force_capture = true;
		else if (strcmp(argv[i], "--no-capture") == 0)
			no_capture = true;
	}

	bool manifest_auto = false;
	char manifest_path[1024];
	int n = snprintf(manifest_path, sizeof manifest_path, "%s/%s/manifest.toml",
					 plugin_dir_path(), name);
	if (n > 0 && (size_t)n < sizeof manifest_path) {
		struct plugin_manifest m;
		if (plugin_manifest_parse_file(manifest_path, &m) == 0) {
			manifest_auto = m.capture_auto;
			plugin_manifest_free(&m);
		}
	}

	if (no_capture ||
		(!force_capture && !(manifest_auto && !plugin_argv_has_input(argc, argv))))
		return NULL;

	struct config cap_cfg;
	if (config_load_full(&cap_cfg) != 0) {
		log_error("plugin: --capture: config_load failed");
		exit(1);
	}
	notify_init(&cap_cfg, log_is_silent());
	struct args ca = {0};
	bool cap_is_temp = false;
	char *captured =
		gapp_capture_to_file(&ca, &cap_cfg, ACTION_OUTPUT, &cap_is_temp, NULL, NULL);
	config_free(&cap_cfg);
	if (!captured) exit(1);
	log_debug("plugin: captured %s for %s", captured, name);
	return captured;
}

static const struct {
	const char *name;
	gapp_command_fn run;
} COMMANDS[] = {
	{"settings", cmd_settings},
	{"set", cmd_set},
	{"get", cmd_get},
	{"unset", cmd_unset},
	{"sxcu", cmd_sxcu},
	{"upload", cmd_upload},
	{"plugin", cmd_plugin},
};

gapp_command_fn gapp_find_command(const char *name) {
	for (size_t i = 0; i < sizeof COMMANDS / sizeof *COMMANDS; i++)
		if (strcmp(COMMANDS[i].name, name) == 0) return COMMANDS[i].run;
	return NULL;
}

bool gapp_name_is_reserved(const char *name) {
	return name && (strcmp(name, "help") == 0 || gapp_find_command(name));
}

int gapp_try_dispatch_plugin(const char *name, int argc, char **argv) {
	if (!plugin_name_is_valid(name)) return -1;
	char path[1024];
	int found = plugin_resolve(name, path, sizeof path);
	if (found == -2) plugin_log_disabled(name);
	if (found != 0) return found == -2 ? 1 : -1;

	plugin_maybe_auto_update(name);
	plugin_dispatch_set_env(name);

	char *captured = gapp_plugin_capture(name, argc, argv);

	int extra = captured ? 1 : 0;
	char **new_argv = calloc((size_t)argc + 1 + extra, sizeof *new_argv);
	if (!new_argv) {
		free(captured);
		return -1;
	}
	int o = 0;
	new_argv[o++] = path;
	if (captured) new_argv[o++] = captured;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--capture") == 0) continue;
		if (strcmp(argv[i], "--no-capture") == 0) continue;
		new_argv[o++] = argv[i];
	}

	execv(path, new_argv);
	int err = errno;
	free(new_argv);
	free(captured);
	log_error("plugin: exec %s: %s", path, strerror(err));
	return 1;
}
