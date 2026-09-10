// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_TONEMAP_H
#define GRABIT_CAPTURE_TONEMAP_H

#include <stdbool.h>

struct image;

bool grabit_tonemap_10bit(struct image *img, bool swap_rb);

#endif
