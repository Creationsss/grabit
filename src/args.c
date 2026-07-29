// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "args.h"

#include "log.h"
#include "upload/upload.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool is_silent_flag(const char *a) {
	return strcmp(a, "--silent") == 0 || strcmp(a, "--quiet") == 0 ||
		   strcmp(a, "-q") == 0;
}

static bool is_debug_flag(const char *a) {
	return strcmp(a, "-d") == 0 || strcmp(a, "--debug") == 0;
}

void args_pre_scan(int argc, char **argv, bool *silent, bool *debug) {
	for (int i = 1; i < argc; i++) {
		if (is_silent_flag(argv[i]))
			*silent = true;
		else if (is_debug_flag(argv[i]))
			*debug = true;
	}
}

static int parse_delay(const char *v, int *out) {
	char *end = NULL;
	long n = v && *v ? strtol(v, &end, 10) : 0;
	if (!v || !*v || !end || *end != '\0' || n < 0 || n > 3600) {
		log_error("--delay requires a whole number of seconds (0-3600)");
		return -1;
	}
	*out = (int)n;
	return 0;
}

static int set_action(struct args *a, enum action act, const char *flag) {
	if (a->action == act) return 0;
	if (a->action != ACTION_NONE) {
		log_error("conflicting actions: %s contradicts an earlier flag", flag);
		return -1;
	}
	a->action = act;
	return 0;
}

