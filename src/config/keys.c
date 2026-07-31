// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "region/keybinds.h"

#include <stdbool.h>
#include <string.h>

static const char *BOOL_KEYS[] = {
	"notifications",
	"log_file",
	"save_captures",
	"also_save",
	"webp.lossless",
	"recording.cursor",
	"recording.tray",
	"recording.show_dimensions",
	"sound.enabled",
	"capture.cursor",
	"region.window_snap",
	"region.confirm",
	"region.show_coords",
	"region.repeat_last",
	"services.zipline.chunked",
	"edit.default",
	"edit.instant_capture",
	"edit.start_with_tool",
	"preview.enabled",
	NULL,
};

static const char *STATE_KEYS[] = {
	"edit.color",
	"edit.width",
	"edit.tool",
	"edit.toolbar_pos",
	"region.last",
	NULL,
};

static const char *KNOWN_TOP[] = {
	"default_action",
	"notifications",
	"log_file",
	"save_captures",
	"also_save",
	"save_dir",
	"filename",
	"filename_preset",
	"service",
	"format",
	NULL,
};

static const char *KNOWN_SERVICES[] = {
	"zipline",
	"nest",
	"fakecrime",
	"ez",
	"guns",
	"pixelvault",
	NULL,
};

bool cfg_in_list(const char *needle, const char **list) {
	for (size_t i = 0; list[i]; i++) {
		if (strcmp(list[i], needle) == 0) return true;
	}
	return false;
}

bool cfg_is_bool_key(const char *key) {
	return cfg_in_list(key, BOOL_KEYS);
}

bool cfg_is_state_key(const char *key) {
	return cfg_in_list(key, STATE_KEYS);
}

bool cfg_is_known_service(const char *s) {
	return cfg_in_list(s, KNOWN_SERVICES);
}

static bool valid_top_key(const char *key) {
	return cfg_in_list(key, KNOWN_TOP);
}

static bool valid_service_key(const char *key) {
	if (strncmp(key, "services.", 9) != 0) return false;
	const char *rest = key + 9;
	const char *dot = strchr(rest, '.');
	if (!dot) return false;
	char svc[32];
	size_t svc_len = (size_t)(dot - rest);
	if (svc_len == 0 || svc_len >= sizeof svc) return false;
	memcpy(svc, rest, svc_len);
	svc[svc_len] = '\0';
	if (!cfg_is_known_service(svc)) return false;

	const char *leaf = dot + 1;
	if (strcmp(leaf, "auth") == 0) return true;
	if (strcmp(leaf, "domain") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "chunked") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "chunk_size") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "folder") == 0) return strcmp(svc, "nest") == 0;
	if (strncmp(leaf, "headers.", 8) == 0) return strcmp(svc, "zipline") == 0 && leaf[8] != '\0';
	return false;
}

static bool valid_ocr_key(const char *key) {
	if (strncmp(key, "ocr.", 4) != 0) return false;
	const char *leaf = key + 4;
	return strcmp(leaf, "tesseract") == 0 || strcmp(leaf, "lang") == 0;
}

static bool valid_edit_key(const char *key) {
	if (strncmp(key, "edit.", 5) != 0) return false;
	const char *leaf = key + 5;
	return strcmp(leaf, "color") == 0 || strcmp(leaf, "width") == 0 ||
		   strcmp(leaf, "tool") == 0 || strcmp(leaf, "default") == 0 ||
		   strcmp(leaf, "toolbar_output") == 0 ||
		   strcmp(leaf, "toolbar_pos") == 0 ||
		   strcmp(leaf, "instant_capture") == 0 ||
		   strcmp(leaf, "start_with_tool") == 0 ||
		   strcmp(leaf, "multi_select") == 0 ||
		   strcmp(leaf, "line_style") == 0;
}

static bool valid_png_key(const char *key) {
	if (strncmp(key, "png.", 4) != 0) return false;
	return strcmp(key + 4, "level") == 0;
}

