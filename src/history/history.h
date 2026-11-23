// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_HISTORY_H
#define GRABIT_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct config;
struct upload_result;

enum history_kind {
	HISTORY_UPLOAD = 1,
	HISTORY_FILE = 2,
	HISTORY_OCR = 4,
};
#define HISTORY_MEDIA (HISTORY_UPLOAD | HISTORY_FILE)
#define HISTORY_ALL (HISTORY_MEDIA | HISTORY_OCR)

struct history_entry {
	char *id;
	int64_t time;
	enum history_kind kind;
	bool video;
	bool delete_auth;
	bool external;
	char *path;
	char *url;
	char *service;
	char *delete_url;
	char *text;
	char *lang;
	char *target;
};

struct history_list {
	struct history_entry *v;
	size_t n;
};

const char *history_file(void);
bool history_enabled(struct config *cfg, enum history_kind kind);
int history_add(struct config *cfg, struct history_entry *e);
int history_load(unsigned kinds, struct history_list *out);
void history_list_free(struct history_list *l);
const struct history_entry *history_find(const struct history_list *l, const char *ref);
int history_remove(const char *id);
int history_clear(unsigned kinds);
int history_take(const char *id, unsigned kinds, char **taken);
int history_restore(const char *lines);
int64_t history_stamp(void);
const char *history_thumb_dir(void);
void history_thumb_path(const char *id, const char *ext, char *out, size_t n);

void history_record_upload(struct config *cfg, const char *service, const char *kept_path,
						   bool external, bool video, const struct upload_result *r);
void history_record_file(struct config *cfg, const char *path, bool video);
void history_record_ocr(struct config *cfg, const char *text, const char *lang,
						const char *target);

const char *history_kind_name(enum history_kind kind);
char *history_entry_json(const struct history_entry *e);

enum {
	HISTORY_DELETE_HOST = 1,
	HISTORY_DELETE_FILE = 2,
	HISTORY_DELETE_BOTH = HISTORY_DELETE_HOST | HISTORY_DELETE_FILE,
};
bool history_can_delete_host(const struct history_entry *e);
bool history_has_file(const struct history_entry *e);
bool history_can_delete_file(const struct history_entry *e);
int history_update(const struct history_entry *e);
bool history_host_keeps(const struct history_entry *e);
void history_delete_summary(const struct history_entry *e, unsigned what, char *out, size_t n);

#endif
