// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
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
