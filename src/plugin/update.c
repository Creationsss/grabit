// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"
#include "plugin/fetch.h"
#include "plugin/lock.h"
#include "plugin/spawn.h"
#include "plugin/state.h"
#include "util/util.h"
#include "vendor/sha256/sha256.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static bool stale(const char *path, int hours) {
	if (hours <= 0) return false;
	struct stat st;
	if (stat(path, &st) != 0) return true;
	time_t now = time(NULL);
	return (now - st.st_mtime) > (time_t)hours * 3600;
}

static int update_prebuilt(const char *plugin_dir, const char *name,
						   const char *url, const char *sha) {
	if (!url || !*url) {
		log_error("plugin: %s has no prebuilt url; reinstall it", name);
		return -1;
	}
	char *binary_path = NULL;
	char *tmp_path = NULL;
	int rc = -1;
	if (grabit_xasprintf(&binary_path, "%s/%s", plugin_dir, name) != 0) goto out;
	if (grabit_xasprintf(&tmp_path, "%s.new", binary_path) != 0) goto out;

	struct stat st;
	time_t since = (stat(binary_path, &st) == 0) ? st.st_mtime : 0;
	log_debug("plugin: checking %s for updates", name);
	enum plugin_fetch_result res = plugin_fetch_url(url, tmp_path, since);
	if (res == PLUGIN_FETCH_NOT_MODIFIED) {
		log_info("plugin: %s is up to date", name);
		rc = 0;
		goto out;
	}
	if (res != PLUGIN_FETCH_OK) goto out;

	if (plugin_verify_sha256(tmp_path, sha) != 0) {
		unlink(tmp_path);
		goto out;
	}
	chmod(tmp_path, 0755);
	int sync_fd = open(tmp_path, O_RDONLY | O_CLOEXEC);
	if (sync_fd >= 0) {
		fsync(sync_fd);
		close(sync_fd);
	}
	if (rename(tmp_path, binary_path) != 0) {
		log_error("plugin: rename %s -> %s: %s", tmp_path, binary_path, strerror(errno));
		unlink(tmp_path);
		goto out;
	}
	int dir_fd = open(plugin_dir, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
	if (dir_fd >= 0) {
		fsync(dir_fd);
		close(dir_fd);
	}
	log_info("plugin: %s updated", name);
	rc = 0;
out:
	free(binary_path);
	free(tmp_path);
	return rc;
}

int plugin_update(const char *name) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return -1;
	}
	int lock_fd = plugin_lock_acquire();
	if (lock_fd < 0) return -1;

	char *plugin_dir = NULL;
	char *manifest_path = NULL;
	struct plugin_manifest m = {0};
	int rc = -1;

	plugin_dir = plugin_path_for(name, NULL);
	manifest_path = plugin_path_for(name, "manifest.toml");
	if (!plugin_dir || !manifest_path) goto out;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) goto out;

	char source_kind[32];
	char source_url[2048];
	char source_sha[SHA256_HEX_SIZE];
	if (plugin_state_read(plugin_dir, source_kind, sizeof source_kind,
						  source_url, sizeof source_url,
						  source_sha, sizeof source_sha) != 0) {
		strncpy(source_kind, "git", sizeof source_kind - 1);
		source_kind[sizeof source_kind - 1] = '\0';
	}

	if (strcmp(source_kind, "git") == 0) {
		log_debug("plugin: updating %s (branch %s)", name, m.branch);
		char *const fetch[] = {"git", "-C", plugin_dir, "fetch", "--quiet",
							   "--depth", "1", "origin", m.branch, NULL};
		if (plugin_run_in(NULL, fetch) != 0) goto out;
		char *const reset[] = {"git", "-C", plugin_dir, "reset", "--hard", "FETCH_HEAD", NULL};
		if (plugin_run_in(NULL, reset) != 0) goto out;
		if (m.kind == PLUGIN_KIND_BUILD) {
			char *const sh[] = {"/bin/sh", "-c", m.build_cmd, NULL};
			if (plugin_run_in(plugin_dir, sh) != 0) goto out;
		}
		log_info("plugin: %s updated", name);
		rc = 0;
	} else if (strcmp(source_kind, "prebuilt") == 0) {
		const char *url = source_url[0] ? source_url : m.prebuilt_url;
		const char *sha = source_sha[0] ? source_sha : m.prebuilt_sha256;
		rc = update_prebuilt(plugin_dir, name, url, sha);
	} else {
		log_error("plugin: %s has an unknown source kind `%s`; reinstall it", name, source_kind);
	}

	plugin_touch_check(plugin_dir);
out:
	plugin_manifest_free(&m);
	free(plugin_dir);
	free(manifest_path);
	plugin_lock_release(lock_fd);
	return rc;
}

static int update_one_cb(const char *name, void *ud) {
	int *n = ud;
	if (plugin_update(name) == 0) (*n)++;
	return 0;
}

int plugin_update_all(void) {
	int n = 0;
	if (plugin_foreach_installed(update_one_cb, &n) != 0) return -1;
	log_info("plugin: checked %d plugins", n);
	return 0;
}

void plugin_maybe_auto_update(const char *name) {
	if (!plugin_name_is_valid(name)) return;

	int lock_fd = plugin_lock_try();
	if (lock_fd < 0) return;

	char *plugin_dir = NULL;
	char *manifest_path = NULL;
	char *check_path = NULL;
	struct plugin_manifest m = {0};
	bool should_spawn = false;

	plugin_dir = plugin_path_for(name, NULL);
	manifest_path = plugin_path_for(name, "manifest.toml");
	if (!plugin_dir || !manifest_path) goto out;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) goto out;
	if (m.update_check_hours <= 0) goto out;
	check_path = plugin_path_for(name, ".last_check");
	if (!check_path) goto out;
	if (!stale(check_path, m.update_check_hours)) goto out;

	char source_kind[32];
	char source_url[2048];
	char source_sha[SHA256_HEX_SIZE];
	bool prebuilt = plugin_state_read(plugin_dir, source_kind, sizeof source_kind,
									  source_url, sizeof source_url,
									  source_sha, sizeof source_sha) == 0 &&
					strcmp(source_kind, "prebuilt") == 0;
	plugin_touch_check(plugin_dir);
	if (!prebuilt) {
		log_info("plugin: %s may have updates; run `grabit plugin update %s`", name,
				 name);
		goto out;
	}
	should_spawn = true;
out:
	plugin_manifest_free(&m);
	free(plugin_dir);
	free(manifest_path);
	free(check_path);
	plugin_lock_release(lock_fd);
	if (!should_spawn) return;

	pid_t pid = fork();
	if (pid < 0) return;
	if (pid != 0) return;

	grabit_double_fork_detach();
	signal(SIGHUP, SIG_IGN);
	alarm(300);

	char log_path[1024];
	snprintf(log_path, sizeof log_path, "%s/%s/.update.log", plugin_dir_path(), name);
	int logfd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (logfd >= 0) {
		dup2(logfd, STDOUT_FILENO);
		dup2(logfd, STDERR_FILENO);
		if (logfd > STDERR_FILENO) close(logfd);
	}
	int devnull = open("/dev/null", O_RDONLY);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		if (devnull > STDERR_FILENO) close(devnull);
	}

	char self[1024];
	if (grabit_self_exe(self, sizeof self) != 0) _exit(127);

	char *const argv[] = {self, "plugin", "update", (char *)name, NULL};
	execv(self, argv);
	_exit(127);
}
