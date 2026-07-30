// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_PW_H
#define GRABIT_RECORD_PW_H

#include <stdbool.h>
#include <stdint.h>

struct buf_pool;
struct ring;
struct pw_capture;

bool pw_available(void);

struct pw_capture *pw_capture_open(uint32_t node_id, int fps);
void pw_capture_size(const struct pw_capture *c, int32_t *w, int32_t *h,
					 int32_t *stride);
void pw_capture_bind(struct pw_capture *c, struct buf_pool *pool, struct ring *ring);
void pw_capture_set_paused(struct pw_capture *c, bool paused);
bool pw_capture_failed(const struct pw_capture *c);
void pw_capture_close(struct pw_capture *c);

#endif
