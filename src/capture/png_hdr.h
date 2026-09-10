// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_PNG_HDR_H
#define GRABIT_CAPTURE_PNG_HDR_H

#include <stdbool.h>
#include <stdint.h>

struct image;

int grabit_save_png_hdr(const struct image *img, int32_t x, int32_t y,
						int32_t w, int32_t h, const char *path, int level);

#endif
