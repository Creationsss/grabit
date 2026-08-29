// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_EDIT_FILE_H
#define GRABIT_CAPTURE_EDIT_FILE_H

#include <stdbool.h>
#include <stdint.h>

struct config;
struct grabit_save_opts;
struct rect;

int gapp_edit_image_file(struct config *cfg, const char *src_path,
						 const struct grabit_save_opts *opts, const char *out_path,
						 uint32_t *inout_color, int32_t *inout_width,
						 int32_t *inout_tool, bool *out_choices_dirty,
						 struct rect *out_rect);

#endif
