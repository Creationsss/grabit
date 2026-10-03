// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/dispatch.h"

#include "app/app.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "plugin/plugin.h"
#include "util/util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void plugin_dispatch_set_env(const char *name) {
	char self[1024];
	if (grabit_self_exe(self, sizeof self) == 0) {
		setenv("GRABIT_BIN", self, 1);
	}
	setenv("GRABIT_PLUGIN_NAME", name, 1);

	char *plugin_dir = plugin_path_for(name, NULL);
	if (plugin_dir) {
		setenv("GRABIT_PLUGIN_DIR", plugin_dir, 1);
		free(plugin_dir);
	}

	const char *cache_home = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	char *cache = NULL;
	if (cache_home && cache_home[0] == '/') {
		grabit_xasprintf(&cache, "%s/grabit/plugins/%s", cache_home, name);
	} else if (home) {
		grabit_xasprintf(&cache, "%s/.cache/grabit/plugins/%s", home, name);
	}
	if (cache) {
		paths_mkdir_p(cache);
		setenv("GRABIT_CACHE_DIR", cache, 1);
		free(cache);
	}
}

static const char *last_line(struct grabit_buf *b) {
	if (!b->data) return "";
	b->len = grabit_rstrip(b->data, b->len);
	char *nl = strrchr(b->data, '\n');
	return nl ? nl + 1 : b->data;
}

int plugin_dispatch_pin(const char *name, int argc, char **argv, bool quiet) {
	if (!plugin_name_is_valid(name)) {
		if (quiet) return -1;
		log_error("plugin: invalid name `%s`", name ? name : "");
		return 1;
	}
	char path[1024];
	if (plugin_resolve(name, path, sizeof path) != 0) {
		if (quiet) return -1;
		log_error("plugin: %s not installed", name);
		char body[256];
		snprintf(body, sizeof body, "%s is not installed", name);
		notify_send(&(struct notify_opts){
			.summary = "Plugin failed",
			.body = body,
			.force = true,
		});
		return 1;
	}
	plugin_maybe_auto_update(name);
	plugin_dispatch_set_env(name);

	char *captured = gapp_plugin_capture(name, argc, argv);
	char **new_argv = calloc((size_t)argc + 2, sizeof *new_argv);
	if (!new_argv) {
		free(captured);
		return 1;
	}
	int n = 0;
	new_argv[n++] = path;
	if (captured) new_argv[n++] = captured;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--pin") == 0) continue;
		if (strcmp(argv[i], "--capture") == 0) continue;
		if (strcmp(argv[i], "--no-capture") == 0) continue;
		new_argv[n++] = argv[i];
	}

	struct grabit_buf out = {0};
	enum { PLUGIN_OUTPUT_CAP = 16u << 20 };
	bool capped = false;
	int status = 0;
	int rc = grabit_spawn_capture(new_argv, false, PLUGIN_OUTPUT_CAP, &out, &capped, &status);
	free(new_argv);
	free(captured);
	if (rc != 0) {
		grabit_buf_free(&out);
		return 1;
	}
	if (capped) log_warn("plugin: %s stdout exceeded %d MiB; truncating",
						 name, PLUGIN_OUTPUT_CAP >> 20);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		char body[256];
		snprintf(body, sizeof body, "%s exited with an error", name);
		notify_send(&(struct notify_opts){
			.summary = "Plugin failed",
			.body = body,
			.force = true,
			.log_hint = true,
		});
		if (out.data && out.data[0]) {
			fputs(out.data, stderr);
			if (out.len > 0 && out.data[out.len - 1] != '\n') fputc('\n', stderr);
		}
		grabit_buf_free(&out);
		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}

	const char *last = last_line(&out);
	if (!*last) {
		log_error("plugin: %s produced no output to pin", name);
		char body[256];
		snprintf(body, sizeof body, "%s printed no path to pin", name);
		notify_send(&(struct notify_opts){
			.summary = "Plugin failed",
			.body = body,
			.force = true,
		});
		grabit_buf_free(&out);
		return 1;
	}

	const char *self = getenv("GRABIT_BIN");
	if (!self || !*self) {
		log_error("plugin: cannot resolve the grabit executable path");
		grabit_buf_free(&out);
		return 1;
	}
	char *const pin_argv[] = {(char *)self, "--pin", "-f", (char *)last, NULL};
	execv(self, pin_argv);
	log_error("plugin: exec --pin: %s", strerror(errno));
	grabit_buf_free(&out);
	return 1;
}
