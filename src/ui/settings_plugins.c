// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "log.h"
#include "plugin/plugin.h"
#include "util/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PLUGIN_PREFIX "grabit "
#define CONFIRM_KEY "plugins.confirm_remove"
#define UPDATE_KEY "grabit plugin update"

static const char *name_of(const struct set_row *r) {
	return r->key + strlen(PLUGIN_PREFIX);
}

static bool busy(struct settings_state *st) {
	if (st->plugin_jobs.pid <= 0) return false;
	set_toast(st, "Wait for the plugin update to finish");
	return true;
}

static void run(struct settings_state *st, const char *name, const char *done, int (*fn)(const char *name, bool arg), bool arg) {
	if (busy(st)) return;
	char err[192];
	log_capture_errors(err, sizeof err);
	int rc = fn(name, arg);
	log_capture_errors(NULL, 0);
	if (rc != 0)
		set_toast(st, "%s", err[0] ? err : "could not change this plugin");
	else
		set_toast(st, "Plugin %s %s", name, done);
	set_refresh_plugins(st);
}

static int remove_fn(const char *name, bool unused) {
	(void)unused;
	return plugin_remove(name);
}

static void remove_plugin(struct settings_state *st, const char *name) {
	run(st, name, "removed", remove_fn, false);
}

static void toggle_plugin(struct settings_state *st, struct set_row *r) {
	bool off = !plugin_is_disabled(name_of(r));
	run(st, name_of(r), off ? "disabled" : "enabled", plugin_set_disabled, off);
}

static void describe_plugin(struct set_row *r, const char *name) {
	bool off = plugin_is_disabled(name);
	r->alt_button = off ? "Enable" : "Disable";
	char *path = plugin_path_for(name, "manifest.toml");
	struct plugin_manifest m = {0};
	if (path && plugin_manifest_parse_file(path, &m) == 0) {
		const char *desc = m.description && m.description[0] ? m.description : "no description";
		grabit_xasprintf(&r->owned_hint, "%s%s", off ? "Disabled. " : "", desc);
		plugin_manifest_free(&m);
	}
	free(path);
}

static const struct set_list PLUGIN_LIST = {
	.action = set_remove_plugin_ask,
	.alt_action = toggle_plugin,
	.prefix = PLUGIN_PREFIX,
	.suffix = "",
	.section = "Installed plugins",
	.button = "Remove",
	.unreadable = "could not read this plugin's manifest.toml",
	.describe = describe_plugin,
};

void set_refresh_plugins(struct settings_state *st) {
	char **names = NULL;
	size_t n = 0;
	if (plugin_list(&names, &n) != 0) return;
	set_sync_list(st, st->plugins_page, &PLUGIN_LIST, names, n);
	grabit_strv_free(names, n);
}

void set_remove_plugin_ask(struct settings_state *st, struct set_row *r) {
	const char *name = name_of(r);
	struct set_row *c = set_find_row(st, CONFIRM_KEY);
	if (c && strcmp(set_effective(st, c), "false") == 0) {
		remove_plugin(st, name);
		return;
	}
	char *arg = strdup(name);
	if (!arg) return;
	char title[96], text[256];
	snprintf(title, sizeof title, "Remove plugin %s?", name);
	snprintf(text, sizeof text,
			 "This deletes %s and everything in its folder. To keep it installed but stop "
			 "grabit %s from running it, use Disable instead.",
			 name, name);
	set_dialog_open(st, title, text, arg);
	set_dialog_button(st, "Remove", true, remove_plugin);
	set_dialog_button(st, "Cancel", false, NULL);
	set_dialog_check(st, "Don't ask again", CONFIRM_KEY);
}

static void set_update_label(struct settings_state *st, const char *label) {
	struct set_row *r = set_find_row(st, UPDATE_KEY);
	if (r) r->button = label;
}

static pid_t spawn_update(const char *name) {
	pid_t pid = fork();
	if (pid != 0) return pid;
	int null = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (null >= 0) {
		dup2(null, STDIN_FILENO);
		dup2(null, STDOUT_FILENO);
	}
	plugin_exec_update(name);
}

static void finish_jobs(struct settings_state *st) {
	struct set_plugin_jobs *j = &st->plugin_jobs;
	if (j->failed == 0 && j->n == 1)
		set_toast(st, "Plugin %s updated", j->names[0]);
	else if (j->failed == 0)
		set_toast(st, "Updated %zu plugins", j->n);
	else if (j->failed == 1)
		set_toast(st, "Could not update %s; details in grabit.log", j->failed_name);
	else
		set_toast(st, "%zu of %zu plugins failed to update; details in grabit.log", j->failed,
				  j->n);
	set_jobs_free(st);
	set_update_label(st, "Update all");
	set_refresh_plugins(st);
}

static void job_failed(struct set_plugin_jobs *j) {
	j->pid = 0;
	j->failed++;
	j->failed_name = j->names[j->next - 1];
}

static void next_job(struct settings_state *st) {
	struct set_plugin_jobs *j = &st->plugin_jobs;
	while (j->next < j->n) {
		j->pid = spawn_update(j->names[j->next++]);
		if (j->pid > 0) return;
		job_failed(j);
	}
	finish_jobs(st);
}

void set_update_plugins(struct settings_state *st, struct set_row *r) {
	(void)r;
	if (st->plugin_jobs.pid > 0) {
		set_toast(st, "Plugins are already updating");
		return;
	}
	char **names = NULL;
	size_t n = 0;
	if (plugin_list(&names, &n) != 0 || n == 0) {
		free(names);
		set_toast(st, "No plugins installed");
		return;
	}
	st->plugin_jobs = (struct set_plugin_jobs){.names = names, .n = n};
	set_update_label(st, "Updating");
	if (n == 1)
		set_toast(st, "Updating %s", names[0]);
	else
		set_toast(st, "Updating %zu plugins", n);
	next_job(st);
}

bool set_jobs_poll(struct settings_state *st) {
	struct set_plugin_jobs *j = &st->plugin_jobs;
	if (j->pid <= 0) return false;
	int status = 0;
	pid_t w = waitpid(j->pid, &status, WNOHANG);
	if (w == 0 || (w < 0 && errno == EINTR)) return false;
	if (w != j->pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		job_failed(j);
	j->pid = 0;
	next_job(st);
	return true;
}

void set_jobs_free(struct settings_state *st) {
	grabit_strv_free(st->plugin_jobs.names, st->plugin_jobs.n);
	st->plugin_jobs = (struct set_plugin_jobs){0};
}
