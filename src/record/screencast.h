// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_SCREENCAST_H
#define GRABIT_RECORD_SCREENCAST_H

#include <stdbool.h>
#include <stdint.h>

#include "region/region.h"

struct grabit_wl_state;
struct screencast;

bool screencast_available(struct grabit_wl_state *s);
const char *screencast_backend_name(struct grabit_wl_state *s);

struct screencast *screencast_start(struct grabit_wl_state *s, struct rect r,
									bool cursor, uint32_t *out_node_id);
void screencast_stop(struct screencast *sc);

void screencast_explain_unavailable(void);

#endif
