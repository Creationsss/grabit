// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/capture.h"
#include "capture/freeze.h"
#include "capture/region_plan.h"
#include "capture/save.h"
#include "clipboard/clipboard.h"
#include "config/config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "ocr/ocr.h"
#include "paths.h"
#include "pin/pin.h"
#include "pin/preview.h"
#include "pin/text_card.h"
#include "plugin/dispatch.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "region/region.h"
#include "sound/sound.h"
#include "upload/upload.h"
#include "util/util.h"
#include "wl/wl.h"
#include "wm/wm.h"

#include "app/app.h"

static char g_tmpfile_path[4096] = {0};
static char *g_preview_png;

static void register_tmpfile(const char *path) {
	if (!path) return;
	size_t n = strlen(path);
	if (n >= sizeof g_tmpfile_path) n = sizeof g_tmpfile_path - 1;
	memcpy(g_tmpfile_path, path, n);
	g_tmpfile_path[n] = 0;
}

void gapp_unlink_tmpfile(void) {
	if (g_tmpfile_path[0]) unlink(g_tmpfile_path);
}

void gapp_clear_tmpfile(void) {
	g_tmpfile_path[0] = 0;
}

int gapp_read_int_cfg_clamp(struct config *cfg, const char *key,
							int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	char *end = NULL;
	long n = strtol(v, &end, 10);
	if (!end || *end != '\0') return def;
	if (n < lo) return lo;
	if (n > hi) return hi;
	return (int)n;
}

static bool preview_enabled(struct config *cfg) {
	const char *en = config_get(cfg, "preview.enabled");
	return en && strcmp(en, "true") == 0;
}

static int preview_width(struct config *cfg) {
	return gapp_read_int_cfg_clamp(cfg, "preview.size", 300, 100, 800);
}

void gapp_maybe_show_preview(struct config *cfg, const char *image_path,
							 const char *caption, const char *click_open) {
	if (!image_path) return;
	if (!preview_enabled(cfg)) return;

	bool prerendered = g_preview_png && access(g_preview_png, R_OK) == 0;
	char fallback_name[64];
	snprintf(fallback_name, sizeof fallback_name, "preview-%d.png", (int)getpid());
	char *png_path = prerendered ? g_preview_png : paths_temp_file(fallback_name);
	if (!png_path) return;

	int width = preview_width(cfg);
	const char *pos = config_get(cfg, "preview.position");
	if (!pos || !pos[0]) pos = "bottom-right";

	if (prerendered || pin_preview_render_png(image_path, width, png_path) == 0) {
		struct pin_show_opts opts = {
			.dismiss_secs = gapp_read_int_cfg_clamp(cfg, "preview.dismiss_secs", 5, 0, 600),
			.position = pos,
			.output_name = config_get(cfg, "preview.output"),
			.hover_caption = caption,
			.click_open = click_open,
		};
		pin_spawn_show(cfg, png_path, &opts);
	}
	(void)unlink(png_path);
	if (png_path == g_preview_png) {
		free(g_preview_png);
		g_preview_png = NULL;
	} else {
		free(png_path);
	}
}

int gapp_resolve_save_opts(const struct args *a, struct config *cfg,
						   struct grabit_save_opts *out) {
	*out = (struct grabit_save_opts){0};
	const char *fmt_name = a->format;
	if (!fmt_name) fmt_name = config_get(cfg, "format");
	if (!fmt_name) fmt_name = "png";
	if (grabit_format_from_name(fmt_name, &out->format) != 0) {
		log_error("unknown format `%s` (expected png|jpeg|webp)", fmt_name);
		return -1;
	}
	out->png_level = gapp_read_int_cfg_clamp(cfg, "png.level", 1, 0, 9);
	if (preview_enabled(cfg)) {
		free(g_preview_png);
		char name[64];
		snprintf(name, sizeof name, "preview-%d.png", (int)getpid());
		g_preview_png = paths_temp_file(name);
		if (g_preview_png) (void)unlink(g_preview_png);
		out->preview_path = g_preview_png;
		out->preview_width = preview_width(cfg);
	}
	out->jpeg_quality = gapp_read_int_cfg_clamp(cfg, "jpeg.quality", 90, 1, 100);
	out->webp_quality = gapp_read_int_cfg_clamp(cfg, "webp.quality", 85, 0, 100);
	const char *wl = config_get(cfg, "webp.lossless");
	out->webp_lossless = wl && strcmp(wl, "true") == 0;
	return 0;
}

static int capture_wm_window(struct config *cfg, bool cursor,
							 const struct grabit_save_opts *opts, const char *path) {
	if (opts->format == GRABIT_FMT_PNG && !opts->preview_path)
		return grabit_wm_capture_active_window(cursor, path);

	char *tmp = paths_build_output(cfg, "grabit-window-%s-%r", ".png",
								   PATHS_DEST_TEMP);
	if (!tmp) return -1;

	int rc = -1;
	if (grabit_wm_capture_active_window(cursor, tmp) == 0) {
		cairo_surface_t *img = grabit_load_png_surface(tmp, "window capture");
		if (img) {
			rc = grabit_save_surface(img, opts, path);
			cairo_surface_destroy(img);
		}
	}

	(void)unlink(tmp);
	free(tmp);
	return rc;
}

static char *discard_capture(char *path, const char *summary) {
	unlink(path);
	gapp_clear_tmpfile();
	free(path);
	if (summary)
		notify_send(&(struct notify_opts){.summary = summary, .force = true});
	return NULL;
}

