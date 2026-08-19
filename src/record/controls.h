// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_CONTROLS_H
#define GRABIT_RECORD_CONTROLS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;
struct rect;
struct rec_controls;

struct rec_controls *controls_start(struct grabit_wl_state *s, struct rect r,
									atomic_int *stop_flag, atomic_int *pause_flag,
									atomic_int *abort_flag, bool rounded_ui);
void controls_set_paused(struct rec_controls *c, bool paused);
void controls_tick(struct rec_controls *c, int64_t secs);
void controls_stop(struct rec_controls *c);

#endif
