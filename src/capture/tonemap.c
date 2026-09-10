// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/tonemap.h"

#include "capture/capture.h"
#include "log.h"
#include "wl/color.h"

#include <math.h>
#include <stdint.h>

#include "color-management-v1-client-protocol.h"

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
#define SRGB_LUT_N 4096
#define TONE_KNEE 0.8

static const double BT2020_TO_SRGB[3][3] = {
	{1.66049100, -0.58764114, -0.07284986},
	{-0.12455047, 1.13289990, -0.00834942},
	{-0.01815076, -0.10057890, 1.11872966},
};

bool grabit_tonemap_needed(const struct grabit_colorimetry *c) {
	if (!c) return false;
	return c->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
		   c->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
}

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

static double roll_off(double v) {
	if (v <= TONE_KNEE) return v;
	return TONE_KNEE + (1.0 - TONE_KNEE) * tanh((v - TONE_KNEE) / (1.0 - TONE_KNEE));
}

static void build_light_lut(double *lut, const struct grabit_colorimetry *c) {
	bool pq = c->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
	double ref = pq ? REF_WHITE_NITS / PQ_PEAK : hlg_inv_oetf(HLG_REF_SIGNAL);
	if (ref <= 0.0) ref = 1.0;
	for (int i = 0; i < 1024; i++) {
		double e = (double)i / 1023.0;
		double l = pq ? pq_eotf(e) : hlg_inv_oetf(e);
		lut[i] = l / ref;
	}
}

bool grabit_tonemap_10bit(struct image *img, bool swap_rb) {
	if (!img || !img->bytes || !img->have_color) return false;
	if (!grabit_tonemap_needed(&img->color)) return false;

	double light[1024];
	build_light_lut(light, &img->color);
	double unity = roll_off(1.0);

	uint8_t enc[SRGB_LUT_N];
	for (int i = 0; i < SRGB_LUT_N; i++) {
		double v = srgb_oetf((double)i / (SRGB_LUT_N - 1));
		int q = (int)(v * 255.0 + 0.5);
		enc[i] = (uint8_t)(q < 0 ? 0 : (q > 255 ? 255 : q));
	}

	uint8_t *base = img->bytes;
	for (int32_t y = 0; y < img->height; y++) {
		uint32_t *line = (uint32_t *)(base + (size_t)y * (size_t)img->stride);
		for (int32_t x = 0; x < img->width; x++) {
			uint32_t p = line[x];
			uint32_t f0 = (p >> 20) & 0x3ffu;
			uint32_t f1 = (p >> 10) & 0x3ffu;
			uint32_t f2 = p & 0x3ffu;
			double in[3];
			in[0] = light[swap_rb ? f2 : f0];
			in[1] = light[f1];
			in[2] = light[swap_rb ? f0 : f2];

			double out[3];
			for (int k = 0; k < 3; k++)
				out[k] = BT2020_TO_SRGB[k][0] * in[0] +
						 BT2020_TO_SRGB[k][1] * in[1] +
						 BT2020_TO_SRGB[k][2] * in[2];

			double luma = 0.2126 * out[0] + 0.7152 * out[1] + 0.0722 * out[2];
			if (luma > 0.0) {
				double scale = roll_off(luma) / (luma * unity);
				for (int k = 0; k < 3; k++)
					out[k] *= scale;
			}

			uint32_t px = 0xff000000u;
			for (int k = 0; k < 3; k++) {
				double v = out[k];
				if (v < 0.0) v = 0.0;
				if (v > 1.0) v = 1.0;
				int idx = (int)(v * (SRGB_LUT_N - 1) + 0.5);
				px |= (uint32_t)enc[idx] << (16 - 8 * k);
			}
			line[x] = px;
		}
	}
	return true;
}
