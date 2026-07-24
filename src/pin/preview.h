// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_PREVIEW_H
#define GRABIT_PIN_PREVIEW_H

#include <cairo/cairo.h>

int pin_preview_render_surface(cairo_surface_t *src, int target_w,
							   const char *out_path);
int pin_preview_render_png(const char *src_image_path, int target_w,
						   const char *out_path);

#endif
