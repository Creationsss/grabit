// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_SC_BACKEND_H
#define GRABIT_RECORD_SC_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "region/region.h"

struct grabit_wl_state;

struct sc_kde;
bool sc_kde_available(struct grabit_wl_state *s);
struct sc_kde *sc_kde_start(struct grabit_wl_state *s, struct rect r, bool cursor,
							uint32_t *out_node_id);
void sc_kde_stop(struct sc_kde *k);

struct sc_gnome;
bool sc_gnome_available(void);
struct sc_gnome *sc_gnome_start(struct rect r, bool cursor, uint32_t *out_node_id);
void sc_gnome_stop(struct sc_gnome *g);

#endif
