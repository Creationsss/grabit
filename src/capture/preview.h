// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_PREVIEW_H
#define GRABIT_CAPTURE_PREVIEW_H

#include <cairo/cairo.h>

int capture_preview_surface(cairo_surface_t *src, int target_w,
							const char *out_path);
int capture_preview_png(const char *src_image_path, int target_w,
						const char *out_path);

#endif