static char *build_capture_path(const struct args *a, struct config *cfg,
								enum action eff, bool *is_temp,
								const struct grabit_save_opts *opts) {
	bool save;
	if (eff == ACTION_OUTPUT) {
		save = true;
	} else if (eff == ACTION_OCR) {
		save = false;
	} else {
		save = config_also_save(cfg);
	}
	*is_temp = !save;
	enum paths_dest dest = save ? PATHS_DEST_PICTURES : PATHS_DEST_TEMP;
	const char *ext = grabit_format_extension(opts->format);
	return paths_build_output(cfg, a->filename_tpl, ext, dest);
}

char *gapp_capture_to_file(const struct args *a, struct config *cfg,
						   enum action eff, bool *is_temp,
						   struct rect *out_rect) {
	*is_temp = false;
	struct grabit_save_opts opts;
	if (gapp_resolve_save_opts(a, cfg, &opts) != 0) return NULL;

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "could not connect to wayland compositor",
			.force = true,
		});
		return NULL;
	}
	if (!grabit_wl_require_capture(&s)) {
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "this compositor has no screen-capture protocol",
			.force = true,
		});
		return NULL;
	}

	struct rect forced_rect;
	const struct rect *forced = NULL;
	struct region_plan_req plan_req = {
		.fullscreen = a->fullscreen,
		.fullscreen_target = a->fullscreen_target,
		.window = a->window,
		.use_last = a->last_region,
	};
	enum region_plan plan = region_plan_resolve(&s, cfg, &plan_req, &forced_rect);
	if (plan == REGION_PLAN_NO_MONITOR) {
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "grabit: fullscreen failed",
			.body = "no matching monitor",
			.force = true,
		});
		return NULL;
	}
	if (plan == REGION_PLAN_FIXED) forced = &forced_rect;

	char *path = build_capture_path(a, cfg, eff, is_temp, &opts);
	if (!path) {
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "grabit: capture failed",
			.body = "could not build output path",
			.force = true,
		});
		return NULL;
	}

	if (*is_temp) register_tmpfile(path);

	const char *cursor_cfg = config_get(cfg, "capture.cursor");
	bool cursor = a->cursor || !cursor_cfg || strcmp(cursor_cfg, "false") != 0;
	grabit_sleep_secs(a->delay_secs);

	if (plan == REGION_PLAN_NO_WINDOW) {
		grabit_wl_finish(&s);
		if (capture_wm_window(cfg, cursor, &opts, path) != 0) {
			log_error("--window: %s cannot capture the active window",
					  grabit_wm_current_name());
			return discard_capture(path, "grabit: window capture failed");
		}
		if (a->edit)
			log_warn("--window: %s rendered the window itself, so it was captured "
					 "without the editor",
					 grabit_wm_current_name());
		log_debug("captured window to %s", path);
		return path;
	}

	struct snap_window *mon_snaps = NULL;
	size_t n_snaps = 0;
	if (plan == REGION_PLAN_MONITOR_PICK) {
		struct rect *mon_rects = NULL;
		size_t n_mon = 0;
		grabit_wl_monitor_rects(&s, &mon_rects, &n_mon);
		if (mon_rects && n_mon > 0) {
			mon_snaps = calloc(n_mon, sizeof *mon_snaps);
			if (mon_snaps) {
				for (size_t i = 0; i < n_mon; i++) {
					mon_snaps[i].rect = mon_rects[i];
					mon_snaps[i].radius = 0;
				}
				n_snaps = n_mon;
			}
		}
		free(mon_rects);
	}

	int32_t forced_radius = -1;
	int32_t forced_border = 0;
	if (a->window) {
		const char *v = config_get(cfg, "region.window_radius");
		if (v && strcmp(v, "auto") != 0) {
			long r = strtol(v, NULL, 10);
			if (r >= 0) forced_radius = (int32_t)r;
		} else {
			forced_radius = grabit_wm_active_window_radius();
		}
		forced_border = grabit_wm_active_window_border_size();
	}

	uint32_t edit_color = edit_color_from_str(config_get(cfg, "edit.color"));
	int32_t edit_width = edit_width_from_str(config_get(cfg, "edit.width"));
	int32_t edit_tool = edit_tool_from_str(config_get(cfg, "edit.tool"));
	bool edit_dirty = false;

	struct rect got = {0};
	int rc = grabit_freeze_capture(&s, cfg, path, &opts, &got, a->edit, cursor,
								   a->edit ? &edit_color : NULL,
								   a->edit ? &edit_width : NULL,
								   a->edit ? &edit_tool : NULL,
								   a->edit ? &edit_dirty : NULL, forced, mon_snaps, n_snaps,
								   forced_radius, forced_border);
	grabit_wl_finish(&s);
	free(mon_snaps);
	if (rc == 0 && out_rect) *out_rect = got;

	struct edit_choices ec = {edit_color, edit_width, edit_tool};
	persist_capture_state(cfg, (a->edit && edit_dirty) ? &ec : NULL,
						  (rc == 0 && !a->fullscreen) ? &got : NULL);

	if (rc != 0)
		return discard_capture(path, rc == GRABIT_CAPTURE_CANCELLED
										 ? NULL
										 : "grabit: capture failed");

	log_debug("captured to %s", path);
	return path;
}

char *gapp_acquire_source(const struct args *a, struct config *cfg,
						  enum action eff, bool *is_temp,
						  struct rect *out_rect) {
	if (a->file) {
		char *path = strdup(a->file);
		if (!path) log_error("out of memory");
		return path;
	}
	return gapp_capture_to_file(a, cfg, eff, is_temp, out_rect);
}

void gapp_release_source(char *path, bool is_temp) {
	if (is_temp) {
		unlink(path);
		gapp_clear_tmpfile();
	}
	free(path);
}
