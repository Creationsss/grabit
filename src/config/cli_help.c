// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/internal.h"

#include "region/keybinds.h"
#include "util/util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *const ALL_KNOWN_KEYS[] = {
	"default_action",
	"notifications",
	"log_file",
	"also_save",
	"save_dir",
	"filename",
	"filename_preset",
	"service",
	"format",
	"recording.fps",
	"recording.format",
	"recording.crf",
	"recording.preset",
	"recording.tune",
	"recording.pix_fmt",
	"recording.max_size_mb",
	"recording.cursor",
	"recording.ffmpeg",
	"sound.enabled",
	"sound.player",
	"sound.file",
	"edit.color",
	"edit.width",
	"edit.tool",
	"edit.default",
	"edit.instant_capture",
	"edit.start_with_tool",
	"edit.multi_select",
	"edit.toolbar_output",
	"edit.toolbar_pos",
	"png.level",
	"jpeg.quality",
	"webp.quality",
	"webp.lossless",
	"ocr.tesseract",
	"ocr.lang",
	"capture.backend",
	"capture.cursor",
	"region.window_snap",
	"region.confirm",
	"region.show_coords",
	"keys.confirm",
	"keys.cancel",
	"keys.select_all",
	"keys.undo",
	"keys.redo",
	"keys.delete",
	"keys.edit_mode",
	"keys.region_mode",
	"keys.nudge_left",
	"keys.nudge_right",
	"keys.nudge_up",
	"keys.nudge_down",
	"keys.magnifier",
	"keys.tool.pen",
	"keys.tool.marker",
	"keys.tool.line",
	"keys.tool.rect",
	"keys.tool.ellipse",
	"keys.tool.arrow",
	"keys.tool.blur",
	"keys.tool.text",
	"keys.tool.eraser",
	"translate.target",
	"translate.backend",
	"translate.url",
	"translate.api_key",
	"text_card.dismiss_secs",
	"text_card.position",
	"text_card.output",
	"preview.enabled",
	"preview.size",
	"preview.position",
	"preview.output",
	"preview.dismiss_secs",
	"services.zipline.auth",
	"services.zipline.domain",
	"services.zipline.chunked",
	"services.zipline.chunk_size",
	"services.nest.auth",
	"services.nest.folder",
	"services.fakecrime.auth",
	"services.ez.auth",
	"services.guns.auth",
	"services.pixelvault.auth",
	NULL,
};

const char *cfg_help_suggest_key(const char *input) {
	if (!input || !*input) return NULL;
	size_t in_len = strlen(input);
	const char *best = NULL;
	size_t best_dist = (size_t)-1;
	for (size_t i = 0; ALL_KNOWN_KEYS[i]; i++) {
		const char *k = ALL_KNOWN_KEYS[i];
		size_t d = grabit_edit_distance(input, k);
		if (d < best_dist) {
			best_dist = d;
			best = k;
		}
	}
	if (!best) return NULL;
	size_t max_allowed = in_len / 3 + 1;
	if (max_allowed < 2) max_allowed = 2;
	return best_dist <= max_allowed ? best : NULL;
}

bool cfg_help_print_example(const char *example, const char *def) {
	if (!def) {
		printf("%s", example);
		return false;
	}
	size_t deflen = strlen(def);
	const char *p = example;
	bool starred = false;
	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		if (!starred && len == deflen && strncmp(p, def, len) == 0) {
			printf("%.*s*", (int)len, p);
			starred = true;
		} else {
			printf("%.*s", (int)len, p);
		}
		if (!bar) break;
		printf("|");
		p = bar + 1;
	}
	return starred;
}

static void print_key_line(const char *key, const char *note) {
	if (note)
		printf("  %-28s (%s)\n", key, note);
	else
		printf("  %s\n", key);
}

static void print_key_with_default(const char *key, const char *def) {
	if (def)
		printf("  %-28s default: %s\n", key, def);
	else
		printf("  %s\n", key);
}

static const char *find_default(const char *key) {
	const char *ex = NULL, *def = NULL;
	if (cfg_help_example_for_key(key, &ex, &def) == 0) return def;
	return NULL;
}

static const char *const G_TOP[] = {
	"default_action",
	"notifications",
	"log_file",
	"also_save",
	"save_dir",
	"filename",
	"filename_preset",
	"service",
	"format",
	NULL,
};
static const char *const G_RECORDING[] = {
	"recording.fps",
	"recording.format",
	"recording.crf",
	"recording.preset",
	"recording.tune",
	"recording.pix_fmt",
	"recording.max_size_mb",
	"recording.cursor",
	"recording.ffmpeg",
	NULL,
};
static const char *const G_SOUND[] = {"sound.enabled", "sound.player", "sound.file", NULL};
static const char *const G_EDIT[] = {
	"edit.color",
	"edit.width",
	"edit.tool",
	"edit.default",
	"edit.instant_capture",
	"edit.start_with_tool",
	"edit.multi_select",
	"edit.toolbar_output",
	"edit.toolbar_pos",
	NULL,
};
static const char *const G_ENCODER[] = {"png.level", "jpeg.quality", "webp.quality",
										"webp.lossless", NULL};
static const char *const G_OCR[] = {"ocr.tesseract", "ocr.lang", NULL};
static const char *const G_CAPTURE[] = {"capture.backend", "capture.cursor", NULL};
static const char *const G_REGION[] = {"region.window_snap", "region.confirm",
									   "region.show_coords", NULL};
static const char *const G_TRANSLATE[] = {
	"translate.target",
	"translate.backend",
	"translate.url",
	"translate.api_key",
	NULL,
};
static const char *const G_TEXT_CARD[] = {
	"text_card.dismiss_secs",
	"text_card.position",
	"text_card.output",
	NULL,
};
static const char *const G_PREVIEW[] = {
	"preview.enabled",
	"preview.size",
	"preview.position",
	"preview.output",
	"preview.dismiss_secs",
	NULL,
};

static const char *const *const KEY_GROUPS[] = {
	G_TOP,
	G_RECORDING,
	G_SOUND,
	G_EDIT,
	G_ENCODER,
	G_OCR,
	G_CAPTURE,
	G_REGION,
	G_TRANSLATE,
	G_TEXT_CARD,
	G_PREVIEW,
	NULL,
};

void cfg_help_print_all_keys(void) {
	puts("settable config keys (run `grabit set <key>` for values and defaults):");
	for (size_t g = 0; KEY_GROUPS[g]; g++) {
		puts("");
		for (size_t i = 0; KEY_GROUPS[g][i]; i++)
			print_key_with_default(KEY_GROUPS[g][i], find_default(KEY_GROUPS[g][i]));
	}
	puts("");
	print_key_line("services.<svc>.auth", "svc: zipline|nest|fakecrime|ez|guns|pixelvault");
	print_key_line("services.zipline.domain", NULL);
	print_key_line("services.zipline.chunked", NULL);
	print_key_line("services.zipline.chunk_size", NULL);
	print_key_line("services.nest.folder", NULL);
	print_key_line("services.zipline.headers.<name>", "zipline upload metadata");
	puts("");
	print_key_line("keys.<action>", "run `grabit set keys` to list every binding");
	puts("");
	puts("`also_save` is also accepted as `save_captures`.");
}
