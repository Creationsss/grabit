// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "args.h"
#include "config/internal.h"
#include "log.h"
#include "region/edit_persist.h"
#include "region/keybinds.h"
#include "region/region.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *VALS_filename_preset[] = {"date", "random", "uuid", "timestamp", NULL};
static const char *VALS_modifier[] = {"ctrl", "shift", "alt", "super", NULL};
static const char *VALS_format[] = {"png", "jpeg", "jpg", "webp", NULL};
static const char *VALS_translate_backend[] = {"trans", "libretranslate", "deepl", NULL};

static const char *VALS_x264_tune[] = {
	"film",
	"animation",
	"grain",
	"stillimage",
	"psnr",
	"ssim",
	"fastdecode",
	"zerolatency",
	NULL,
};

static const char *VALS_pix_fmt[] = {
	"yuv420p",
	"yuv422p",
	"yuv444p",
	"yuv420p10le",
	NULL,
};

static const char *VALS_record_format[] = {"mp4", "webm", "gif", NULL};

static const char *VALS_x264_preset[] = {
	"ultrafast",
	"superfast",
	"veryfast",
	"faster",
	"fast",
	"medium",
	"slow",
	"slower",
	"veryslow",
	NULL,
};

static int validate_int_in_range(const char *key, const char *value, long lo, long hi) {
	if (!*value) {
		log_error("%s must be an integer", key);
		return -1;
	}
	char *end = NULL;
	long n = strtol(value, &end, 10);
	if (!end || *end != '\0') {
		log_error("%s must be an integer", key);
		return -1;
	}
	if (n < lo || n > hi) {
		log_error("%s must be between %ld and %ld", key, lo, hi);
		return -1;
	}
	return 0;
}

static const char *VALS_capture_backend[] = {"auto", "wlr", "ext", "kwin", NULL};

static const char *VALS_toolbar_placement[] = {"top", "attach", NULL};

static const char *VALS_show_position[] = {
	"top-left",
	"top-center",
	"top-right",
	"bottom-left",
	"bottom-center",
	"bottom-right",
	"center",
	NULL,
};

struct cfg_enum_key {
	const char *key;
	const char *const *vals;
	bool allow_empty;
};

static const struct cfg_enum_key ENUM_KEYS[] = {
	{"format", VALS_format, false},
	{"capture.backend", VALS_capture_backend, false},
	{"edit.toolbar_placement", VALS_toolbar_placement, false},
	{"preview.position", VALS_show_position, false},
	{"text_card.position", VALS_show_position, false},
	{"recording.preset", VALS_x264_preset, false},
	{"recording.tune", VALS_x264_tune, true},
	{"recording.format", VALS_record_format, false},
	{"recording.pix_fmt", VALS_pix_fmt, false},
	{"filename_preset", VALS_filename_preset, false},
	{"translate.backend", VALS_translate_backend, false},
	{"edit.multi_select", VALS_modifier, false},
	{"default_action", grabit_action_names, false},
	{"edit.line_style", grabit_line_style_names, false},
	{"edit.tool", grabit_tool_names, false},
};

struct cfg_int_key {
	const char *key;
	long lo;
	long hi;
	bool allow_auto;
};

static const struct cfg_int_key INT_KEYS[] = {
	{"png.level", 0, 9, false},
	{"jpeg.quality", 1, 100, false},
	{"webp.quality", 0, 100, false},
	{"recording.fps", 1, 120, false},
	{"recording.crf", 0, 51, false},
	{"recording.max_size_mb", 0, 100000, false},
	{"text_card.dismiss_secs", 0, 600, false},
	{"preview.size", 100, 800, false},
	{"preview.dismiss_secs", 0, 600, false},
	{"services.zipline.chunk_size", 1, 95, false},
	{"capture.delay", 0, 3600, false},
	{"edit.width", EDIT_MIN_WIDTH, EDIT_MAX_WIDTH, false},
	{"region.window_radius", 0, 100, true},
	{"gui.radius", 0, 100, true},
};

