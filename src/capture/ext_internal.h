// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_EXT_INTERNAL_H
#define GRABIT_CAPTURE_EXT_INTERNAL_H

#include "capture/pixels.h"

#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;
struct grabit_output;
struct ext_image_capture_source_v1;
struct ext_image_copy_capture_session_v1;

struct ec_session {
	struct ext_image_capture_source_v1 *source;
	struct ext_image_copy_capture_session_v1 *session;

	int32_t width;
	int32_t height;
	int32_t stride;
	struct pixels_fmt_pick fmt;
	bool have_size;
	bool done;
	bool overlay_cursor;

	int status;
	int *frame_status;
};

struct ec_session *gext_session_get(struct grabit_wl_state *s, struct grabit_output *o,
									bool overlay_cursor, struct pixels_pool *pool);
void gext_session_destroy(struct ec_session *es);

#endif
