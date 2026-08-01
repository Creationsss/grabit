// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"
#include "config/internal.h"

#include "log.h"

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
	"save_state",
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
	"recording.show_dimensions",
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
	"edit.line_style",
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
	"capture.delay",
	"region.window_snap",
	"region.confirm",
	"region.show_coords",
	"region.repeat_last",
	"region.last",
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
	"tray.icon",
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

static bool key_namespace_exists(const char *input) {
	size_t n = strlen(input);
	if (n == 0) return false;
	for (size_t i = 0; ALL_KNOWN_KEYS[i]; i++) {
		if (strncmp(ALL_KNOWN_KEYS[i], input, n) == 0 && ALL_KNOWN_KEYS[i][n] == '.')
			return true;
	}
	return false;
}

void cfg_help_report_unknown_key(const char *key) {
	if (key_namespace_exists(key)) {
		log_error("`%s` is a group of keys, not a key itself; run `grabit set` to "
				  "list what is under it",
				  key);
		return;
	}
	const char *hint = cfg_help_suggest_key(key);
	if (hint)
		log_error("unknown config key `%s`; did you mean `%s`?", key, hint);
	else
		log_error("unknown config key `%s`", key);
}

const char *cfg_help_suggest_key(const char *input) {
	if (!input || !*input) return NULL;

	static char hdrbuf[128];
	if (strncmp(input, "services.", 9) == 0) {
		const char *dot = strchr(input + 9, '.');
		if (dot && strncmp(dot + 1, "x-", 2) == 0 &&
			strncmp(dot + 1, "headers.", 8) != 0) {
			int n = snprintf(hdrbuf, sizeof hdrbuf, "%.*s.headers.%s",
							 (int)(dot - input), input, dot + 1);
			if (n > 0 && (size_t)n < sizeof hdrbuf) return hdrbuf;
		}
	}

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
	static char keybuf[64];
	for (int a = 0; a < KA_COUNT; a++) {
		const char *k = region_keybind_action_key(a);
		if (!k) continue;
		size_t d = grabit_edit_distance(input, k);
		if (d < best_dist) {
			best_dist = d;
			best = k;
		}
	}
	for (int t = 0; t < TOOL_COUNT; t++) {
		char k[64];
		snprintf(k, sizeof k, "keys.tool.%s", grabit_tool_names[t]);
		size_t d = grabit_edit_distance(input, k);
		if (d < best_dist) {
			best_dist = d;
			snprintf(keybuf, sizeof keybuf, "%s", k);
			best = keybuf;
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

static void print_key_with_default(const char *key, const char *def,
								   const char *cur) {
	if (cur && cur[0] && def && strcmp(cur, def) != 0)
		printf("  %-28s = %s  (default: %s)\n", key, cur, def);
	else if (cur && cur[0])
		printf("  %-28s = %s\n", key, cur);
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
	"save_state",
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
	"recording.show_dimensions",
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
	"edit.line_style",
	"edit.toolbar_output",
	"edit.toolbar_pos",
	NULL,
};
static const char *const G_ENCODER[] = {"png.level", "jpeg.quality", "webp.quality",
										"webp.lossless", NULL};
static const char *const G_OCR[] = {"ocr.tesseract", "ocr.lang", NULL};
static const char *const G_CAPTURE[] = {"capture.backend", "capture.cursor",
										"capture.delay", NULL};
static const char *const G_REGION[] = {"region.window_snap", "region.confirm",
									   "region.show_coords", "region.repeat_last",
									   "region.last", NULL};
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
static const char *const G_TRAY[] = {
	"tray.icon",
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
	G_TRAY,
	NULL,
};

void cfg_help_print_all_keys(void) {
	struct config c = {0};
	if (config_exists()) (void)config_load_full(&c);
	puts("settable config keys (`=` marks one you have set; run `grabit set <key>` "
		 "for values):");
	for (size_t g = 0; KEY_GROUPS[g]; g++) {
		puts("");
		for (size_t i = 0; KEY_GROUPS[g][i]; i++) {
			const char *key = KEY_GROUPS[g][i];
			print_key_with_default(key, find_default(key), config_get(&c, key));
		}
	}
	config_free(&c);
	puts("");
	print_key_line("services.<svc>.auth", "svc: see --help, or an .sxcu name");
	print_key_line("services.zipline.domain", NULL);
	print_key_line("services.zipline.chunked", NULL);
	print_key_line("services.zipline.chunk_size", NULL);
	print_key_line("services.nest.folder", NULL);
	print_key_line("services.zipline.headers.<name>", "zipline upload metadata");
	puts("");
	print_key_line("keys.<action>", "run `grabit set keys` to list every binding");
}
