// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "history/history.h"

#include "clipboard/clipboard.h"
#include "config/config.h"
#include "log.h"
#include "subcmd.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const struct subcmd SUBCMDS[] = {
	{"list", "ls", "[--ocr] [--json]", "uploads and saved files, newest first (1 = newest)"},
	{"copy", NULL, "<n|id> [--ocr]", "copy the link, file path or OCR text again"},
	{"open", NULL, "<n|id>", "open the link or file"},
	{"delete", NULL, "<n|id> [-y] [--host-only|--file-only]",
	 "delete from the host and the saved file, then forget it"},
	{"remove", "rm", "<n|id> [--ocr]", "forget an entry, leaving the upload and file alone"},
	{"clear", NULL, "[--ocr|--all] [-y]", "forget every entry"},
};
#define N_SUBCMDS (sizeof SUBCMDS / sizeof SUBCMDS[0])

struct opts {
	const char *ref;
	bool ocr;
	bool all;
	bool json;
	bool yes;
	unsigned what;
};

static int usage(void) {
	return subcmd_usage(SUBCMDS, N_SUBCMDS, "history");
}

static int parse(int argc, char **argv, struct opts *o) {
	*o = (struct opts){.what = HISTORY_DELETE_BOTH};
	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "--ocr") == 0)
			o->ocr = true;
		else if (strcmp(a, "--all") == 0)
			o->all = true;
		else if (strcmp(a, "--json") == 0)
			o->json = true;
		else if (strcmp(a, "-y") == 0 || strcmp(a, "--yes") == 0)
			o->yes = true;
		else if (strcmp(a, "--host-only") == 0)
			o->what = HISTORY_DELETE_HOST;
		else if (strcmp(a, "--file-only") == 0)
			o->what = HISTORY_DELETE_FILE;
		else if (a[0] != '-' && !o->ref)
			o->ref = a;
		else
			return -1;
	}
	return 0;
}

static unsigned mask_for(const struct opts *o) {
	return o->all ? HISTORY_ALL : o->ocr ? HISTORY_OCR
										 : HISTORY_MEDIA;
}

static void when(int64_t t, char *out, size_t n) {
	time_t tt = (time_t)t;
	struct tm tm;
	if (!localtime_r(&tt, &tm) || !strftime(out, n, "%Y-%m-%d %H:%M", &tm)) snprintf(out, n, "?");
}

static void first_line(const char *text, char *out, size_t n) {
	size_t len = strcspn(text ? text : "", "\n");
	if (len >= n) len = n - 1;
	memcpy(out, text ? text : "", len);
	out[len] = '\0';
}

static int do_list(const struct opts *o) {
	struct history_list l;
	if (history_load(mask_for(o), &l) != 0) return 1;
	if (o->json) {
		for (size_t i = 0; i < l.n; i++) {
			char *line = history_entry_json(&l.v[i]);
			if (line) puts(line);
			free(line);
		}
	} else if (l.n == 0) {
		log_info("history is empty (%s)", history_file());
	}
	for (size_t i = 0; !o->json && i < l.n; i++) {
		const struct history_entry *e = &l.v[i];
		char t[32], line[96];
		when(e->time, t, sizeof t);
		if (e->kind == HISTORY_OCR) {
			first_line(e->text, line, sizeof line);
			printf("%3zu  %s  ocr     %-10s  %s\n", i + 1, t, e->target ? e->target : e->lang ? e->lang
																							  : "",
				   line);
		} else if (e->kind == HISTORY_UPLOAD) {
			printf("%3zu  %s  upload  %-10s  %s%s%s%s\n", i + 1, t, e->service ? e->service : "",
				   e->url ? e->url : "", e->path ? "  (" : "", e->path ? e->path : "", e->path ? ")" : "");
		} else {
			printf("%3zu  %s  %-6s  %-10s  %s\n", i + 1, t, e->video ? "video" : "file", "",
				   e->path ? e->path : "");
		}
	}
	history_list_free(&l);
	return 0;
}

static const struct history_entry *resolve(const struct opts *o, struct history_list *l) {
	if (!o->ref) {
		usage();
		return NULL;
	}
	bool numeric = strspn(o->ref, "0123456789") == strlen(o->ref);
	if (history_load(numeric ? mask_for(o) : HISTORY_ALL, l) != 0) return NULL;
	const struct history_entry *e = history_find(l, o->ref);
	if (!e) log_error("history: no entry %s; see `grabit history%s`", o->ref, o->ocr ? " --ocr" : "");
	return e;
}

static const char *payload(const struct history_entry *e) {
	if (e->kind == HISTORY_OCR) return e->text;
	return e->url ? e->url : e->path;
}

static int do_copy(const struct opts *o) {
	struct history_list l = {0};
	const struct history_entry *e = resolve(o, &l);
	int rc = 1;
	if (e && payload(e)) {
		rc = clipboard_set_text(payload(e)) == 0 ? 0 : 1;
		if (rc == 0) log_info("copied %s", e->kind == HISTORY_OCR ? "the text" : payload(e));
	}
	history_list_free(&l);
	return rc;
}

