// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "wl/color.h"

#include "log.h"
#include "wl/wl.h"

#include <unistd.h>

#include "color-management-v1-client-protocol.h"

static const char *tf_name(uint32_t tf) {
	switch (tf) {
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886:
		return "bt1886";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_GAMMA22:
		return "gamma2.2";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_LINEAR:
		return "linear";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB:
		return "srgb";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_EXT_SRGB:
		return "ext-srgb";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ:
		return "st2084-pq";
	case WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG:
		return "hlg";
	default:
		return NULL;
	}
}

static const char *primaries_name(uint32_t p) {
	switch (p) {
	case WP_COLOR_MANAGER_V1_PRIMARIES_SRGB:
		return "srgb";
	case WP_COLOR_MANAGER_V1_PRIMARIES_BT2020:
		return "bt2020";
	case WP_COLOR_MANAGER_V1_PRIMARIES_DISPLAY_P3:
		return "display-p3";
	case WP_COLOR_MANAGER_V1_PRIMARIES_ADOBE_RGB:
		return "adobe-rgb";
	default:
		return NULL;
	}
}

static bool is_hdr(const struct grabit_colorimetry *c) {
	return c->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
		   c->tf_named == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
}

struct probe {
	struct grabit_colorimetry info;
	bool done;
	bool failed;
};

static void set_primaries(struct grabit_color_primaries *p, int32_t rx, int32_t ry,
						  int32_t gx, int32_t gy, int32_t bx, int32_t by,
						  int32_t wx, int32_t wy) {
	p->r = (struct grabit_color_xy){rx, ry};
	p->g = (struct grabit_color_xy){gx, gy};
	p->b = (struct grabit_color_xy){bx, by};
	p->w = (struct grabit_color_xy){wx, wy};
}

static void info_primaries(void *data, struct wp_image_description_info_v1 *i,
						   int32_t rx, int32_t ry, int32_t gx, int32_t gy,
						   int32_t bx, int32_t by, int32_t wx, int32_t wy) {
	(void)i;
	struct probe *p = data;
	set_primaries(&p->info.primaries, rx, ry, gx, gy, bx, by, wx, wy);
	p->info.have_primaries = true;
}

static void info_primaries_named(void *data, struct wp_image_description_info_v1 *i,
								 uint32_t primaries) {
	(void)i;
	struct probe *p = data;
	p->info.primaries_named = primaries;
}

static void info_tf_power(void *data, struct wp_image_description_info_v1 *i,
						  uint32_t eexp) {
	(void)i;
	struct probe *p = data;
	p->info.tf_power = eexp;
}

static void info_tf_named(void *data, struct wp_image_description_info_v1 *i,
						  uint32_t tf) {
	(void)i;
	struct probe *p = data;
	p->info.tf_named = tf;
}

static void info_luminances(void *data, struct wp_image_description_info_v1 *i,
							uint32_t min_lum, uint32_t max_lum, uint32_t ref_lum) {
	(void)i;
	struct probe *p = data;
	p->info.min_lum = min_lum;
	p->info.max_lum = max_lum;
	p->info.ref_lum = ref_lum;
	p->info.have_luminances = true;
}

static void info_target_primaries(void *data, struct wp_image_description_info_v1 *i,
								  int32_t rx, int32_t ry, int32_t gx, int32_t gy,
								  int32_t bx, int32_t by, int32_t wx, int32_t wy) {
	(void)i;
	struct probe *p = data;
	set_primaries(&p->info.target, rx, ry, gx, gy, bx, by, wx, wy);
	p->info.have_target = true;
}

static void info_target_luminance(void *data, struct wp_image_description_info_v1 *i,
								  uint32_t min_lum, uint32_t max_lum) {
	(void)i;
	struct probe *p = data;
	p->info.target_min_lum = min_lum;
	p->info.target_max_lum = max_lum;
}

static void info_target_max_cll(void *data, struct wp_image_description_info_v1 *i,
								uint32_t max_cll) {
	(void)i;
	struct probe *p = data;
	p->info.max_cll = max_cll;
}

