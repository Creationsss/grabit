// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "config/internal.h"
#include "exit.h"
#include "log.h"
#include "subcmd.h"
#include "upload/sxcu.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const struct subcmd SUBCMDS[] = {
	{"add", "install", "<file> [--force]", "register a .sxcu uploader"},
	{"list", "ls", "", "show registered uploaders"},
	{"show", NULL, "<name> [--show-secrets]", "print parsed fields"},
	{"remove", "rm", "<name> [-y]", "remove an uploader"},
};
#define N_SUBCMDS (sizeof SUBCMDS / sizeof SUBCMDS[0])

static int usage(void) {
	return subcmd_usage(SUBCMDS, N_SUBCMDS, "sxcu");
}

static int help(void) {
	return subcmd_help(SUBCMDS, N_SUBCMDS, "sxcu");
}

static bool is_sub(const char *name, const char *sub) {
	return subcmd_is(SUBCMDS, N_SUBCMDS, name, sub);
}

static int do_list(void) {
	char **names = NULL;
	size_t n = 0;
	if (sxcu_dir_list(&names, &n) != 0) {
		log_error("sxcu: cannot list uploaders (no config dir?)");
		return 1;
	}
	if (n == 0) {
		log_info("no .sxcu uploaders registered in %s", sxcu_dir_path());
	} else {
		for (size_t i = 0; i < n; i++)
			puts(names[i]);
	}
	for (size_t i = 0; i < n; i++)
		free(names[i]);
	free(names);
	return 0;
}

static void show_kv(const char *label, const char *k, const char *sep,
					const char *v, bool reveal) {
	if (!reveal && cfg_key_is_secret(k))
		printf("%s%s%s<hidden>\n", label, k, sep);
	else
		printf("%s%s%s%s\n", label, k, sep, v ? v : "");
}

static int do_show(const char *name, bool reveal) {
	struct sxcu_uploader u = {0};
	if (sxcu_dir_lookup(name, &u) != 0) {
		log_error("sxcu: %s not found in %s", name, sxcu_dir_path());
		return 1;
	}
	printf("name:        %s\n", u.name ? u.name : "");
	char safe_url[512];
	grabit_redact_url(u.request_url, safe_url, sizeof safe_url);
	printf("url:         %s\n", reveal && u.request_url ? u.request_url : safe_url);
	printf("method:      %s\n", sxcu_method_str(u.method));
	printf("body:        %s\n", sxcu_body_str(u.body_type));
	if (u.file_form_name) printf("file_field:  %s\n", u.file_form_name);
	if (u.url_expr) printf("url_expr:    %s\n", u.url_expr);
	if (u.del_expr) printf("del_expr:    %s\n", u.del_expr);
	for (size_t i = 0; i < u.n_headers; i++)
		show_kv("header:      ", u.headers[i].k, ": ", u.headers[i].v, reveal);
	for (size_t i = 0; i < u.n_args; i++)
		show_kv("arg:         ", u.args[i].k, " = ", u.args[i].v, reveal);
	for (size_t i = 0; i < u.n_params; i++)
		show_kv("param:       ", u.params[i].k, " = ", u.params[i].v, reveal);
	if (!reveal) puts("(secrets hidden; --show-secrets reveals them)");
	sxcu_free(&u);
	return 0;
}

int cmd_sxcu(int argc, char **argv) {
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return help();
	}
	if (argc < 1) {
		help();
		return 2;
	}
	const char *sub = argv[0];
	if (is_sub("add", sub)) {
		bool force = false;
		const char *file = NULL;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--force") == 0)
				force = true;
			else if (!file)
				file = argv[i];
			else
				return usage();
		}
		if (!file) return usage();
		if (sxcu_dir_add(file, force) != 0) return 1;
		log_info("sxcu: added to %s", sxcu_dir_path());
		return 0;
	}
	if (is_sub("list", sub)) return do_list();
	if (is_sub("remove", sub)) {
		bool yes = false;
		const char *name = NULL;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0)
				yes = true;
			else if (!name)
				name = argv[i];
			else
				return usage();
		}
		if (!name) return usage();
		char what[256];
		snprintf(what, sizeof what, "remove the uploader %s", name);
		if (!grabit_confirm(yes, what)) return 1;
		return sxcu_dir_remove(name) == 0 ? 0 : 1;
	}
	if (is_sub("show", sub)) {
		bool reveal = false;
		const char *target = NULL;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--show-secrets") == 0)
				reveal = true;
			else if (!target)
				target = argv[i];
			else
				return usage();
		}
		if (!target) return usage();
		return do_show(target, reveal);
	}
	return usage();
}
