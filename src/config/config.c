// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "ui_theme.h"
#include "util/util.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vendor/tomlc99/toml.h"

static char *g_noticed[64];
static size_t g_n_noticed;

static bool noticed(const char *key, const char *val, bool add) {
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	char buf[512];
	snprintf(buf, sizeof buf, "%s=%s", key, val ? val : "");
	bool seen = false;
	pthread_mutex_lock(&lock);
	for (size_t i = 0; i < g_n_noticed && !seen; i++)
		seen = strcmp(g_noticed[i], buf) == 0;
	if (!seen && add && g_n_noticed < sizeof g_noticed / sizeof *g_noticed) {
		char *copy = strdup(buf);
		if (copy) g_noticed[g_n_noticed++] = copy;
	}
	pthread_mutex_unlock(&lock);
	return seen;
}

static void note_unknown(const char *full) {
	if (noticed(full, NULL, true)) return;
	const char *hint = cfg_help_suggest_key(full);
	if (hint)
		log_warn("config: unknown key `%s`; did you mean `%s`?", full, hint);
	else
		log_warn("config: unknown key `%s`; it has no effect", full);
}

static bool accept_value(const char *full, const char *val) {
	full = cfg_canonical_key(full);
	if (!cfg_key_is_known(full)) {
		note_unknown(full);
		return true;
	}
	bool repeat = noticed(full, val, false);
	if (repeat) log_mute(true);
	int rc = cfg_validate_value(full, val);
	if (repeat) log_mute(false);
	if (rc == 0) return true;
	if (!repeat) {
		noticed(full, val, true);
		log_warn("config: ignoring %s = %s; using the default", full, val);
	}
	return false;
}

