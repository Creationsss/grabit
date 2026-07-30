// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_SETUP_H
#define GRABIT_RECORD_SETUP_H

#include <stdbool.h>

struct args;
struct config;
struct grabit_wl_state;
struct rect;

char *rec_record_path(struct config *cfg, const struct args *a,
					  const char *format, bool keep_locally);
void rec_fail_notify(const char *body);
int rec_pick_region(struct grabit_wl_state *s, struct config *cfg,
					const struct args *a, struct rect *out);

#endif
