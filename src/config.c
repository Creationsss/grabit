// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"
#include "paths.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vendor/tomlc99/toml.h"

static int kv_grow(struct config *c, size_t need) {
	if (c->cap >= need) return 0;
	size_t cap = c->cap ? c->cap : 16;
	while (cap < need)
		cap *= 2;
	struct kv *p = realloc(c->kvs, cap * sizeof *p);
	if (!p) return -1;
	c->kvs = p;
	c->cap = cap;
	return 0;
}

static size_t kv_lower_bound(struct config *c, const char *key) {
	size_t lo = 0, hi = c->n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (strcmp(c->kvs[mid].key, key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static struct kv *kv_find(struct config *c, const char *key) {
	size_t i = kv_lower_bound(c, key);
	if (i < c->n && strcmp(c->kvs[i].key, key) == 0) return &c->kvs[i];
	return NULL;
}

int cfg_kv_upsert(struct config *c, const char *key, const char *val) {
	size_t i = kv_lower_bound(c, key);
	if (i < c->n && strcmp(c->kvs[i].key, key) == 0) {
		char *nv = strdup(val);
		if (!nv) return -1;
		free(c->kvs[i].val);
		c->kvs[i].val = nv;
		return 0;
	}
	if (kv_grow(c, c->n + 1) != 0) return -1;
	char *new_key = strdup(key);
	if (!new_key) return -1;
	char *new_val = strdup(val);
	if (!new_val) {
		free(new_key);
		return -1;
	}
	if (i < c->n) {
		memmove(&c->kvs[i + 1], &c->kvs[i], (c->n - i) * sizeof *c->kvs);
	}
	c->kvs[i].key = new_key;
	c->kvs[i].val = new_val;
	c->n++;
	return 0;
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
			if (!cfg_key_is_known(full))
				log_warn("config: unknown key `%s` (kept; may be stale)", full);
			int rc = cfg_kv_upsert(c, full, s.u.s);
			free(s.u.s);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t b = toml_bool_in(t, k);
		if (b.ok) {
			if (!cfg_key_is_known(full))
				log_warn("config: unknown key `%s` (kept; may be stale)", full);
			int rc = cfg_kv_upsert(c, full, b.u.b ? "true" : "false");
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t n = toml_int_in(t, k);
		if (n.ok) {
			if (!cfg_key_is_known(full))
				log_warn("config: unknown key `%s` (kept; may be stale)", full);
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
	if (cfg_kv_upsert(c, "also_save", "false") != 0) return -1;
	return 0;
}

int config_load(struct config *c) {
	memset(c, 0, sizeof *c);

	const char *file = paths_config_file();
	const char *dir = paths_config_dir();
	if (paths_mkdir_p(dir) != 0) {
		log_error("mkdir -p %s: %s", dir, strerror(errno));
		return -1;
	}

	struct stat st;
	bool first_run = stat(file, &st) != 0 || st.st_size == 0;
	if (first_run) {
		if (seed_defaults(c) != 0) {
			log_error("could not seed default config (out of memory)");
			config_free(c);
			return -1;
		}
		if (config_save(c) != 0) {
			log_error("could not write default config to %s: %s", file, strerror(errno));
			config_free(c);
			return -1;
		}
		log_info("no config found at %s; wrote sensible defaults.", file);
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
			if (broken) log_error("  (and could not move it aside: %s)", strerror(errno));
			free(broken);
			return -1;
		}
		log_warn("config %s could not be parsed (%s)", file, errbuf);
		log_warn("  moved aside to %s; seeding defaults", broken);
		free(broken);
		if (seed_defaults(c) != 0) {
			log_error("could not seed default config (out of memory)");
			config_free(c);
			return -1;
		}
		if (config_save(c) != 0) {
			log_error("could not write default config to %s: %s", file, strerror(errno));
			config_free(c);
			return -1;
		}
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
			log_warn("state: ignoring non-state key `%s` in %s", st.kvs[i].key,
					 paths_state_file());
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
	log_info("  the edit.* entries in %s are now just a starting point and"
			 " can be removed",
			 paths_config_file());
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

size_t cfg_kv_remove(struct config *c, const char *key, bool prefix) {
	size_t klen = strlen(key);
	size_t removed = 0;
	for (size_t i = 0; i < c->n;) {
		bool match = prefix ? strncmp(c->kvs[i].key, key, klen) == 0
							: strcmp(c->kvs[i].key, key) == 0;
		if (!match) {
			i++;
			continue;
		}
		free(c->kvs[i].key);
		free(c->kvs[i].val);
		if (i + 1 < c->n)
			memmove(&c->kvs[i], &c->kvs[i + 1], (c->n - i - 1) * sizeof *c->kvs);
		c->n--;
		removed++;
	}
	return removed;
}

void config_free(struct config *c) {
	if (!c) return;
	for (size_t i = 0; i < c->n; i++) {
		free(c->kvs[i].key);
		free(c->kvs[i].val);
	}
	free(c->kvs);
	memset(c, 0, sizeof *c);
}

const char *config_get(struct config *c, const char *key) {
	struct kv *e = kv_find(c, key);
	return e ? e->val : NULL;
}

bool config_also_save(struct config *c) {
	const char *v = config_get(c, "also_save");
	if (!v) v = config_get(c, "save_captures");
	return v && strcmp(v, "true") == 0;
}
