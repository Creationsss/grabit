// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "args.h"
#include "capture/freeze.h"
#include "config/config.h"
#include "exit.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "pin/pin.h"
#include "plugin/dispatch.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "sound/sound.h"
#include "tray/apptray.h"
#include "upload/upload.h"
#include "util/util.h"

#include "app/app.h"

static void on_signal(int sig) {
	gapp_unlink_tmpfile();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install_signal_handlers(void) {
	grabit_install_signal_handler(SIGINT, on_signal);
	grabit_install_signal_handler(SIGTERM, on_signal);
	grabit_install_signal_handler(SIGHUP, on_signal);
}

static int run_record(struct config *cfg, const struct args *a) {
	return record_toggle(cfg, a);
}

static int run(const struct args *a) {
	struct config cfg;
	if (config_load_full(&cfg) != 0) return 1;
	config_state_migrate(&cfg);
	notify_init(&cfg, a->silent);

	enum action eff = a->action;
	if (eff == ACTION_NONE) eff = args_default_action(config_get(&cfg, "default_action"));

	struct args eff_a = *a;
	if (!eff_a.edit && !a->file &&
		(eff == ACTION_UPLOAD || eff == ACTION_COPY ||
		 eff == ACTION_OUTPUT || eff == ACTION_PIN)) {
		const char *v = config_get(&cfg, "edit.always");
		if (v && strcmp(v, "true") == 0) eff_a.edit = true;
	}
	if (!eff_a.last_region && !eff_a.no_last) {
		const char *v = config_get(&cfg, "region.repeat_last");
		if (v && strcmp(v, "true") == 0) eff_a.last_region = true;
	}
	if (!eff_a.delay_set)
		eff_a.delay_secs = config_get_int_clamp(&cfg, "capture.delay_secs", 0, 0, 3600);
	a = &eff_a;

	int rc;
	switch (eff) {
	case ACTION_UPLOAD:
		rc = gapp_run_upload(&cfg, a);
		break;
	case ACTION_COPY:
		rc = gapp_run_copy(&cfg, a);
		break;
	case ACTION_OUTPUT:
		rc = gapp_run_output(&cfg, a);
		break;
	case ACTION_OCR:
		rc = gapp_run_ocr(&cfg, a);
		break;
	case ACTION_RECORD:
		rc = run_record(&cfg, a);
		break;
	case ACTION_PIN:
		rc = gapp_run_pin(&cfg, a);
		break;
	case ACTION_PIN_GRAB:
		rc = pin_grab();
		break;
	case ACTION_PIN_RELEASE:
		rc = pin_release();
		break;
	case ACTION_PIN_CLOSE_ALL:
		rc = pin_close_all();
		break;
	case ACTION_TRAY:
		rc = tray_app_run(&cfg);
		break;
	default:
		log_error("no action specified; try -u, -c, -o, --pin, --record, or --tesseract");
		notify_send(&(struct notify_opts){
			.summary = "No action set",
			.body = "run `grabit set default_action upload|copy|save|pin`",
		});
		rc = 1;
		break;
	}

	config_free(&cfg);
	return rc;
}

static int help_topic(const char *sub) {
	if (strcmp(sub, "filename") == 0) return gapp_print_help_filename();
	if (strcmp(sub, "env") == 0) return gapp_print_help_env();
	if (strcmp(sub, "examples") == 0) return gapp_print_help_examples();
	if (strcmp(sub, "ocr") == 0 || strcmp(sub, "tesseract") == 0)
		return gapp_print_help_ocr();
	static char *help_argv[] = {(char *)"--help", NULL};
	if (strcmp(sub, "set") == 0) return cmd_set(1, help_argv);
	if (strcmp(sub, "get") == 0) return cmd_get(1, help_argv);
	if (strcmp(sub, "unset") == 0) return cmd_unset(1, help_argv);
	if (strcmp(sub, "sxcu") == 0) return cmd_sxcu(1, help_argv);
	if (strcmp(sub, "plugin") == 0) return cmd_plugin(1, help_argv);
	log_error("no help topic for `%s`", sub);
	gapp_print_help_topics();
	return GRABIT_EXIT_USAGE;
}

int main(int argc, char **argv) {
	bool pre_silent = false, pre_debug = false;
	args_pre_scan(argc, argv, &pre_silent, &pre_debug);
	log_init(pre_silent, pre_debug);
	install_signal_handlers();

	if (argc >= 2) {
		const char *first = argv[1];
		if (strcmp(first, "--version") == 0 || strcmp(first, "-V") == 0) return gapp_print_version();
		if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0)
			return argc < 3 ? gapp_print_help() : help_topic(argv[2]);
		if (strcmp(first, "help") == 0)
			return argc < 3 ? gapp_print_help_topics() : help_topic(argv[2]);
		if (strcmp(first, "set") == 0) return cmd_set(argc - 2, argv + 2);
		if (strcmp(first, "get") == 0) return cmd_get(argc - 2, argv + 2);
		if (strcmp(first, "unset") == 0) return cmd_unset(argc - 2, argv + 2);
		if (strcmp(first, "sxcu") == 0) return cmd_sxcu(argc - 2, argv + 2);
		if (strcmp(first, "plugin") == 0) return cmd_plugin(argc - 2, argv + 2);
		if (strcmp(first, "-p") == 0) {
			if (argc < 3) {
				log_error("usage: grabit <plugin> --pin [args]");
				return GRABIT_EXIT_USAGE;
			}
			log_warn("-p is deprecated; use `grabit %s --pin`", argv[2]);
			return plugin_dispatch_pin(argv[2], argc - 2, argv + 2, false);
		}
		if (first[0] != '-') {
			bool pin = false;
			for (int i = 2; i < argc && strcmp(argv[i], "--") != 0; i++)
				if (strcmp(argv[i], "--pin") == 0) pin = true;
			int prc = pin ? plugin_dispatch_pin(first, argc - 1, argv + 1, true)
						  : gapp_try_dispatch_plugin(first, argc - 1, argv + 1);
			if (prc >= 0) return prc;
		}
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0) break;
		if (args_is_version_flag(argv[i])) return gapp_print_version();
		if (args_is_help_flag(argv[i])) return gapp_print_help();
	}

	struct args a;
	if (args_parse(argc, argv, &a) != 0) return GRABIT_EXIT_USAGE;
	return run(&a);
}
