// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_SETTINGS_UPLOAD_H
#define GRABIT_SETTINGS_UPLOAD_H

#include "ui/settings_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <cairo/cairo.h>

#define UPQ_ZONE_H 220.0
#define UPQ_CARD_MIN_W 180.0
#define UPQ_CARD_H 84.0
#define UPQ_GAP 12.0
#define UPQ_BTN_H 34.0
#define UPQ_RES_ROW 30.0
#define UPQ_SHOWN 24
#define UPQ_POP_W 380.0
#define UPQ_POP_PREVIEW 220.0

enum upq_state {
	UQ_READY,
	UQ_WAIT,
	UQ_UPLOADING,
	UQ_DONE,
	UQ_FAILED,
};

enum upq_hit {
	UH_NONE,
	UH_ZONE,
	UH_FIELD,
	UH_CLEAR,
	UH_OPTIONS,
	UH_GO,
	UH_MORE,
	UH_ALL,
	UH_RES_CLOSE,
	UH_COPY_ALL,
	UH_POP,
	UH_POP_REMOVE,
	UH_CARD = 1000,
	UH_RES_OPEN = 100000,
	UH_RES_COPY = 200000,
};

#define UH_IN(h, base) ((h) >= (base) && (h) < (base) + 90000)

struct upq_file {
	char *path;
	char *mime;
	int64_t size;
	cairo_surface_t *thumb;
	struct media_job *job;
	bool loaded;
	enum upq_state state;
};

struct upq_result {
	char *name;
	char url[1024];
	bool failed;
};

struct upq_layout {
	struct set_rect zone;
	struct set_rect field;
	struct set_rect bar;
	struct set_rect info;
	struct set_rect res;
	struct set_rect res_close;
	struct set_rect copy_all;
	double res_rows;
	double cards_top;
	size_t cols;
	double card_w;
	size_t shown;
	double hidden_x;
	struct set_rect more;
	struct set_rect all;
	struct set_rect clear;
	struct set_rect options;
	struct set_rect go;
	double bottom;
};

struct set_upq {
	struct upq_file *files;
	size_t n;
	size_t cap;
	size_t shown;
	struct ui_text input;
	bool typing;
	size_t hover;
	size_t pop;
	bool options_open;
	struct set_row **opts;
	size_t n_opts;

	struct upq_result *res;
	size_t n_res;

	pid_t pid;
	int out_fd;
	int prog_fd;
	size_t cur;
	char out[2048];
	size_t out_len;
	char prog[128];
	size_t prog_len;
	char *overlay;
	char service[64];
	size_t done;
	size_t failed;
	int64_t batch_total;
	int64_t batch_sent;
	int64_t cur_sent;
	int64_t cur_total;
	double speed;
	int64_t sample_ms;
	int64_t sample_bytes;
};

extern const struct set_custom SET_UPQ_CUSTOM;

void upq_layout(cairo_t *cr, struct settings_state *st, const struct set_row *r,
				struct upq_layout *out);
struct set_rect upq_card(const struct upq_layout *l, size_t i);
bool upq_popover(const struct settings_state *st, const struct upq_layout *l, struct set_rect *box,
				 struct set_rect *remove);
void upq_res_icons(const struct upq_layout *l, size_t i, struct set_rect *open,
				   struct set_rect *copy);
size_t upq_hit(struct settings_state *st, const struct set_row *r, double bx, double by);
void upq_go_label(const struct set_upq *q, char *buf, size_t n);
size_t upq_changes(const struct set_upq *q);
int64_t upq_total(const struct set_upq *q);
bool upq_busy(const struct set_upq *q);
double upq_progress(const struct set_upq *q);
bool upq_finalizing(const struct set_upq *q);

void upq_draw(struct settings_state *st, cairo_t *cr, struct set_row *r, bool focused);
void upq_draw_overlay(struct settings_state *st, cairo_t *cr, struct set_row *r);

size_t upq_add_text(struct settings_state *st, const char *text, char *err, size_t n);
void upq_remove_at(struct settings_state *st, size_t i);
void upq_load_preview(struct settings_state *st, struct upq_file *f);
void upq_clear_files(struct settings_state *st);
void upq_start(struct settings_state *st);
size_t set_upload_drop(struct settings_state *st, const char *uri_list);
void upq_paste(struct settings_state *st, struct set_row *r);
void upq_set_options(struct settings_state *st, bool open);
void upq_close_results(struct settings_state *st);
void upq_copy_all(struct settings_state *st);

#endif