static int flatten_table(toml_table_t *t, const char *prefix, struct config *c) {
	for (int i = 0;; i++) {
		const char *k = toml_key_in(t, i);
		if (!k) break;

		char *full = NULL;
		if (prefix && prefix[0]) {
			if (grabit_xasprintf(&full, "%s.%s", prefix, k) != 0) return -1;
		} else {
			full = strdup(k);
			if (!full) return -1;
		}

		toml_datum_t s = toml_string_in(t, k);
		if (s.ok) {
			int rc = accept_value(full, s.u.s) ? cfg_upsert_normalized(c, full, s.u.s) : 0;
			free(s.u.s);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t b = toml_bool_in(t, k);
		if (b.ok) {
			const char *bv = b.u.b ? "true" : "false";
			int rc = accept_value(full, bv) ? cfg_kv_upsert(c, full, bv) : 0;
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t n = toml_int_in(t, k);
		if (n.ok) {
			char buf[32];
			snprintf(buf, sizeof buf, "%lld", (long long)n.u.i);
			int rc = accept_value(full, buf) ? cfg_kv_upsert(c, full, buf) : 0;
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_table_t *sub = toml_table_in(t, k);
		if (sub) {
			int rc = flatten_table(sub, full, c);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		log_warn("dropping unsupported config value at %s", full);
		free(full);
	}
	return 0;
}

static int seed_defaults(struct config *c) {
	if (cfg_kv_upsert(c, "default_action", "copy") != 0) return -1;
	if (cfg_kv_upsert(c, "notifications", "true") != 0) return -1;
	if (cfg_kv_upsert(c, "log.enabled", "true") != 0) return -1;
	if (cfg_kv_upsert(c, "also_save", "false") != 0) return -1;
	if (cfg_kv_upsert(c, "save_state", "true") != 0) return -1;
	return 0;
}

static void config_apply_runtime(struct config *c) {
	const char *v = config_get(c, "log.enabled");
	if (v && strcmp(v, "false") == 0) log_file_disable();

	v = config_get(c, "capture.backend");
	if (v && !getenv("GRABIT_CAPTURE_BACKEND"))
		setenv("GRABIT_CAPTURE_BACKEND", v, 1);

	grabit_ui_theme_init(c);
}

bool config_exists(void) {
	struct stat st;
	return stat(paths_config_file(), &st) == 0 && st.st_size > 0;
}

static toml_table_t *parse_file(FILE *f, char *err, size_t n) {
	toml_table_t *root = toml_parse_file(f, err, (int)n);
	fclose(f);
	return root;
}

static int is_toml(const struct dirent *e) {
	size_t n = strlen(e->d_name);
	return e->d_name[0] != '.' && n > 5 && strcmp(e->d_name + n - 5, ".toml") == 0;
}

static void config_d_path(char *out, size_t n) {
	snprintf(out, n, "%s/config.d", paths_config_dir());
}

static void each_override(void (*fn)(const char *path, void *ud), void *ud) {
	char dir[PATH_MAX];
	config_d_path(dir, sizeof dir);
	struct dirent **ents = NULL;
	int n = scandir(dir, &ents, is_toml, alphasort);
	for (int i = 0; i < n; i++) {
		char path[PATH_MAX];
		if (snprintf(path, sizeof path, "%s/%s", dir, ents[i]->d_name) < (int)sizeof path)
			fn(path, ud);
		free(ents[i]);
	}
	free(ents);
	const char *env = getenv("GRABIT_CONFIG");
	char *list = env && env[0] ? strdup(env) : NULL;
	char *save = NULL;
	for (char *tok = list ? strtok_r(list, ":", &save) : NULL; tok; tok = strtok_r(NULL, ":", &save)) {
		char *path = paths_expand_tilde(tok);
		if (path) fn(path, ud);
		free(path);
	}
	free(list);
}

static void load_override(const char *path, void *ud) {
	struct config *c = ud;
	FILE *f = fopen(path, "r");
	if (!f) {
		if (!noticed(path, strerror(errno), true))
			log_warn("config: cannot read %s: %s", path, strerror(errno));
		return;
	}
	char err[256];
	toml_table_t *root = parse_file(f, err, sizeof err);
	if (!root) {
		if (!noticed(path, err, true)) log_warn("config: %s: %s; skipping it", path, err);
		return;
	}
	struct config file = {0};
	(void)flatten_table(root, "", &file);
	toml_free(root);
	char **grown = realloc(c->sources, (c->n_sources + 1) * sizeof *grown);
	char *copy = grown ? strdup(path) : NULL;
	if (grown) c->sources = grown;
	if (copy) {
		c->sources[c->n_sources++] = copy;
		for (size_t i = 0; i < file.n; i++)
			(void)cfg_kv_put(c, file.kvs[i].key, file.kvs[i].val, (unsigned)c->n_sources);
		c->overlaid = true;
	}
	config_free(&file);
}

int config_read(struct config *c) {
	memset(c, 0, sizeof *c);
	if (!config_exists()) return -1;
	FILE *f = fopen(paths_config_file(), "r");
	if (!f) return -1;
	char err[256];
	toml_table_t *root = parse_file(f, err, sizeof err);
	if (!root) return -1;
	int rc = flatten_table(root, "", c);
	toml_free(root);
	if (rc != 0) {
		config_free(c);
		return -1;
	}
	each_override(load_override, c);
	return 0;
}

static void add_stamp(const char *path, void *ud) {
	*(int64_t *)ud += grabit_file_mtime_ns(path);
}

int64_t config_stamp(void) {
	char dir[PATH_MAX];
	config_d_path(dir, sizeof dir);
	int64_t stamp = grabit_file_mtime_ns(paths_config_file()) + grabit_file_mtime_ns(dir);
	each_override(add_stamp, &stamp);
	return stamp;
}

#define WATCH_MASK (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM)

static void watch_dir_of(const char *path, void *ud) {
	char target[PATH_MAX];
	if (!realpath(path, target)) return;
	char *slash = strrchr(target, '/');
	if (!slash || slash == target) return;
	*slash = '\0';
	inotify_add_watch(*(int *)ud, target, WATCH_MASK);
}

void config_watch_add(int inotify_fd) {
	char dir[PATH_MAX];
	config_d_path(dir, sizeof dir);
	inotify_add_watch(inotify_fd, paths_config_dir(), WATCH_MASK);
	inotify_add_watch(inotify_fd, dir, WATCH_MASK);
	watch_dir_of(paths_config_file(), &inotify_fd);
	each_override(watch_dir_of, &inotify_fd);
}

static int load_base(struct config *c) {
	memset(c, 0, sizeof *c);

	const char *file = paths_config_file();
	const char *dir = paths_config_dir();
	if (paths_mkdir_p(dir) != 0) {
		log_error("mkdir %s: %s", dir, strerror(errno));
		return -1;
	}

	bool first_run = !config_exists();
	if (first_run) {
		if (seed_defaults(c) != 0) {
			log_error("out of memory");
			config_free(c);
			return -1;
		}
		if (config_save(c) != 0) {
			log_error("could not write default config to %s: %s", file, strerror(errno));
			config_free(c);
			return -1;
		}
		log_info("wrote default config to %s", file);
		return 0;
	}

	FILE *f = fopen(file, "r");
	if (!f) {
		log_error("open(%s): %s", file, strerror(errno));
		return -1;
	}
	char errbuf[256];
	toml_table_t *root = parse_file(f, errbuf, sizeof errbuf);
	if (!root) {
		log_error("%s: %s", file, errbuf);
		log_error("using built-in defaults for this run; fix the file or move it "
				  "aside, then run `grabit get` to check it");
		char body[512];
		snprintf(body, sizeof body, "%s\n%s\nusing defaults until it parses", file,
				 errbuf);
		notify_send(&(struct notify_opts){
			.summary = "Config could not be read",
			.body = body,
			.force = true,
		});
		if (seed_defaults(c) != 0) {
			log_error("out of memory");
			config_free(c);
			return -1;
		}
		c->unparsed = true;
		return 0;
	}

	int rc = flatten_table(root, "", c);
	toml_free(root);
	if (rc != 0) {
		config_free(c);
		return -1;
	}
	return 0;
}

int config_load(struct config *c) {
	if (load_base(c) != 0) return -1;
	each_override(load_override, c);
	config_apply_runtime(c);
	return 0;
}

bool cfg_state_enabled(struct config *cfg) {
	const char *v = config_get(cfg, "save_state");
	return !v || strcmp(v, "false") != 0;
}

static int state_read(struct config *c) {
	memset(c, 0, sizeof *c);
	const char *file = paths_state_file();
	FILE *f = fopen(file, "r");
	if (!f) return 1;
	char errbuf[256];
	toml_table_t *root = toml_parse_file(f, errbuf, sizeof errbuf);
	fclose(f);
	if (!root) {
		log_warn("state %s could not be parsed (%s); ignoring", file, errbuf);
		return 1;
	}
	int rc = flatten_table(root, "", c);
	toml_free(root);
	if (rc != 0) {
		config_free(c);
		return -1;
	}
	return 0;
}

static int state_write_from(struct config *cfg) {
	if (!cfg_state_enabled(cfg)) return 0;
	struct config st = {0};
	for (size_t i = 0; i < cfg->n; i++) {
		if (!cfg_is_state_key(cfg->kvs[i].key)) continue;
		if (cfg_kv_upsert(&st, cfg->kvs[i].key, cfg->kvs[i].val) != 0) {
			config_free(&st);
			return -1;
		}
	}
	int rc = config_state_save(&st);
	config_free(&st);
	return rc;
}

static void config_state_overlay(struct config *cfg) {
	if (!cfg_state_enabled(cfg)) return;
	struct config st;
	if (state_read(&st) != 0) return;
	for (size_t i = 0; i < st.n; i++) {
		if (!cfg_is_state_key(st.kvs[i].key)) {
			log_debug("state: ignoring non-state key %s", st.kvs[i].key);
			continue;
		}
		(void)cfg_kv_upsert(cfg, st.kvs[i].key, st.kvs[i].val);
	}
	config_free(&st);
	cfg->overlaid = true;
}

int config_load_full(struct config *c) {
	if (config_load(c) != 0) return -1;
	config_state_overlay(c);
	return 0;
}

void config_state_migrate(struct config *cfg) {
	if (!cfg_state_enabled(cfg)) return;
	struct config st;
	int rc = state_read(&st);
	if (rc == 0) config_free(&st);
	if (rc != 1) return;

	bool any = false;
	for (size_t i = 0; i < cfg->n && !any; i++)
		any = cfg_is_state_key(cfg->kvs[i].key);
	if (!any) return;

	if (state_write_from(cfg) != 0) return;
	log_info("edit state moved to %s", paths_state_file());
}

int config_state_put(struct config *cfg, const char *const *keys,
					 const char *const *vals, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (config_set(cfg, keys[i], vals[i]) != 0) return -1;
	}
	if (!cfg_state_enabled(cfg)) return 0;
	struct config st;
	int rd = state_read(&st);
	if (rd < 0) return -1;
	if (rd != 0) memset(&st, 0, sizeof st);
	int rc = 0;
	for (size_t i = 0; i < n && rc == 0; i++) {
		const char *v = config_get(cfg, keys[i]);
		if (v && cfg_kv_upsert(&st, keys[i], v) != 0) rc = -1;
	}
	if (rc == 0) rc = config_state_save(&st);
	config_free(&st);
	return rc;
}

int config_state_clear(struct config *cfg, const char *key) {
	if (!cfg_state_enabled(cfg)) return 0;
	struct config st;
	if (state_read(&st) != 0) return 0;
	int rc = 0;
	if (cfg_kv_remove(&st, key, false) > 0) rc = config_state_save(&st);
	config_free(&st);
	return rc;
}
