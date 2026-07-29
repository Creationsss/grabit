// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "paths.h"
#include "util/util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vendor/tomlc99/toml.h"

static void note_unknown(const char *full) {
	if (!cfg_key_is_known(full)) log_debug("config: unknown key %s", full);
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
			note_unknown(full);
			int rc = cfg_kv_upsert(c, full, s.u.s);
			free(s.u.s);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t b = toml_bool_in(t, k);
		if (b.ok) {
			note_unknown(full);
			int rc = cfg_kv_upsert(c, full, b.u.b ? "true" : "false");
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t n = toml_int_in(t, k);
		if (n.ok) {
			note_unknown(full);
			char buf[32];
			snprintf(buf, sizeof buf, "%lld", (long long)n.u.i);
			int rc = cfg_kv_upsert(c, full, buf);
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
	if (cfg_kv_upsert(c, "log_file", "true") != 0) return -1;
	if (cfg_kv_upsert(c, "also_save", "false") != 0) return -1;
	return 0;
}

static void config_apply_runtime(struct config *c) {
	const char *v = config_get(c, "log_file");
	if (v && strcmp(v, "false") == 0) log_file_disable();

	v = config_get(c, "capture.backend");
	if (v && v[0] && !getenv("GRABIT_CAPTURE_BACKEND"))
		setenv("GRABIT_CAPTURE_BACKEND", v, 1);
}

bool config_exists(void) {
	struct stat st;
	return stat(paths_config_file(), &st) == 0 && st.st_size > 0;
}

int config_load(struct config *c) {
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
		config_apply_runtime(c);
		return 0;
	}

	FILE *f = fopen(file, "r");
	if (!f) {
		log_error("open(%s): %s", file, strerror(errno));
		return -1;
	}
	char errbuf[256];
	toml_table_t *root = toml_parse_file(f, errbuf, sizeof errbuf);
	fclose(f);
	if (!root) {
		char *broken = NULL;
		if (grabit_xasprintf(&broken, "%s.broken", file) != 0 ||
			rename(file, broken) != 0) {
			log_error("parse %s: %s", file, errbuf);
			free(broken);
			return -1;
		}
		log_warn("config %s unparseable (%s); moved to %s, using defaults", file,
				 errbuf, broken);
		free(broken);
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
		config_apply_runtime(c);
		return 0;
	}

	int rc = flatten_table(root, "", c);
	toml_free(root);
	if (rc != 0) {
		config_free(c);
		return -1;
	}
	config_apply_runtime(c);
	return 0;
}

int config_load_full(struct config *c) {
	if (config_load(c) != 0) return -1;
	config_state_overlay(c);
	return 0;
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

void config_state_overlay(struct config *cfg) {
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

void config_state_migrate(struct config *cfg) {
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
	return state_write_from(cfg);
}

int config_state_clear(const char *key) {
	struct config st;
	if (state_read(&st) != 0) return 0;
	int rc = 0;
	if (cfg_kv_remove(&st, key, false) > 0) rc = config_state_save(&st);
	config_free(&st);
	return rc;
}
