// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_COLOR_H
#define GRABIT_WL_COLOR_H

#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;

#define GRABIT_CICP_UNKNOWN 0
#define GRABIT_CICP_PRI_BT709 1
#define GRABIT_CICP_PRI_BT2020 9
#define GRABIT_CICP_PRI_P3 12
#define GRABIT_CICP_TF_BT709 1
#define GRABIT_CICP_TF_GAMMA22 4
#define GRABIT_CICP_TF_LINEAR 8
#define GRABIT_CICP_TF_SRGB 13
#define GRABIT_CICP_TF_PQ 16
#define GRABIT_CICP_TF_HLG 18

struct grabit_color_xy {
	int32_t x, y;
};

struct grabit_color_primaries {
	struct grabit_color_xy r, g, b, w;
};

struct grabit_colorimetry {
	struct grabit_color_primaries primaries;
	int cicp_primaries;
	bool have_primaries;

	int cicp_transfer;
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
