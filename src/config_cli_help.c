// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config_internal.h"

#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct example {
	const char *key;
	const char *example;
	const char *def;
};

static const struct example TOP_EXAMPLES[] = {
	{"default_action", "upload|copy|save|pin", "copy"},
	{"notifications", "true|false", "true"},
	{"also_save", "true|false (alias: save_captures)", "false"},
	{"save_dir", "~/Pictures", NULL},
	{"editor", "satty | swappy | gimp | krita | kolourpaint", NULL},
	{"filename", "%Y-%m-%d-%H-%M-%S", NULL},
	{"filename_preset", "date|random|uuid|timestamp", "date"},
	{"service", "zipline|nest|fakecrime|ez|guns|pixelvault", NULL},
	{"format", "png|jpeg|webp", "png"},
};
static const size_t TOP_EXAMPLES_N = sizeof TOP_EXAMPLES / sizeof TOP_EXAMPLES[0];

static const char *zl_header_example(const struct zl_hdr *h) {
	static char buf[160];
	switch (h->kind) {
	case ZL_FREE:
		if (strcmp(h->name, "x-zipline-deletes-at") == 0) return "1d";
		if (strcmp(h->name, "x-zipline-domain") == 0) return "cdn1.example.com,cdn2.example.com";
		if (strcmp(h->name, "x-zipline-file-extension") == 0) return ".png";
		if (strcmp(h->name, "x-zipline-folder") == 0) return "<folder-id>";
		if (strcmp(h->name, "x-zipline-filename") == 0) return "<override>";
		return "<string>";
	case ZL_ENUM: {
		size_t off = 0;
		buf[0] = '\0';
		for (size_t i = 0; h->allowed[i]; i++) {
			int n = snprintf(buf + off, sizeof buf - off, "%s%s", i ? "|" : "", h->allowed[i]);
			if (n < 0 || (size_t)n >= sizeof buf - off) break;
			off += (size_t)n;
		}
		return buf;
	}
	case ZL_INT:
		return "<integer>";
	case ZL_INT_PCT:
		return "0-100";
	}
	return "";
}

