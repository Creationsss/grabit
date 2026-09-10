// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/tonemap.h"

#include "capture/capture.h"
#include "wl/color.h"

#include <math.h>
#include <stdint.h>

#include "color-management-v1-client-protocol.h"
#include <wayland-client.h>

#define PQ_M1 0.1593017578125
#define PQ_M2 78.84375
#define PQ_C1 0.8359375
#define PQ_C2 18.8515625
#define PQ_C3 18.6875
#define PQ_PEAK 10000.0

#define HLG_A 0.17883277
#define HLG_B 0.28466892
#define HLG_C 0.55991073
#define HLG_REF_SIGNAL 0.75

#define REF_WHITE_NITS 203.0
#define LIGHT_LUT_N 1024
#define SRGB_LUT_N 4096
#define TONE_KNEE 0.8
#define MEMO_N 8192

static const double BT2020_TO_SRGB[3][3] = {
	{1.66049100, -0.58764114, -0.07284986},
	{-0.12455047, 1.13289990, -0.00834942},
	{-0.01815076, -0.10057890, 1.11872966},
};

static double pq_eotf(double e) {
	if (e <= 0.0) return 0.0;
	double p = pow(e, 1.0 / PQ_M2);
	double num = p - PQ_C1;
	if (num < 0.0) num = 0.0;
	double den = PQ_C2 - PQ_C3 * p;
	if (den <= 0.0) return 1.0;
	return pow(num / den, 1.0 / PQ_M1);
}

static double hlg_inv_oetf(double e) {
	if (e <= 0.0) return 0.0;
	if (e <= 0.5) return e * e / 3.0;
	return (exp((e - HLG_C) / HLG_A) + HLG_B) / 12.0;
}

static double srgb_oetf(double v) {
	if (v <= 0.0031308) return 12.92 * v;
	return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

static double knee(double v) {
	if (v <= TONE_KNEE) return v;
	return TONE_KNEE + (1.0 - TONE_KNEE) * tanh((v - TONE_KNEE) / (1.0 - TONE_KNEE));
}

static double roll_off(double v, double unity) {
	return knee(v) / unity;
}

static void build_light_lut(double *lut, uint32_t tf) {
	bool pq = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
	double ref = pq ? REF_WHITE_NITS / PQ_PEAK : hlg_inv_oetf(HLG_REF_SIGNAL);
	for (int i = 0; i < LIGHT_LUT_N; i++) {
		double e = (double)i / (LIGHT_LUT_N - 1);
		lut[i] = (pq ? pq_eotf(e) : hlg_inv_oetf(e)) / ref;
	}
}

static uint8_t enc8(const uint8_t *lut, double v) {
	if (v < 0.0) v = 0.0;
	if (v > 1.0) v = 1.0;
	return lut[(int)(v * (SRGB_LUT_N - 1) + 0.5)];
}

bool grabit_tonemap_10bit(struct image *img, bool swap_rb) {
	if (!img || !img->bytes || !img->have_color) return false;
	if (!grabit_color_is_hdr(&img->color)) return false;

	double light[LIGHT_LUT_N];
	build_light_lut(light, img->color.tf_named);

	double unity = knee(1.0);

	uint8_t enc[SRGB_LUT_N];
	for (int i = 0; i < SRGB_LUT_N; i++)
		enc[i] = (uint8_t)(srgb_oetf((double)i / (SRGB_LUT_N - 1)) * 255.0 + 0.5);

	uint32_t memo_key[MEMO_N];
	uint32_t memo_val[MEMO_N];
	for (int i = 0; i < MEMO_N; i++) {
		memo_key[i] = 0u;
		memo_val[i] = 0xff000000u;
	}

	uint8_t *base = img->bytes;
	for (int32_t y = 0; y < img->height; y++) {
		uint32_t *line = (uint32_t *)(base + (size_t)y * (size_t)img->stride);
		for (int32_t x = 0; x < img->width; x++) {
			uint32_t p = line[x];
			uint32_t h = (p * 2654435761u) >> 19;
			if (memo_key[h] == p) {
				line[x] = memo_val[h];
				continue;
			}

			uint32_t f0 = (p >> 20) & 0x3ffu;
			uint32_t f1 = (p >> 10) & 0x3ffu;
			uint32_t f2 = p & 0x3ffu;
			double r = light[swap_rb ? f2 : f0];
			double g = light[f1];
			double b = light[swap_rb ? f0 : f2];

			double xr = BT2020_TO_SRGB[0][0] * r + BT2020_TO_SRGB[0][1] * g +
						BT2020_TO_SRGB[0][2] * b;
			double xg = BT2020_TO_SRGB[1][0] * r + BT2020_TO_SRGB[1][1] * g +
						BT2020_TO_SRGB[1][2] * b;
			double xb = BT2020_TO_SRGB[2][0] * r + BT2020_TO_SRGB[2][1] * g +
						BT2020_TO_SRGB[2][2] * b;

			double luma = 0.2126 * xr + 0.7152 * xg + 0.0722 * xb;
			if (luma > 0.0) {
				double s = roll_off(luma, unity) / luma;
				xr *= s;
				xg *= s;
				xb *= s;
			}

			uint32_t px = 0xff000000u | ((uint32_t)enc8(enc, xr) << 16) |
						  ((uint32_t)enc8(enc, xg) << 8) | (uint32_t)enc8(enc, xb);
			line[x] = px;
			memo_key[h] = p;
			memo_val[h] = px;
		}
	}

	img->format = WL_SHM_FORMAT_XRGB8888;
	img->have_color = false;
	return true;
}
