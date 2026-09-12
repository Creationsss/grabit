// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_COLOR_PICKER_INTERNAL_H
#define GRABIT_REGION_COLOR_PICKER_INTERNAL_H

#define COLOR_PICKER_W 220
#define COLOR_PICKER_GRID_H 130
#define COLOR_PICKER_INPUT_H 26
#define COLOR_PICKER_INPUT_GAP 5
#define COLOR_PICKER_INPUT_BTN_GAP 4
#define COLOR_PICKER_PAD 6
#define COLOR_PICKER_GAP 8
#define COLOR_PICKER_SWATCH_PAD_X 5
#define COLOR_PICKER_SWATCH_PAD_Y 4
#define COLOR_PICKER_TEXT_GAP 8

void gcp_hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b);

#endif
