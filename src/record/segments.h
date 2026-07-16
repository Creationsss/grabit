// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_SEGMENTS_H
#define GRABIT_RECORD_SEGMENTS_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "record/ring.h"

struct grabit_wl_state;

struct seg_ctx {
	const char *ffmpeg_bin;
	const char *format;
	const char *preset;
	const char *tune;
	const char *pix_fmt;
	int w;
	int h;
	int fps;
	int crf;
	const char *final_path;
	atomic_int *stop;
	char **segs;
	size_t n_segs;
	size_t cap_segs;
	pid_t *pending;
	size_t n_pending;
	size_t cap_pending;
	pid_t pid;
	int fd;
	struct ring *ring;
	pthread_t enc;
	struct enc_state es;
	bool enc_running;
	bool failed;
};

int seg_begin(struct seg_ctx *sc);
void seg_finish(struct seg_ctx *sc, struct grabit_wl_state *s);
bool seg_any_pending_alive(struct seg_ctx *sc);
void seg_reap_all(struct seg_ctx *sc);
int seg_assemble(struct seg_ctx *sc, const char *output_path);
void seg_unlink_all(struct seg_ctx *sc);
void seg_ctx_free(struct seg_ctx *sc);

#endif