static void info_target_max_fall(void *data, struct wp_image_description_info_v1 *i,
								 uint32_t max_fall) {
	(void)i;
	struct probe *p = data;
	p->info.max_fall = max_fall;
}

static void info_icc_file(void *data, struct wp_image_description_info_v1 *i,
						  int32_t fd, uint32_t size) {
	(void)data;
	(void)i;
	(void)size;
	if (fd >= 0) close(fd);
}

static void info_done(void *data, struct wp_image_description_info_v1 *i) {
	(void)i;
	struct probe *p = data;
	p->done = true;
}

static const struct wp_image_description_info_v1_listener info_listener = {
	.done = info_done,
	.icc_file = info_icc_file,
	.primaries = info_primaries,
	.primaries_named = info_primaries_named,
	.tf_power = info_tf_power,
	.tf_named = info_tf_named,
	.luminances = info_luminances,
	.target_primaries = info_target_primaries,
	.target_luminance = info_target_luminance,
	.target_max_cll = info_target_max_cll,
	.target_max_fall = info_target_max_fall,
};

static void desc_failed(void *data, struct wp_image_description_v1 *d,
						uint32_t cause, const char *msg) {
	(void)d;
	(void)cause;
	struct probe *p = data;
	p->failed = true;
	log_debug("color: image description failed: %s", msg ? msg : "(no reason)");
}

static void desc_ready(void *data, struct wp_image_description_v1 *d,
					   uint32_t identity) {
	(void)data;
	(void)d;
	(void)identity;
}

static void desc_ready2(void *data, struct wp_image_description_v1 *d,
						uint32_t identity_hi, uint32_t identity_lo) {
	(void)data;
	(void)d;
	(void)identity_hi;
	(void)identity_lo;
}

static const struct wp_image_description_v1_listener desc_listener = {
	.failed = desc_failed,
	.ready = desc_ready,
	.ready2 = desc_ready2,
};

static void log_output_color(const struct grabit_output *o) {
	const struct grabit_colorimetry *c = &o->color;
	const char *pri = primaries_name(c->primaries_named);
	const char *tf = tf_name(c->tf_named);
	log_debug("color: %s primaries=%s(%u) white=%d,%d transfer=%s(%u) power=%u "
			  "luminance=%u..%u ref=%u target=%u..%u max_cll=%u max_fall=%u%s",
			  o->name ? o->name : "?", pri ? pri : "?", c->primaries_named,
			  c->primaries.w.x, c->primaries.w.y, tf ? tf : "?", c->tf_named,
			  c->tf_power, c->min_lum, c->max_lum, c->ref_lum, c->target_min_lum,
			  c->target_max_lum, c->max_cll, c->max_fall,
			  is_hdr(c) ? " [hdr]" : "");
}

static void probe_output(struct grabit_wl_state *s, struct grabit_output *o) {
	struct wp_color_management_output_v1 *cmo =
		wp_color_manager_v1_get_output(s->color_manager, o->wl_output);
	if (!cmo) return;

	struct wp_image_description_v1 *desc =
		wp_color_management_output_v1_get_image_description(cmo);
	if (!desc) {
		wp_color_management_output_v1_destroy(cmo);
		return;
	}

	struct probe p = {0};
	wp_image_description_v1_add_listener(desc, &desc_listener, &p);
	if (wl_display_roundtrip(s->display) < 0 || p.failed) goto out;

	struct wp_image_description_info_v1 *info =
		wp_image_description_v1_get_information(desc);
	if (!info) goto out;
	wp_image_description_info_v1_add_listener(info, &info_listener, &p);
	if (wl_display_roundtrip(s->display) < 0) goto out;

	if (p.done) {
		o->color = p.info;
		o->have_color = true;
		log_output_color(o);
	}

out:
	wp_image_description_v1_destroy(desc);
	wp_color_management_output_v1_destroy(cmo);
}

void grabit_color_probe_outputs(struct grabit_wl_state *s) {
	if (!s->color_manager) {
		log_debug("color: compositor does not support color-management-v1");
		return;
	}
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (!s->outputs[i]->dead) probe_output(s, s->outputs[i]);
	}
}
