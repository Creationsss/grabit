// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_COLOR_H
#define GRABIT_WL_COLOR_H

#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;

struct grabit_color_xy {
	int32_t x, y;
};

struct grabit_color_primaries {
	struct grabit_color_xy r, g, b, w;
};

struct grabit_colorimetry {
	struct grabit_color_primaries primaries;
	uint32_t primaries_named;
	bool have_primaries;

	uint32_t tf_named;
	uint32_t tf_power;

	uint32_t min_lum;
	uint32_t max_lum;
	uint32_t ref_lum;
	bool have_luminances;

	struct grabit_color_primaries target;
	uint32_t target_min_lum;
	uint32_t target_max_lum;
	bool have_target;

	uint32_t max_cll;
	uint32_t max_fall;
};

bool grabit_color_is_hdr(const struct grabit_colorimetry *c);

void grabit_color_probe_outputs(struct grabit_wl_state *s);

#endif
