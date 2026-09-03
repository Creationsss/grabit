// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/capture.h"
#include "capture/freeze.h"
#include "capture/save.h"
#include "clipboard/clipboard.h"
#include "config/config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "ocr/ocr.h"
#include "paths.h"
#include "pin/pin.h"
#include "pin/preview.h"
#include "pin/text_card.h"
#include "plugin/dispatch.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "region/region.h"
#include "sound/sound.h"
#include "upload/upload.h"
#include "util/util.h"
#include "wl/wl.h"

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

int gapp_try_dispatch_plugin(const char *name, int argc, char **argv) {
	if (!plugin_name_is_valid(name)) return -1;
	char path[1024];
	if (plugin_resolve(name, path, sizeof path) != 0) return -1;

	plugin_maybe_auto_update(name);
	plugin_dispatch_set_env(name);

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

	bool want_capture = !no_capture &&
						(force_capture ||
						 (manifest_auto && !plugin_argv_has_input(argc, argv)));

	char *captured = NULL;
	struct config cap_cfg;
	bool cap_cfg_loaded = false;
	bool cap_is_temp = false;
	if (want_capture) {
		if (config_load_full(&cap_cfg) != 0) {
			log_error("plugin: --capture: config_load failed");
			exit(1);
		}
		cap_cfg_loaded = true;
		struct args ca = {0};
		captured = gapp_capture_to_file(&ca, &cap_cfg, ACTION_OUTPUT, &cap_is_temp, NULL);
		if (!captured) {
			config_free(&cap_cfg);
			exit(1);
		}
		log_debug("plugin: captured %s for %s", captured, name);
	}

	int extra = captured ? 1 : 0;
	char **new_argv = calloc((size_t)argc + 1 + extra, sizeof *new_argv);
	if (!new_argv) {
		if (cap_cfg_loaded) config_free(&cap_cfg);
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
	if (cap_cfg_loaded) config_free(&cap_cfg);
	free(captured);
	log_error("plugin: exec %s: %s", path, strerror(err));
	return 1;
}
