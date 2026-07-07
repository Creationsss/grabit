// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "notify_test.h"

#include "config.h"
#include "notify/notify.h"
#include "paths.h"
#include "pin/pin.h"
#include "pin/preview.h"
#include "sound/sound.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cairo/cairo.h>

static int clamp_cfg_int(struct config *cfg, const char *key, int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	int n = atoi(v);
	if (n < lo) n = lo;
	if (n > hi) n = hi;
	return n;
}

static int write_sample_png(const char *path, int w, int h) {
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);
	cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, w, h);
	cairo_pattern_add_color_stop_rgb(g, 0, 1.0, 0.55, 0.32);
	cairo_pattern_add_color_stop_rgb(g, 1, 0.20, 0.28, 0.5);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, w / 9.0);
	cairo_text_extents_t e;
	cairo_text_extents(cr, "grabit test", &e);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
	cairo_move_to(cr, (w - e.width) / 2.0 - e.x_bearing, (h - e.height) / 2.0 - e.y_bearing);
	cairo_show_text(cr, "grabit test");
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_status_t st = cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);
	return st == CAIRO_STATUS_SUCCESS ? 0 : -1;
}

static void show_test_preview(struct config *cfg, const char *sample) {
	const char *en = config_get(cfg, "preview.enabled");
	if (!en || strcmp(en, "true") != 0) return;
	char *png = paths_build_output(cfg, NULL, ".png", PATHS_DEST_TEMP);
	if (!png) return;
	const char *pos = config_get(cfg, "preview.position");
	if (!pos || !pos[0]) pos = "bottom-right";
	if (pin_preview_render_png(sample, clamp_cfg_int(cfg, "preview.size", 300, 100, 800), png) == 0) {
		struct pin_show_opts opts = {
			.dismiss_secs = clamp_cfg_int(cfg, "preview.dismiss_secs", 5, 0, 600),
			.position = pos,
			.output_name = config_get(cfg, "preview.output"),
			.hover_caption = "notify test",
		};
		pin_spawn_show(cfg, png, &opts);
	}
	(void)unlink(png);
	free(png);
}

int grabit_notify_test(struct config *cfg) {
	char *sample = paths_build_output(cfg, NULL, ".png", PATHS_DEST_TEMP);
	if (!sample) return -1;
	int w = clamp_cfg_int(cfg, "preview.size", 300, 100, 800);
	int h = (int)(w * 0.625);
	if (write_sample_png(sample, w, h) != 0) {
		free(sample);
		return -1;
	}

	notify_init(cfg, false);
	notify_send(&(struct notify_opts){
		.summary = "grabit",
		.body = "notification test",
	});
	grabit_sound_play(cfg);
	show_test_preview(cfg, sample);

	(void)unlink(sample);
	free(sample);
	return 0;
}
