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
static const char *VALS_format[] = {"png", "jpeg", "webp", NULL};
static const struct config_alias ALIAS_format[] = {{"jpg", "jpeg"}, {NULL, NULL}};
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
	const struct config_alias *aliases;
};

static const struct cfg_enum_key ENUM_KEYS[] = {
	{"format", VALS_format, false, ALIAS_format},
	{"capture.backend", VALS_capture_backend, false, NULL},
	{"edit.toolbar_placement", VALS_toolbar_placement, false, NULL},
	{"preview.position", VALS_show_position, false, NULL},
	{"text_card.position", VALS_show_position, false, NULL},
	{"recording.preset", VALS_x264_preset, false, NULL},
	{"recording.tune", VALS_x264_tune, true, NULL},
	{"recording.format", VALS_record_format, false, NULL},
	{"recording.pix_fmt", VALS_pix_fmt, false, NULL},
	{"filename_preset", VALS_filename_preset, false, NULL},
	{"translate.backend", VALS_translate_backend, false, NULL},
	{"edit.multi_select", VALS_modifier, false, NULL},
	{"default_action", grabit_action_names, false, NULL},
	{"edit.line_style", grabit_line_style_names, false, NULL},
	{"edit.tool", grabit_tool_names, false, NULL},
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
	{"capture.delay_secs", 0, 3600, false},
	{"edit.width", EDIT_MIN_WIDTH, EDIT_MAX_WIDTH, false},
	{"region.window_radius", 0, 100, true},
	{"gui.radius", 0, 100, true},
};

const char *cfg_canonical_key(const char *key) {
	static const struct {
		const char *old;
		const char *now;
	} ALIASES[] = {
		{"save_captures", "also_save"},
		{"log_file", "log.enabled"},
		{"capture.delay", "capture.delay_secs"},
		{"edit.default", "edit.always"},
	};
	for (size_t i = 0; i < sizeof ALIASES / sizeof *ALIASES; i++)
		if (strcmp(key, ALIASES[i].old) == 0) return ALIASES[i].now;
	return key;
}

static const char *const OUTPUT_KEYS[] = {"preview.output", "text_card.output",
										  "edit.toolbar_output", NULL};

static const char *TRAY_ICONS[] = {
	"camera-photo",
	"camera-photo-symbolic",
	"applets-screenshooter",
	"accessories-screenshot",
	"gnome-screenshot",
	"org.gnome.Screenshot",
	"spectacle",
	"flameshot",
	"ksnip",
	"deepin-screenshot",
	"screenshot",
	"camera",
	"camera-web",
	"media-record",
	"applications-graphics",
	NULL,
};

static void classify(const char *key, struct config_key_info *out) {
	*out = (struct config_key_info){.kind = CFG_KIND_TEXT};
	if (cfg_is_bool_key(key)) {
		out->kind = CFG_KIND_BOOL;
		return;
	}
	if (strncmp(key, "keys.", 5) == 0) {
		out->kind = CFG_KIND_KEYBIND;
		return;
	}
	bool uploaders = strcmp(key, "service") == 0;
	if (uploaders || cfg_in_list(key, OUTPUT_KEYS)) {
		out->kind = CFG_KIND_ENUM;
		out->allow_empty = true;
		out->open_values = true;
		out->values = uploaders ? cfg_builtin_services() : NULL;
		out->values_src = uploaders ? CFG_VALUES_UPLOADERS : CFG_VALUES_OUTPUTS;
		return;
	}
	if (strcmp(key, "tray.icon") == 0) {
		out->kind = CFG_KIND_ENUM;
		out->open_values = true;
		out->values = TRAY_ICONS;
		out->values_src = CFG_VALUES_ICONS;
		return;
	}
	for (size_t i = 0; i < sizeof ENUM_KEYS / sizeof *ENUM_KEYS; i++) {
		if (strcmp(key, ENUM_KEYS[i].key) != 0) continue;
		out->kind = CFG_KIND_ENUM;
		out->values = ENUM_KEYS[i].vals;
		out->aliases = ENUM_KEYS[i].aliases;
		out->allow_empty = ENUM_KEYS[i].allow_empty;
		return;
	}
	for (size_t i = 0; i < sizeof INT_KEYS / sizeof *INT_KEYS; i++) {
		if (strcmp(key, INT_KEYS[i].key) != 0) continue;
		out->kind = CFG_KIND_INT;
		out->lo = INT_KEYS[i].lo;
		out->hi = INT_KEYS[i].hi;
		out->allow_auto = INT_KEYS[i].allow_auto;
		return;
	}
}

static int validate_kind(const char *key, const char *value,
						 const struct config_key_info *info) {
	switch (info->kind) {
	case CFG_KIND_TEXT:
		return 0;
	case CFG_KIND_BOOL:
		if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) return 0;
		log_error("%s must be true or false", key);
		return -1;
	case CFG_KIND_KEYBIND:
		if (region_keybind_validate(value)) return 0;
		log_error("%s must be a comma-separated list of keys, e.g. "
				  "\"Return, Ctrl+c\" or \"Escape, mouse:right\"",
				  key);
		return -1;
	case CFG_KIND_ENUM:
		if (info->open_values || (info->allow_empty && !value[0])) return 0;
		if (cfg_in_list(value, info->values)) return 0;
		if (config_alias_target(info, value) != value) return 0;
		log_error("%s must be one of %s", key, grabit_join_names(info->values));
		return -1;
	case CFG_KIND_INT:
		if (info->allow_auto && strcmp(value, "auto") == 0) return 0;
		return validate_int_in_range(key, value, info->lo, info->hi);
	}
	return 0;
}

int cfg_validate_value(const char *key, const char *value) {
	if (strcmp(key, "save_dir") == 0 && value[0] != '/' && value[0] != '~') {
		log_error("save_dir must be an absolute path or start with ~/");
		log_error("  a relative path is resolved against whatever directory grabit "
				  "was launched from, which for a keybind is unpredictable");
		return -1;
	}
	struct config_key_info info;
	classify(key, &info);
	if (validate_kind(key, value, &info) != 0) return -1;
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

	const char *zl_prefix = "services.zipline.headers.";
	if (strncmp(key, zl_prefix, strlen(zl_prefix)) == 0) {
		if (gcfg_validate_zl_header(key + strlen(zl_prefix), value) != 0) return -1;
	}

	return 0;
}

const char *config_alias_target(const struct config_key_info *info, const char *value) {
	for (const struct config_alias *a = info->aliases; a && a->from; a++)
		if (strcmp(a->from, value) == 0) return a->to;
	return value;
}

int config_key_info(const char *key, struct config_key_info *out) {
	key = cfg_canonical_key(key);
	if (!cfg_key_is_known(key)) return -1;
	classify(key, out);
	const char *ex = NULL, *def = NULL;
	if (cfg_help_example_for_key(key, &ex, &def) == 0) {
		out->example = ex;
		out->def = def;
	}
	out->secret = cfg_key_is_secret(key);
	out->state = cfg_is_state_key(key);
	out->internal = cfg_is_internal_key(key);
	return 0;
}

int config_set(struct config *c, const char *key, const char *value) {
	key = cfg_canonical_key(key);
	if (!cfg_key_is_known(key)) {
		cfg_help_report_unknown_key(key);
		return -1;
	}
	if (cfg_validate_value(key, value) != 0) return -1;

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
