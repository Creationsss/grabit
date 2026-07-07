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
		log_info("configure with: grabit set <key> <value>");
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
