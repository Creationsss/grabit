// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CONFIG_H
#define GRABIT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

struct kv {
	char *key;
	char *val;
};

struct config {
	struct kv *kvs;
	size_t n;
	size_t cap;
	bool overlaid;
};

int config_load(struct config *c);
int config_save(struct config *c);
void config_free(struct config *c);

int config_load_full(struct config *c);
bool config_exists(void);
void config_state_overlay(struct config *cfg);
void config_state_migrate(struct config *cfg);
int config_state_put(struct config *cfg, const char *const *keys,
					 const char *const *vals, size_t n);
int config_state_clear(struct config *cfg, const char *key);

const char *config_get(struct config *c, const char *key);
int config_set(struct config *c, const char *key, const char *value);
int config_get_int_clamp(struct config *c, const char *key, int def, int lo, int hi);
bool config_also_save(struct config *c);

int cmd_set(int argc, char **argv);
int cmd_get(int argc, char **argv);
int cmd_unset(int argc, char **argv);

#endif
