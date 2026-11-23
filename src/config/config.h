// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CONFIG_H
#define GRABIT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kv {
	char *key;
	char *val;
	unsigned src;
};

struct config {
	struct kv *kvs;
	size_t n;
	size_t cap;
	char **sources;
	size_t n_sources;
	bool overlaid;
	bool unparsed;
};

int config_load(struct config *c);
int config_read(struct config *c);
const char *config_source(const struct config *c, const char *key);
int64_t config_stamp(void);
void config_watch_add(int inotify_fd);
int config_save(struct config *c);
void config_free(struct config *c);
int config_copy(struct config *dst, const struct config *src);

int config_load_full(struct config *c);
bool config_exists(void);
void config_state_migrate(struct config *cfg);
int config_state_put(struct config *cfg, const char *const *keys,
					 const char *const *vals, size_t n);
int config_state_clear(struct config *cfg, const char *key);

const char *config_get(struct config *c, const char *key);
int config_set(struct config *c, const char *key, const char *value);
int config_get_int_clamp(struct config *c, const char *key, int def, int lo, int hi);
bool config_also_save(struct config *c);

enum config_kind {
	CFG_KIND_TEXT,
	CFG_KIND_BOOL,
	CFG_KIND_ENUM,
	CFG_KIND_INT,
	CFG_KIND_KEYBIND,
};

enum config_values_src {
	CFG_VALUES_FIXED,
	CFG_VALUES_OUTPUTS,
	CFG_VALUES_UPLOADERS,
	CFG_VALUES_ICONS,
};

struct config_alias {
	const char *from;
	const char *to;
};

struct config_key_info {
	enum config_kind kind;
	enum config_values_src values_src;
	const char *const *values;
	const struct config_alias *aliases;
	bool allow_empty;
	bool open_values;
	long lo;
	long hi;
	bool allow_auto;
	bool secret;
	bool state;
	bool internal;
	const char *def;
	const char *example;
};

int config_key_info(const char *key, struct config_key_info *out);
const char *config_alias_target(const struct config_key_info *info, const char *value);
bool cfg_in_list(const char *needle, const char *const *list);
int config_write_key(const char *key, const char *value);
bool config_key_known(const char *key);
int config_remove_key(const char *key);
const char *config_suggest_key(const char *key);
void config_each_key(void (*fn)(const char *key, void *data), void *data);

int cmd_set(int argc, char **argv);
int cmd_get(int argc, char **argv);
int cmd_unset(int argc, char **argv);

#endif
