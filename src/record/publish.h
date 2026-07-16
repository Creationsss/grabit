// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_PUBLISH_H
#define GRABIT_RECORD_PUBLISH_H

#include <stdatomic.h>
#include <stdbool.h>

struct config;

struct publish_opts {
	const char *ffmpeg_bin;
	const char *format;
	const char *output_path;
	const char *upload_service;
	bool keep_locally;
	bool chunked;
	double secs;
	atomic_int *stop;
};

void record_publish(struct config *cfg, const struct publish_opts *po);

#endif
