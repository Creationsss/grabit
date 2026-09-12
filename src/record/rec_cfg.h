// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_REC_CFG_H
#define GRABIT_RECORD_REC_CFG_H

#include <stdbool.h>
#include <string.h>

#include "config/config.h"

static inline bool rec_cfg_cursor(struct config *cfg) {
	const char *v = config_get(cfg, "recording.cursor");
	return !v || strcmp(v, "true") == 0;
}

static inline bool rec_cfg_tray(struct config *cfg) {
	const char *v = config_get(cfg, "recording.tray");
	return !v || strcmp(v, "true") == 0;
}

static inline bool rec_cfg_show_dimensions(struct config *cfg) {
	const char *v = config_get(cfg, "recording.show_dimensions");
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
