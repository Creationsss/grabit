// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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
	key = cfg_canonical_key(key);
	size_t i = kv_lower_bound(c, key);
	if (i < c->n && strcmp(c->kvs[i].key, key) == 0) {
		char *nv = strdup(val);
		if (!nv) return -1;
		free(c->kvs[i].val);
		c->kvs[i].val = nv;
		c->kvs[i].src = 0;
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
	c->kvs[i].src = 0;
	c->n++;
	return 0;
}

int cfg_kv_put(struct config *c, const char *key, const char *val, unsigned src) {
	if (cfg_kv_upsert(c, key, val) != 0) return -1;
	kv_find(c, cfg_canonical_key(key))->src = src;
	return 0;
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

int config_copy(struct config *dst, const struct config *src) {
	*dst = (struct config){.overlaid = src->overlaid, .unparsed = src->unparsed};
	for (size_t i = 0; i < src->n; i++) {
		if (cfg_kv_put(dst, src->kvs[i].key, src->kvs[i].val, src->kvs[i].src) != 0) {
			config_free(dst);
			return -1;
		}
	}
	dst->sources = src->n_sources ? calloc(src->n_sources, sizeof *dst->sources) : NULL;
	for (size_t i = 0; dst->sources && i < src->n_sources; i++)
		if ((dst->sources[i] = strdup(src->sources[i]))) dst->n_sources = i + 1;
	return 0;
}

void config_free(struct config *c) {
	if (!c) return;
	for (size_t i = 0; i < c->n; i++) {
		free(c->kvs[i].key);
		free(c->kvs[i].val);
	}
	free(c->kvs);
	grabit_strv_free(c->sources, c->n_sources);
	memset(c, 0, sizeof *c);
}

const char *config_source(const struct config *c, const char *key) {
	const struct kv *kv = kv_find((struct config *)c, cfg_canonical_key(key));
	return kv && kv->src ? c->sources[kv->src - 1] : NULL;
}

int config_get_int_clamp(struct config *c, const char *key, int def, int lo, int hi) {
	const char *v = config_get(c, key);
	if (!v) return def;
	char *end = NULL;
	long n = strtol(v, &end, 10);
	if (!end || *end != '\0') return def;
	if (n < lo) return lo;
	if (n > hi) return hi;
	return (int)n;
}

const char *config_get(struct config *c, const char *key) {
	struct kv *e = kv_find(c, cfg_canonical_key(key));
	return e && e->val[0] ? e->val : NULL;
}

bool config_also_save(struct config *c) {
	const char *v = config_get(c, "also_save");
	return v && strcmp(v, "true") == 0;
}