static const char *const ALL_KNOWN_KEYS[] = {
	"default_action",
	"notifications",
	"also_save",
	"save_dir",
	"editor",
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
	"edit.toolbar_output",
	"edit.toolbar_pos",
	"jpeg.quality",
	"webp.quality",
	"webp.lossless",
	"ocr.tesseract",
	"capture.backend",
	"capture.cursor",
	"region.window_snap",
	"region.confirm",
	"translate.target",
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

int cfg_help_example_for_key(const char *key, const char **example_out, const char **def_out) {
	*def_out = NULL;
	for (size_t i = 0; i < TOP_EXAMPLES_N; i++) {
		if (strcmp(TOP_EXAMPLES[i].key, key) == 0) {
			*example_out = TOP_EXAMPLES[i].example;
			*def_out = TOP_EXAMPLES[i].def;
			return 0;
		}
	}
	if (strncmp(key, "services.", 9) == 0) {
		const char *rest = key + 9;
		const char *dot = strchr(rest, '.');
		if (!dot) return -1;
		const char *leaf = dot + 1;
		if (strcmp(leaf, "auth") == 0) {
			*example_out = "<api-token>";
			return 0;
		}
		if (strcmp(leaf, "domain") == 0) {
			*example_out = "https://<host>/api/upload";
			return 0;
		}
		if (strcmp(leaf, "folder") == 0) {
			*example_out = "<folder-uuid>";
			return 0;
		}
		if (strncmp(leaf, "headers.", 8) == 0) {
			const struct zl_hdr *h = gcfg_zl_find(leaf + 8);
			if (h) {
				*example_out = zl_header_example(h);
				return 0;
			}
		}
	}
	if (strncmp(key, "recording.", 10) == 0) {
		const char *leaf = key + 10;
		if (strcmp(leaf, "fps") == 0) {
			*example_out = "1-120";
			*def_out = "30";
			return 0;
		}
		if (strcmp(leaf, "crf") == 0) {
			*example_out = "0-51";
			*def_out = "23";
			return 0;
		}
		if (strcmp(leaf, "format") == 0) {
			*example_out = "mp4|webm|gif";
			*def_out = "mp4";
			return 0;
		}
		if (strcmp(leaf, "max_size_mb") == 0) {
			*example_out = "100 (0 to disable)";
			return 0;
		}
		if (strcmp(leaf, "cursor") == 0) {
			*example_out = "true|false";
			*def_out = "true";
			return 0;
		}
		if (strcmp(leaf, "ffmpeg") == 0) {
			*example_out = "ffmpeg | /usr/bin/ffmpeg";
			*def_out = "ffmpeg";
			return 0;
		}
		if (strcmp(leaf, "preset") == 0) {
			*example_out = "ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow";
			*def_out = "fast";
			return 0;
		}
		if (strcmp(leaf, "tune") == 0) {
			*example_out = "film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency (empty to disable)";
			return 0;
		}
		if (strcmp(leaf, "pix_fmt") == 0) {
			*example_out = "yuv420p|yuv422p|yuv444p|yuv420p10le";
			*def_out = "yuv420p";
			return 0;
		}
	}
	if (strncmp(key, "edit.", 5) == 0) {
		const char *leaf = key + 5;
		if (strcmp(leaf, "color") == 0) {
			*example_out = "#RRGGBB or red|yellow|green|blue|black|white";
			*def_out = "#FF3030";
			return 0;
		}
		if (strcmp(leaf, "width") == 0) {
			*example_out = "1..20";
			*def_out = "4";
			return 0;
		}
		if (strcmp(leaf, "tool") == 0) {
			*example_out = "pen|marker|line|rect|ellipse|arrow|blur|text|eraser";
			*def_out = "pen";
			return 0;
		}
		if (strcmp(leaf, "default") == 0) {
			*example_out = "true|false";
			*def_out = "false";
			return 0;
		}
		if (strcmp(leaf, "toolbar_output") == 0) {
			*example_out = "<output name, e.g. DP-1; empty = primary>";
			return 0;
		}
		if (strcmp(leaf, "toolbar_pos") == 0) {
			*example_out = "<output>:<x>,<y> (written automatically when the toolbar is dragged)";
			return 0;
		}
	}
	if (strncmp(key, "sound.", 6) == 0) {
		const char *leaf = key + 6;
		if (strcmp(leaf, "enabled") == 0) {
			*example_out = "true|false";
			*def_out = "false";
			return 0;
		}
		if (strcmp(leaf, "player") == 0) {
			*example_out = "pw-play | paplay | play | aplay | <abs path>";
			return 0;
		}
		if (strcmp(leaf, "file") == 0) {
			*example_out = "<path to .oga/.wav file>";
			return 0;
		}
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
	}
	if (strcmp(key, "capture.backend") == 0) {
		*example_out = "auto|wlr|ext";
		*def_out = "auto";
		return 0;
	}
	if (strcmp(key, "capture.cursor") == 0) {
		*example_out = "true|false";
		*def_out = "true";
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
	return -1;
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

void cfg_help_print_all_keys(void) {
	puts("keys (run `grabit set <key>` for example values):");
	puts("");
	for (size_t i = 0; i < TOP_EXAMPLES_N; i++) {
		print_key_with_default(TOP_EXAMPLES[i].key, TOP_EXAMPLES[i].def);
		if (strcmp(TOP_EXAMPLES[i].key, "also_save") == 0)
			puts("    (legacy alias: save_captures)");
	}
	puts("");
	puts("  services.<svc>.auth     (svc: zipline|nest|fakecrime|ez|guns|pixelvault)");
	puts("  services.zipline.domain");
	puts("  services.zipline.chunked");
	puts("  services.zipline.chunk_size");
	puts("  services.nest.folder");
	puts("");
	puts("  services.zipline.headers.<name>:");
	for (size_t i = 0; i < gcfg_zl_headers_n; i++) {
		printf("    %s\n", gcfg_zl_headers[i].name);
	}
	puts("");
	static const char *const RECORDING_KEYS[] = {
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
	for (size_t i = 0; RECORDING_KEYS[i]; i++) {
		print_key_with_default(RECORDING_KEYS[i], find_default(RECORDING_KEYS[i]));
	}
	puts("");
	static const char *const SOUND_KEYS[] = {
		"sound.enabled",
		"sound.player",
		"sound.file",
		NULL,
	};
	for (size_t i = 0; SOUND_KEYS[i]; i++) {
		print_key_with_default(SOUND_KEYS[i], find_default(SOUND_KEYS[i]));
	}
	puts("");
	print_key_with_default("edit.color", find_default("edit.color"));
	print_key_with_default("edit.width", find_default("edit.width"));
	print_key_with_default("edit.tool", find_default("edit.tool"));
	print_key_with_default("edit.default", find_default("edit.default"));
	print_key_with_default("edit.toolbar_output", find_default("edit.toolbar_output"));
	print_key_with_default("edit.toolbar_pos", find_default("edit.toolbar_pos"));
	puts("");
	static const char *const ENCODER_KEYS[] = {
		"jpeg.quality",
		"webp.quality",
		"webp.lossless",
		NULL,
	};
	for (size_t i = 0; ENCODER_KEYS[i]; i++) {
		print_key_with_default(ENCODER_KEYS[i], find_default(ENCODER_KEYS[i]));
	}
	puts("");
	print_key_with_default("ocr.tesseract", find_default("ocr.tesseract"));
	puts("");
	print_key_with_default("capture.backend", find_default("capture.backend"));
	print_key_with_default("capture.cursor", find_default("capture.cursor"));
	print_key_with_default("region.window_snap", find_default("region.window_snap"));
	print_key_with_default("region.confirm", find_default("region.confirm"));
	print_key_with_default("translate.target", find_default("translate.target"));
	print_key_with_default("text_card.dismiss_secs", find_default("text_card.dismiss_secs"));
	print_key_with_default("text_card.position", find_default("text_card.position"));
	print_key_with_default("text_card.output", find_default("text_card.output"));
	puts("");
	print_key_with_default("preview.enabled", find_default("preview.enabled"));
	print_key_with_default("preview.size", find_default("preview.size"));
	print_key_with_default("preview.position", find_default("preview.position"));
	print_key_with_default("preview.output", find_default("preview.output"));
	print_key_with_default("preview.dismiss_secs", find_default("preview.dismiss_secs"));
}
