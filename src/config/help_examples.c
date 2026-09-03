// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"

#include "config/internal.h"
#include "log.h"
#include "region/keybinds.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct example {
	const char *key;
	const char *example;
	const char *def;
};

static const struct example TOP_EXAMPLES[] = {
	{"default_action", "upload|copy|save|pin", "copy"},
	{"notifications", "true|false", "true"},
	{"log_file", "true|false", "true"},
	{"also_save", "true|false", "false"},
	{"save_captures", "true|false (use also_save)", "false"},
	{"save_state", "true|false", "true"},
	{"save_dir", "~/Pictures", NULL},
	{"filename", "%Y-%m-%d-%H-%M-%S", NULL},
	{"filename_preset", "date|random|uuid|timestamp", "date"},
	{"service", "zipline|nest|fakecrime|ez|guns|pixelvault", NULL},
	{"format", "png|jpeg|webp", "png"},
};
static const size_t TOP_EXAMPLES_N = sizeof TOP_EXAMPLES / sizeof TOP_EXAMPLES[0];

int cfg_help_example_for_key(const char *key, const char **example_out, const char **def_out) {
	*def_out = NULL;
	for (size_t i = 0; i < TOP_EXAMPLES_N; i++) {
		if (strcmp(TOP_EXAMPLES[i].key, key) == 0) {
			*example_out = TOP_EXAMPLES[i].example;
			*def_out = TOP_EXAMPLES[i].def;
			return 0;
		}
	}
	const char *kb_def = region_keybind_default(key);
	if (kb_def) {
		*example_out = kb_def;
		*def_out = kb_def;
		return 0;
	}
	if (gcfg_help_example_grouped(key, example_out, def_out) == 0) return 0;
	if (strcmp(key, "png.level") == 0) {
		*example_out = "0-9";
		*def_out = "1";
		return 0;
	}
	if (strcmp(key, "jpeg.quality") == 0) {
		*example_out = "1..100";
		*def_out = "90";
		return 0;
	}
	if (strcmp(key, "webp.quality") == 0) {
		*example_out = "0..100";
		*def_out = "85";
		return 0;
	}
	if (strcmp(key, "webp.lossless") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strncmp(key, "ocr.", 4) == 0) {
		const char *leaf = key + 4;
		if (strcmp(leaf, "tesseract") == 0) {
			*example_out = "tesseract | /usr/local/bin/tesseract";
			*def_out = "tesseract";
			return 0;
		}
		if (strcmp(leaf, "lang") == 0) {
			*example_out = "eng | deu | jpn | eng+deu";
			*def_out = "eng";
			return 0;
		}
	}
	if (strcmp(key, "capture.backend") == 0) {
		*example_out = "auto|wlr|ext|kwin";
		*def_out = "auto";
		return 0;
	}
	if (strcmp(key, "capture.cursor") == 0) {
		*example_out = "true|false";
		*def_out = "true";
		return 0;
	}
	if (strcmp(key, "gui.radius") == 0) {
		*example_out = "auto|0..100";
		*def_out = "0";
		return 0;
	}
	if (strcmp(key, "region.snap_animation") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "region.window_radius") == 0) {
		*example_out = "auto|0..100";
		*def_out = "auto";
		return 0;
	}
	if (strcmp(key, "region.window_snap") == 0) {
		*example_out = "true|false";
		*def_out = "true";
		return 0;
	}
	if (strcmp(key, "region.confirm") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "region.show_coords") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "capture.delay") == 0) {
		*example_out = "0..3600 (seconds to wait before capturing)";
		*def_out = "0";
		return 0;
	}
	if (strcmp(key, "region.repeat_last") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "region.last") == 0) {
		*example_out = "<x>,<y>,<w>,<h> (written automatically after each region capture)";
		return 0;
	}
	if (strcmp(key, "edit.multi_select") == 0) {
		*example_out = "ctrl|shift|alt|super";
		*def_out = "ctrl";
		return 0;
	}
	if (strcmp(key, "edit.line_style") == 0) {
		*example_out = "solid|dashed|dotted";
		*def_out = "solid";
		return 0;
	}
	if (strcmp(key, "services.zipline.chunked") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "services.zipline.chunk_size") == 0) {
		*example_out = "1-95 (MiB per chunk)";
		*def_out = "25";
		return 0;
	}
	if (strcmp(key, "translate.target") == 0) {
		*example_out = "<iso-639-1 code, e.g. en|ja|de|es>";
		*def_out = "en";
		return 0;
	}
	if (strcmp(key, "translate.backend") == 0) {
		*example_out = "trans|libretranslate|deepl";
		*def_out = "trans";
		return 0;
	}
	if (strcmp(key, "translate.url") == 0) {
		*example_out = "http://localhost:5000";
		return 0;
	}
	if (strcmp(key, "translate.api_key") == 0) {
		*example_out = "<libretranslate api key, if the server needs one>";
		return 0;
	}
	if (strcmp(key, "text_card.dismiss_secs") == 0) {
		*example_out = "0-600 (0 = stay until replaced)";
		*def_out = "8";
		return 0;
	}
	if (strcmp(key, "text_card.position") == 0) {
		*example_out = "top-left|top-center|top-right|bottom-left|bottom-center|bottom-right|center";
		*def_out = "top-right";
		return 0;
	}
	if (strcmp(key, "text_card.output") == 0) {
		*example_out = "<output name, e.g. DP-1, HDMI-A-1; empty = primary>";
		return 0;
	}
	if (strcmp(key, "preview.enabled") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strcmp(key, "preview.size") == 0) {
		*example_out = "100-800";
		*def_out = "300";
		return 0;
	}
	if (strcmp(key, "preview.position") == 0) {
		*example_out = "top-left|top-center|top-right|bottom-left|bottom-center|bottom-right|center";
		*def_out = "bottom-right";
		return 0;
	}
	if (strcmp(key, "preview.output") == 0) {
		*example_out = "<output name, e.g. DP-1; empty = primary>";
		return 0;
	}
	if (strcmp(key, "preview.dismiss_secs") == 0) {
		*example_out = "0-600 (0 = stay until next capture)";
		*def_out = "5";
		return 0;
	}
	if (strcmp(key, "tray.icon") == 0) {
		*example_out = "<icon name from your icon theme>";
		*def_out = "camera-photo";
		return 0;
	}
	return -1;
}
