// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_FREEZE_H
#define GRABIT_CAPTURE_FREEZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct config;
struct grabit_wl_state;
struct rect;
struct grabit_save_opts;

#define GRABIT_CAPTURE_CANCELLED (-2)

int grabit_freeze_capture(struct grabit_wl_state *s, struct config *cfg,
						  const char *path,
						  const struct grabit_save_opts *save_opts,
						  struct rect *out_rect, bool annotate, bool cursor,
						  uint32_t *inout_color, int32_t *inout_width,
						  int32_t *inout_tool,
						  bool *out_choices_dirty,
						  const struct rect *forced_region,
						  const struct rect *snap_rects, size_t n_snap_rects);

#endif