static int do_open(const struct opts *o) {
	struct history_list l = {0};
	const struct history_entry *e = resolve(o, &l);
	int rc = 1;
	if (e && e->kind == HISTORY_OCR) {
		log_error("history: OCR entries have nothing to open; use `grabit history copy`");
	} else if (e) {
		const char *target = history_has_file(e) ? e->path : e->url;
		int x = target ? grabit_xdg_open(target) : -1;
		if (x == -2) log_error("xdg-open is not installed");
		if (!target) log_error("history: the file is gone and there is no link");
		rc = x == 0 ? 0 : 1;
	}
	history_list_free(&l);
	return rc;
}

static bool confirm_delete(struct config *cfg, bool yes) {
	const char *v = config_get(cfg, "history.confirm_delete");
	return yes || (v && strcmp(v, "false") == 0);
}

static int do_delete(const struct opts *o) {
	struct history_list l = {0};
	const struct history_entry *e = resolve(o, &l);
	if (!e) {
		history_list_free(&l);
		return 1;
	}
	struct config cfg;
	if (config_load(&cfg) != 0) {
		history_list_free(&l);
		return 1;
	}
	char what[1024];
	history_delete_summary(e, o->what, what, sizeof what);
	int rc = 0;
	if ((o->what & HISTORY_DELETE_HOST) && history_host_keeps(e))
		log_info("%s has no delete, so the link stays up", e->service ? e->service : "the host");
	bool can_host = (o->what & HISTORY_DELETE_HOST) && history_can_delete_host(e);
	bool can_file = (o->what & HISTORY_DELETE_FILE) && history_can_delete_file(e);
	if (!can_host && !can_file && ((e->kind == HISTORY_UPLOAD && e->url) || history_has_file(e))) {
		log_error("nothing grabit can delete here%s; `grabit history remove %s` forgets it",
				  e->external ? " (the file is yours, not one grabit made)" : "", o->ref);
		rc = 1;
		goto out;
	}
	if (!grabit_confirm(confirm_delete(&cfg, o->yes), what)) {
		rc = 1;
		goto out;
	}
	struct history_entry left = *e;
	bool host_gone = false, file_gone = false;
	if ((o->what & HISTORY_DELETE_HOST) && history_can_delete_host(e)) {
		host_gone = upload_delete_url(e->service, &cfg, e->delete_url, e->delete_auth) == 0;
		if (host_gone)
			log_info("deleted from %s", e->service);
		else
			log_error("could not delete it from %s", e->service);
		rc |= !host_gone;
	}
	if ((o->what & HISTORY_DELETE_FILE) && history_can_delete_file(e)) {
		file_gone = unlink(e->path) == 0;
		if (file_gone)
			log_info("deleted %s", e->path);
		else
			log_error("could not delete %s", e->path);
		rc |= !file_gone;
	}
	if (host_gone) {
		left.kind = HISTORY_FILE;
		left.url = left.service = left.delete_url = NULL;
		left.delete_auth = false;
	}
	if (file_gone) left.path = NULL;
	bool keep = (left.kind == HISTORY_UPLOAD && left.url) || history_has_file(&left);
	if (keep && (host_gone || file_gone)) {
		if (history_update(&left) < 0) rc = 1;
		log_info("kept the entry for what is left");
	} else if (!keep && history_remove(e->id) < 0) {
		rc = 1;
	} else if (!keep) {
		log_info("forgot it");
	}
out:
	config_free(&cfg);
	history_list_free(&l);
	return rc;
}

static int do_remove(const struct opts *o) {
	struct history_list l = {0};
	const struct history_entry *e = resolve(o, &l);
	int rc = e && history_remove(e->id) == 0 ? 0 : 1;
	if (rc == 0) log_info("forgot it");
	history_list_free(&l);
	return rc;
}

static int do_clear(const struct opts *o) {
	const char *what = o->all ? "forget all history" : o->ocr ? "forget the OCR history"
															  : "forget the upload and file history";
	if (!grabit_confirm(o->yes, what)) return 1;
	int n = history_clear(mask_for(o));
	if (n < 0) return 1;
	log_info("forgot %d entr%s", n, n == 1 ? "y" : "ies");
	return 0;
}

int cmd_history(int argc, char **argv) {
	for (int i = 0; i < argc; i++)
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			return subcmd_help(SUBCMDS, N_SUBCMDS, "history");
	const char *sub = argc > 0 && argv[0][0] != '-' ? argv[0] : "list";
	int skip = argc > 0 && argv[0][0] != '-' ? 1 : 0;
	struct opts o;
	if (parse(argc - skip, argv + skip, &o) != 0) return usage();
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "list", sub)) return do_list(&o);
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "copy", sub)) return do_copy(&o);
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "open", sub)) return do_open(&o);
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "delete", sub)) return do_delete(&o);
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "remove", sub)) return do_remove(&o);
	if (subcmd_is(SUBCMDS, N_SUBCMDS, "clear", sub)) return do_clear(&o);
	return usage();
}
