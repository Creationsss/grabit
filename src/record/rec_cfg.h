// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_REC_CFG_H
#define GRABIT_RECORD_REC_CFG_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static inline int rec_cfg_int(struct config *cfg, const char *key,
							  int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	long n = strtol(v, NULL, 10);
	if (n < lo) return def;
	if (n > hi) return hi;
	return (int)n;
}

static inline int rec_cfg_fps(struct config *cfg) {
	return rec_cfg_int(cfg, "recording.fps", 30, 1, 120);
}

static inline int rec_cfg_crf(struct config *cfg) {
	return rec_cfg_int(cfg, "recording.crf", 23, 0, 51);
}

static inline bool rec_cfg_cursor(struct config *cfg) {
	const char *v = config_get(cfg, "recording.cursor");
	return !v || strcmp(v, "true") == 0;
}

static inline const char *rec_cfg_format(struct config *cfg) {
	const char *v = config_get(cfg, "recording.format");
	if (v && (strcmp(v, "webm") == 0 || strcmp(v, "gif") == 0)) return v;
	return "mp4";
}

static inline const char *rec_cfg_ffmpeg(struct config *cfg) {
	const char *v = config_get(cfg, "recording.ffmpeg");
	return (v && v[0]) ? v : "ffmpeg";
}

static inline const char *rec_cfg_preset(struct config *cfg) {
	const char *v = config_get(cfg, "recording.preset");
	return (v && v[0]) ? v : "fast";
}

static inline const char *rec_cfg_tune(struct config *cfg) {
	const char *v = config_get(cfg, "recording.tune");
	return (v && v[0]) ? v : NULL;
}

static inline const char *rec_cfg_pix_fmt(struct config *cfg) {
	const char *v = config_get(cfg, "recording.pix_fmt");
	return (v && v[0]) ? v : "yuv420p";
}

#endif
