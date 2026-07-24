// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_APP_H
#define GRABIT_APP_H

#include <stdbool.h>

#include "args.h"
#include "capture/save.h"
#include "config/config.h"

int gapp_print_version(void);
int gapp_print_help(void);
int gapp_print_help_topics(void);
int gapp_print_help_filename(void);
int gapp_print_help_env(void);
int gapp_print_help_examples(void);

int gapp_read_int_cfg_clamp(struct config *cfg, const char *key, int def, int lo, int hi);
void gapp_maybe_show_preview(struct config *cfg, const char *image_path,
							 const char *caption, const char *click_open);
int gapp_resolve_save_opts(const struct args *a, struct config *cfg,
						   struct grabit_save_opts *out);
char *gapp_capture_to_file(const struct args *a, struct config *cfg,
						   enum action eff, bool *is_temp, struct rect *out_rect);
char *gapp_acquire_source(const struct args *a, struct config *cfg,
						  enum action eff, bool *is_temp, struct rect *out_rect);
void gapp_release_source(char *path, bool is_temp);
void gapp_clear_tmpfile(void);
void gapp_unlink_tmpfile(void);

int gapp_run_upload(struct config *cfg, const struct args *a);
int gapp_run_copy(struct config *cfg, const struct args *a);
int gapp_run_output(struct config *cfg, const struct args *a);
int gapp_run_ocr(struct config *cfg, const struct args *a);
int gapp_run_pin(struct config *cfg, const struct args *a);

int gapp_try_dispatch_plugin(const char *name, int argc, char **argv);

#endif
