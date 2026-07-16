// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_LOOP_H
#define GRABIT_RECORD_LOOP_H

#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>

struct grabit_wl_state;
struct rec_layout;
struct buf_pool;
struct seg_ctx;
struct rec_controls;

extern atomic_int grabit_rec_stop;
extern atomic_int grabit_rec_pause;

struct prev_sigs {
	struct sigaction sigint;
	struct sigaction sigterm;
	struct sigaction sighup;
	struct sigaction sigusr1;
	struct sigaction sigpipe;
};

void record_signals_install(struct prev_sigs *prev);
void record_signals_restore(const struct prev_sigs *prev);

double rec_capture_loop(struct grabit_wl_state *s, struct rec_layout *layout,
						struct buf_pool *pool, bool cursor,
						struct seg_ctx *sc, struct rec_controls *ctrl);

#endif