static bool valid_jpeg_key(const char *key) {
	if (strncmp(key, "jpeg.", 5) != 0) return false;
	return strcmp(key + 5, "quality") == 0;
}

static bool valid_webp_key(const char *key) {
	if (strncmp(key, "webp.", 5) != 0) return false;
	const char *leaf = key + 5;
	return strcmp(leaf, "quality") == 0 || strcmp(leaf, "lossless") == 0;
}

static bool valid_sound_key(const char *key) {
	if (strncmp(key, "sound.", 6) != 0) return false;
	const char *leaf = key + 6;
	return strcmp(leaf, "enabled") == 0 || strcmp(leaf, "player") == 0 ||
		   strcmp(leaf, "file") == 0;
}

static bool valid_translate_key(const char *key) {
	if (strncmp(key, "translate.", 10) != 0) return false;
	const char *leaf = key + 10;
	return strcmp(leaf, "target") == 0 || strcmp(leaf, "backend") == 0 ||
		   strcmp(leaf, "url") == 0 || strcmp(leaf, "api_key") == 0;
}

static bool valid_text_card_key(const char *key) {
	if (strncmp(key, "text_card.", 10) != 0) return false;
	const char *leaf = key + 10;
	return strcmp(leaf, "dismiss_secs") == 0 ||
		   strcmp(leaf, "position") == 0 ||
		   strcmp(leaf, "output") == 0;
}

static bool valid_preview_key(const char *key) {
	if (strncmp(key, "preview.", 8) != 0) return false;
	const char *leaf = key + 8;
	return strcmp(leaf, "enabled") == 0 || strcmp(leaf, "size") == 0 ||
		   strcmp(leaf, "position") == 0 || strcmp(leaf, "output") == 0 ||
		   strcmp(leaf, "dismiss_secs") == 0;
}

static bool valid_capture_key(const char *key) {
	if (strncmp(key, "capture.", 8) != 0) return false;
	const char *leaf = key + 8;
	return strcmp(leaf, "backend") == 0 || strcmp(leaf, "cursor") == 0 ||
		   strcmp(leaf, "delay") == 0;
}

static bool valid_region_key(const char *key) {
	if (strncmp(key, "region.", 7) != 0) return false;
	const char *leaf = key + 7;
	return strcmp(leaf, "window_snap") == 0 || strcmp(leaf, "confirm") == 0 ||
		   strcmp(leaf, "show_coords") == 0 || strcmp(leaf, "repeat_last") == 0 ||
		   strcmp(leaf, "last") == 0;
}

static bool valid_tray_key(const char *key) {
	if (strncmp(key, "tray.", 5) != 0) return false;
	return strcmp(key + 5, "icon") == 0;
}

static bool valid_keys_key(const char *key) {
	return region_keybind_default(key) != NULL;
}

static bool valid_recording_key(const char *key) {
	if (strncmp(key, "recording.", 10) != 0) return false;
	const char *leaf = key + 10;
	if (strcmp(leaf, "format") == 0) return true;
	return strcmp(leaf, "fps") == 0 || strcmp(leaf, "crf") == 0 ||
		   strcmp(leaf, "max_size_mb") == 0 || strcmp(leaf, "cursor") == 0 ||
		   strcmp(leaf, "ffmpeg") == 0 || strcmp(leaf, "preset") == 0 ||
		   strcmp(leaf, "tune") == 0 || strcmp(leaf, "pix_fmt") == 0 ||
		   strcmp(leaf, "tray") == 0;
}

bool cfg_key_is_known(const char *key) {
	return valid_top_key(key) || valid_service_key(key) ||
		   valid_recording_key(key) || valid_ocr_key(key) ||
		   valid_sound_key(key) || valid_edit_key(key) ||
		   valid_png_key(key) || valid_jpeg_key(key) || valid_webp_key(key) ||
		   valid_capture_key(key) || valid_region_key(key) ||
		   valid_translate_key(key) || valid_text_card_key(key) ||
		   valid_preview_key(key) || valid_tray_key(key) ||
		   valid_keys_key(key);
}