const char *cfg_canonical_key(const char *key) {
	return strcmp(key, "save_captures") == 0 ? "also_save" : key;
}

int config_set(struct config *c, const char *key, const char *value) {
	key = cfg_canonical_key(key);
	if (!cfg_key_is_known(key)) {
		cfg_help_report_unknown_key(key);
		return -1;
	}
	if (strcmp(key, "save_dir") == 0 && value[0] != '/' && value[0] != '~') {
		log_error("save_dir must be an absolute path or start with ~/");
		log_error("  a relative path is resolved against whatever directory grabit "
				  "was launched from, which for a keybind is unpredictable");
		return -1;
	}
	for (size_t i = 0; i < sizeof ENUM_KEYS / sizeof *ENUM_KEYS; i++) {
		const struct cfg_enum_key *e = &ENUM_KEYS[i];
		if (strcmp(key, e->key) != 0) continue;
		if (e->allow_empty && !value[0]) break;
		if (!cfg_in_list(value, e->vals)) {
			log_error("%s must be one of %s", key, grabit_join_names(e->vals));
			return -1;
		}
		break;
	}
	for (size_t i = 0; i < sizeof INT_KEYS / sizeof *INT_KEYS; i++) {
		const struct cfg_int_key *k = &INT_KEYS[i];
		if (strcmp(key, k->key) != 0) continue;
		if (k->allow_auto && strcmp(value, "auto") == 0) break;
		if (validate_int_in_range(key, value, k->lo, k->hi) != 0) return -1;
		break;
	}
	if (strcmp(key, "service") == 0 && !upload_service_known(value)) {
		log_error("service `%s` is not a built-in (zipline|nest|fakecrime|ez|guns|"
				  "pixelvault) or a registered sxcu uploader; add one with "
				  "`grabit sxcu add <file.sxcu>`",
				  value);
		return -1;
	}
	uint32_t rgb;
	if (strcmp(key, "edit.color") == 0 && !edit_color_try(value, &rgb)) {
		log_error("edit.color must be #RRGGBB or one of %s", edit_color_names());
		return -1;
	}
	if (strcmp(key, "edit.swatches") == 0 && value[0]) {
		uint32_t tmp[EDIT_SWATCH_COUNT];
		if (!edit_swatches_parse(value, tmp)) {
			log_error("edit.swatches must be %d colors separated by commas, each "
					  "#RRGGBB or one of %s",
					  EDIT_SWATCH_COUNT, edit_color_names());
			return -1;
		}
	}
	if (strcmp(key, "region.last") == 0) {
		struct rect tmp;
		if (!last_region_parse(value, &tmp)) {
			log_error("region.last must be <x>,<y>,<w>,<h> with w,h > 0");
			return -1;
		}
	}
	if (cfg_is_bool_key(key) && strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
		log_error("%s must be true or false", key);
		return -1;
	}
	if (strncmp(key, "keys.", 5) == 0 && !region_keybind_validate(value)) {
		log_error("%s must be a comma-separated list of keys, e.g. "
				  "\"Return, Ctrl+c\" or \"Escape, mouse:right\"",
				  key);
		return -1;
	}

	const char *zl_prefix = "services.zipline.headers.";
	if (strncmp(key, zl_prefix, strlen(zl_prefix)) == 0) {
		if (gcfg_validate_zl_header(key + strlen(zl_prefix), value) != 0) return -1;
	}

	char *normalized = NULL;
	if (strcmp(key, "services.zipline.domain") == 0) {
		normalized = gcfg_normalize_zipline_domain(value);
		if (!normalized) {
			log_error("out of memory");
			return -1;
		}
		value = normalized;
	}

	int rc = cfg_kv_upsert(c, key, value);
	free(normalized);
	if (rc != 0) {
		log_error("out of memory");
		return -1;
	}
	return 0;
}