int args_parse(int argc, char **argv, struct args *out) {
	memset(out, 0, sizeof *out);

	bool positional_only = false;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!positional_only && strcmp(arg, "--") == 0) {
			positional_only = true;
			continue;
		}
		if (positional_only) {
			if (out->file) {
				log_error("unexpected extra argument: %s", arg);
				return -1;
			}
			out->file = arg;
			if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
			continue;
		}

		if (strcmp(arg, "-c") == 0 || strcmp(arg, "--copy") == 0) {
			if (set_action(out, ACTION_COPY, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "-u") == 0 || strcmp(arg, "--upload") == 0) {
			if (set_action(out, ACTION_UPLOAD, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "-o") == 0 ||
			strcmp(arg, "--output") == 0 ||
			strcmp(arg, "--save") == 0) {
			if (set_action(out, ACTION_OUTPUT, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--tesseract") == 0) {
			if (set_action(out, ACTION_OCR, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--record") == 0) {
			if (set_action(out, ACTION_RECORD, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--pin") == 0) {
			if (set_action(out, ACTION_PIN, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--grab") == 0) {
			if (set_action(out, ACTION_PIN_GRAB, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--release") == 0) {
			if (set_action(out, ACTION_PIN_RELEASE, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--close-all") == 0) {
			if (set_action(out, ACTION_PIN_CLOSE_ALL, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--tray") == 0) {
			if (set_action(out, ACTION_TRAY, arg) != 0) return -1;
			continue;
		}

		if (strcmp(arg, "-e") == 0 ||
			strcmp(arg, "--edit") == 0) {
			out->edit = true;
			continue;
		}
		if (strcmp(arg, "--cursor") == 0) {
			out->cursor = true;
			continue;
		}
		if (strcmp(arg, "-L") == 0 || strcmp(arg, "--last") == 0) {
			out->last_region = true;
			continue;
		}
		if (strcmp(arg, "--no-last") == 0) {
			out->no_last = true;
			continue;
		}
		if (strcmp(arg, "--delay") == 0) {
			if (++i >= argc) {
				log_error("--delay requires a number of seconds");
				return -1;
			}
			if (parse_delay(argv[i], &out->delay_secs) != 0) return -1;
			continue;
		}
		if (strncmp(arg, "--delay=", 8) == 0) {
			if (parse_delay(arg + 8, &out->delay_secs) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--chunked") == 0) {
			out->chunked = true;
			continue;
		}
		if (strcmp(arg, "-w") == 0 || strcmp(arg, "--window") == 0) {
			out->window = true;
			continue;
		}
		if (strcmp(arg, "-F") == 0 || strcmp(arg, "--fullscreen") == 0) {
			out->fullscreen = true;
			continue;
		}
		if (strncmp(arg, "--fullscreen=", 13) == 0) {
			out->fullscreen = true;
			out->fullscreen_target = arg + 13;
			if (!*out->fullscreen_target) {
				log_error("--fullscreen=<monitor> requires a number or name");
				return -1;
			}
			continue;
		}
		if (strcmp(arg, "--no-tray") == 0) {
			out->no_tray = true;
			continue;
		}
		if (strcmp(arg, "--no-upload") == 0) {
			out->no_upload = true;
			continue;
		}
		if (is_silent_flag(arg)) {
			out->silent = true;
			continue;
		}
		if (is_debug_flag(arg)) {
			continue;
		}

		if (strcmp(arg, "-f") == 0) {
			if (++i >= argc) {
				log_error("-f requires a file argument");
				return -1;
			}
			out->file = argv[i];
			if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
			continue;
		}

		if (strcmp(arg, "--filename") == 0) {
			if (++i >= argc || !argv[i][0]) {
				log_error("--filename requires a non-empty template");
				return -1;
			}
			out->filename_tpl = argv[i];
			continue;
		}
		if (strncmp(arg, "--filename=", 11) == 0) {
			out->filename_tpl = arg + 11;
			if (!*out->filename_tpl) {
				log_error("--filename requires a non-empty template");
				return -1;
			}
			continue;
		}

		if (strcmp(arg, "--translate") == 0) {
			out->translate = true;
			continue;
		}
		if (strcmp(arg, "--show") == 0) {
			out->show = true;
			continue;
		}
		if (strcmp(arg, "--no-copy") == 0) {
			out->no_copy = true;
			continue;
		}
		if (strncmp(arg, "--translate=", 12) == 0) {
			out->translate = true;
			out->translate_to = arg + 12;
			if (!*out->translate_to) {
				log_error("--translate=<lang> requires a non-empty target");
				return -1;
			}
			continue;
		}

		if (strcmp(arg, "--format") == 0) {
			if (++i >= argc) {
				log_error("--format requires a value (png|jpeg|webp)");
				return -1;
			}
			if (!argv[i][0]) {
				log_error("--format requires a non-empty value (png|jpeg|webp)");
				return -1;
			}
			out->format = argv[i];
			continue;
		}
		if (strncmp(arg, "--format=", 9) == 0) {
			out->format = arg + 9;
			if (!*out->format) {
				log_error("--format requires a non-empty value (png|jpeg|webp)");
				return -1;
			}
			continue;
		}

		if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
			const char *name = arg + 2;
			if (upload_service_known(name)) {
				if (out->service && strcmp(out->service, name) != 0) {
					log_error("conflicting services: %s vs %s", out->service, name);
					return -1;
				}
				out->service = name;
				if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
				continue;
			}
			char suggest[64];
			if (upload_suggest_service(name, suggest, sizeof suggest) == 0) {
				log_error("unknown flag `--%s` (did you mean `--%s`?)", name, suggest);
				return -1;
			}
		}

		log_error("unknown argument: %s (try `grabit --help`)", arg);
		return -1;
	}

	if (out->service && out->action != ACTION_UPLOAD &&
		out->action != ACTION_RECORD && out->action != ACTION_NONE) {
		log_error("--%s only makes sense with -u or --record", out->service);
		return -1;
	}

	bool recording = out->action == ACTION_RECORD;
	const struct {
		bool a, b;
		const char *na, *nb;
	} CLASH[] = {
		{recording, out->file, "--record", "-f"},
		{out->fullscreen, out->file, "--fullscreen", "-f"},
		{out->window, out->file, "--window", "-f"},
		{out->window, out->fullscreen, "--window", "--fullscreen"},
		{out->window, recording, "--window", "--record"},
	};
	for (size_t i = 0; i < sizeof CLASH / sizeof CLASH[0]; i++) {
		if (!CLASH[i].a || !CLASH[i].b) continue;
		log_error("%s cannot be combined with %s", CLASH[i].na, CLASH[i].nb);
		return -1;
	}

	if (out->edit) {
		bool edit_applies = out->action == ACTION_UPLOAD ||
							out->action == ACTION_COPY ||
							out->action == ACTION_OUTPUT ||
							out->action == ACTION_PIN ||
							out->action == ACTION_NONE;
		if (!edit_applies) log_debug("--edit is ignored for this action");
	}
	if (out->no_tray && out->action != ACTION_RECORD && out->action != ACTION_NONE) {
		log_debug("--no-tray only applies to --record");
	}
	if (out->file && out->format) {
		log_debug("--format is ignored when -f is used");
	}
	if (out->file && out->filename_tpl) {
		log_debug("--filename is ignored when -f is used");
	}
	if (out->translate && out->action != ACTION_OCR) {
		log_debug("--translate only applies to --tesseract");
		out->translate = false;
	}
	if (out->show && out->action != ACTION_OCR) {
		log_debug("--show only applies to --tesseract");
		out->show = false;
	}
	if (out->no_copy && out->action != ACTION_OCR) {
		log_debug("--no-copy only applies to --tesseract");
		out->no_copy = false;
	} else if (out->no_copy && !out->show) {
		log_warn("--no-copy without --show discards the OCR text");
	}

	return 0;
}
