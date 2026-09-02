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

int gcfg_help_example_grouped(const char *key, const char **example_out,
							  const char **def_out) {
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
		if (strcmp(leaf, "show_dimensions") == 0) {
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
			*example_out = "pen|marker|line|rect|rounded_rect|ellipse|arrow|"
						   "blur|pixelate|spotlight|text|counter|callout|eraser";
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
		if (strcmp(leaf, "line_style") == 0) {
			*example_out = "solid|dashed|dotted";
			*def_out = "solid";
			return 0;
		}
		if (strcmp(leaf, "toolbar_placement") == 0) {
			*example_out = "top|attach";
			*def_out = "top";
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
	return -1;
}
