// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "log.h"
#include "upload/sxcu.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int usage(void) {
	log_error("usage: grabit sxcu <add|list|remove|show> [args]");
	return 2;
}

static int help(void) {
	puts("usage: grabit sxcu <subcommand> [args]");
	puts("");
	puts("  add <file>     register a .sxcu uploader (alias: install)");
	puts("  list           show registered uploaders (alias: ls)");
	puts("  show <name>    print parsed fields (--show-secrets unmasks auth)");
	puts("  remove <name>  remove an uploader (alias: rm)");
	return 0;
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

static bool contains_ci(const char *hay, const char *needle) {
	size_t n = strlen(needle);
	for (; *hay; hay++)
		if (strncasecmp(hay, needle, n) == 0) return true;
	return false;
}

static bool key_is_secret(const char *k) {
	static const char *const NEEDLES[] = {"auth", "secret", "token", "key",
										  "password", "passwd", "session",
										  "cookie", "bearer", NULL};
	if (!k) return false;
	for (size_t i = 0; NEEDLES[i]; i++)
		if (contains_ci(k, NEEDLES[i])) return true;
	return false;
}

static void show_kv(const char *label, const char *k, const char *sep,
					const char *v, bool reveal) {
	if (!reveal && key_is_secret(k))
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
	if (argc < 1) return usage();
	const char *sub = argv[0];
	if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0) {
		return help();
	}
	if (strcmp(sub, "add") == 0 || strcmp(sub, "install") == 0) {
		if (argc != 2) return usage();
		if (sxcu_dir_add(argv[1]) != 0) return 1;
		log_info("sxcu: added to %s", sxcu_dir_path());
		return 0;
	}
	if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) return do_list();
	if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0) {
		if (argc != 2) return usage();
		return sxcu_dir_remove(argv[1]) == 0 ? 0 : 1;
	}
	if (strcmp(sub, "show") == 0) {
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
