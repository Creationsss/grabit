// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CONFIG_INTERNAL_H
#define GRABIT_CONFIG_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

enum zl_kind { ZL_FREE,
			   ZL_ENUM,
			   ZL_INT,
			   ZL_INT_PCT };

struct zl_hdr {
	const char *name;
	enum zl_kind kind;
	const char **allowed;
};

extern const struct zl_hdr gcfg_zl_headers[];
extern const size_t gcfg_zl_headers_n;

const struct zl_hdr *gcfg_zl_find(const char *name);
int gcfg_validate_zl_header(const char *hdr, const char *value);
char *gcfg_normalize_zipline_domain(const char *value);
int gcfg_cmp_kv(const void *a, const void *b);

struct config;
int cfg_kv_upsert(struct config *c, const char *key, const char *val);
size_t cfg_kv_remove(struct config *c, const char *key, bool prefix);
int config_state_save(struct config *c);
bool cfg_is_bool_key(const char *key);

struct grabit_buf;
void gcfg_emit_string(struct grabit_buf *out, const char *s);
void gcfg_emit_key(struct grabit_buf *out, const char *k);
void gcfg_emit_value(struct grabit_buf *out, const char *key, const char *val);
int gcfg_emit_section(struct grabit_buf *out, const char *prefix, size_t len);
int cfg_file_edit(const char *path, const char *key, const char *value, bool prefix);
const char *cfg_canonical_key(const char *key);
bool cfg_is_state_key(const char *key);
bool cfg_in_list(const char *needle, const char **list);
bool cfg_is_known_service(const char *s);
bool cfg_key_is_known(const char *key);

void cfg_help_print_all_keys(void);
int cfg_help_example_for_key(const char *key, const char **example_out, const char **def_out);
int gcfg_help_example_grouped(const char *key, const char **example_out,
							  const char **def_out);
bool cfg_help_print_example(const char *example, const char *def);
const char *cfg_help_suggest_key(const char *input);
void cfg_help_report_unknown_key(const char *key);

#endif
