// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config_internal.h"

#include "region/keybinds.h"
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
	{"log_file", "true|false", "true"},
	{"also_save", "true|false (alias: save_captures)", "false"},
	{"save_dir", "~/Pictures", NULL},
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
	"edit.toolbar_output",
	"edit.toolbar_pos",
	"jpeg.quality",
	"webp.quality",
	"webp.lossless",
	"ocr.tesseract",
	"ocr.lang",
	"capture.backend",
	"capture.cursor",
	"region.window_snap",
	"region.confirm",
	"keys.confirm",
	"keys.cancel",
	"keys.select_all",
	"keys.undo",
	"keys.edit_mode",
	"keys.region_mode",
	"keys.nudge_left",
	"keys.nudge_right",
	"keys.nudge_up",
	"keys.nudge_down",
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
		if (strcmp(leaf, "instant_capture") == 0) {
			*example_out = "true|false";
			*def_out = "false";
			return 0;
		}
		if (strcmp(leaf, "start_with_tool") == 0) {
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
	"edit.toolbar_output",
	"edit.toolbar_pos",
	NULL,
};
static const char *const G_ENCODER[] = {"jpeg.quality", "webp.quality", "webp.lossless", NULL};
static const char *const G_OCR[] = {"ocr.tesseract", "ocr.lang", NULL};
static const char *const G_CAPTURE[] = {"capture.backend", "capture.cursor", NULL};
static const char *const G_REGION[] = {"region.window_snap", "region.confirm", NULL};
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
