// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "region/keybinds.h"
#include "region/region.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *VALS_default_action[] = {"upload", "copy", "save", "pin", NULL};
static const char *VALS_filename_preset[] = {"date", "random", "uuid", "timestamp", NULL};
static const char *VALS_edit_color[] = {"red", "yellow", "green", "blue", "black", "white", NULL};
static const char *VALS_format[] = {"png", "jpeg", "webp", NULL};
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

static int validate_edit_color(const char *value) {
	uint32_t tmp;
	if (!grabit_parse_hex_color(value, &tmp) && !cfg_in_list(value, VALS_edit_color)) {
		log_error("edit.color must be #RRGGBB or one of red|yellow|green|blue|black|white");
		return -1;
	}
	return 0;
}

static const char *VALS_capture_backend[] = {"auto", "wlr", "ext", "kwin", NULL};

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

int config_set(struct config *c, const char *key, const char *value) {
	if (strcmp(key, "save_captures") == 0) key = "also_save";
	if (!cfg_key_is_known(key)) {
		log_error("unknown config key: `%s`", key);
		const char *hint = cfg_help_suggest_key(key);
		if (hint) log_info("did you mean: `%s`?", hint);
		return -1;
	}
	if (strcmp(key, "format") == 0 && !cfg_in_list(value, VALS_format)) {
		log_error("format must be one of png|jpeg|webp");
		return -1;
	}
	if (strcmp(key, "capture.backend") == 0 && !cfg_in_list(value, VALS_capture_backend)) {
		log_error("capture.backend must be one of auto|wlr|ext|kwin");
		return -1;
	}
	if (strcmp(key, "png.level") == 0 &&
		validate_int_in_range(key, value, 0, 9) != 0) return -1;
	if (strcmp(key, "jpeg.quality") == 0 &&
		validate_int_in_range(key, value, 1, 100) != 0) return -1;
	if (strcmp(key, "webp.quality") == 0 &&
		validate_int_in_range(key, value, 0, 100) != 0) return -1;
	if (strcmp(key, "recording.fps") == 0 &&
		validate_int_in_range(key, value, 1, 120) != 0) return -1;
	if (strcmp(key, "text_card.dismiss_secs") == 0 &&
		validate_int_in_range(key, value, 0, 600) != 0) return -1;
	if (strcmp(key, "preview.size") == 0 &&
		validate_int_in_range(key, value, 100, 800) != 0) return -1;
	if (strcmp(key, "preview.dismiss_secs") == 0 &&
		validate_int_in_range(key, value, 0, 600) != 0) return -1;
	if (strcmp(key, "preview.position") == 0 && !cfg_in_list(value, VALS_show_position)) {
		log_error("preview.position must be one of "
				  "top-left|top-center|top-right|bottom-left|bottom-center|bottom-right|center");
		return -1;
	}
	if (strcmp(key, "text_card.position") == 0 && !cfg_in_list(value, VALS_show_position)) {
		log_error("text_card.position must be one of "
				  "top-left|top-center|top-right|bottom-left|bottom-center|bottom-right|center");
		return -1;
	}
	if (strcmp(key, "recording.crf") == 0 &&
		validate_int_in_range(key, value, 0, 51) != 0) return -1;
	if (strcmp(key, "recording.max_size_mb") == 0 &&
		validate_int_in_range(key, value, 0, 100000) != 0) return -1;
	if (strcmp(key, "recording.preset") == 0 && !cfg_in_list(value, VALS_x264_preset)) {
		log_error("recording.preset must be one of "
				  "ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow");
		return -1;
	}
	if (strcmp(key, "recording.tune") == 0 && value[0] && !cfg_in_list(value, VALS_x264_tune)) {
		log_error("recording.tune must be one of "
				  "film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency");
		return -1;
	}
	if (strcmp(key, "services.zipline.chunk_size") == 0 &&
		validate_int_in_range(key, value, 1, 95) != 0) return -1;
	if (strcmp(key, "recording.format") == 0 && !cfg_in_list(value, VALS_record_format)) {
		log_error("recording.format must be one of mp4|webm|gif");
		return -1;
	}
	if (strcmp(key, "recording.pix_fmt") == 0 && !cfg_in_list(value, VALS_pix_fmt)) {
		log_error("recording.pix_fmt must be one of yuv420p|yuv422p|yuv444p|yuv420p10le");
		return -1;
	}
	if (strcmp(key, "default_action") == 0 && !cfg_in_list(value, VALS_default_action)) {
		log_error("default_action must be one of upload|copy|save|pin");
		return -1;
	}
	if (strcmp(key, "filename_preset") == 0 && !cfg_in_list(value, VALS_filename_preset)) {
		log_error("filename_preset must be one of date|random|uuid|timestamp");
		return -1;
	}
	if (strcmp(key, "service") == 0 && !upload_service_known(value)) {
		log_error("service `%s` is not a built-in or a registered sxcu uploader", value);
		log_error("  built-ins: zipline|nest|fakecrime|ez|guns|pixelvault");
		log_error("  add a custom one with: grabit sxcu add <file.sxcu>");
		return -1;
	}
	if (strcmp(key, "translate.backend") == 0 &&
		!cfg_in_list(value, VALS_translate_backend)) {
		log_error("translate.backend must be one of trans|libretranslate|deepl");
		return -1;
	}
	if (strcmp(key, "edit.color") == 0 && validate_edit_color(value) != 0) return -1;
	if (strcmp(key, "edit.tool") == 0 &&
		!cfg_in_list(value, (const char **)grabit_tool_names)) {
		log_error("edit.tool must be one of "
				  "pen|marker|line|rect|ellipse|arrow|blur|text|eraser");
		return -1;
	}
	if (strcmp(key, "edit.width") == 0 &&
		validate_int_in_range(key, value, 1, 20) != 0) return -1;
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
