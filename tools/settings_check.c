// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config/config.h"
#include "log.h"
#include "paths.h"
#include "ui/settings_internal.h"
#include "ui/window.h"
#include "upload/upload.h"
#include "wl/wl.h"

#include <fcntl.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failed;
static int g_passed;
static char g_root[256];

static struct settings_state st;
static struct ui_window win;
static struct grabit_wl_state wls;

static void check(bool ok, const char *fmt, ...) {
	if (ok) {
		g_passed++;
		return;
	}
	g_failed++;
	va_list ap;
	va_start(ap, fmt);
	fputs("  FAIL: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static int rm_one(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
	(void)sb;
	(void)flag;
	(void)ftw;
	return remove(path);
}

static void setup_env(void) {
	const char *tmp = getenv("TMPDIR");
	snprintf(g_root, sizeof g_root, "%s/grabit-settings-check-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
	if (!mkdtemp(g_root)) {
		perror("mkdtemp");
		exit(1);
	}
	static const char *const DIRS[][2] = {
		{"HOME", ""},
		{"XDG_CONFIG_HOME", "/config"},
		{"XDG_STATE_HOME", "/state"},
		{"XDG_DATA_HOME", "/data"},
		{"XDG_CACHE_HOME", "/cache"},
		{"XDG_RUNTIME_DIR", "/run"},
	};
	for (size_t i = 0; i < sizeof DIRS / sizeof *DIRS; i++) {
		char path[512];
		snprintf(path, sizeof path, "%s%s", g_root, DIRS[i][1]);
		mkdir(path, 0700);
		setenv(DIRS[i][0], path, 1);
	}
	static const char *const OFF[] = {"WAYLAND_DISPLAY", "GRABIT_ZIPLINE_AUTH", "GRABIT_LOG_FILE",
									  "GRABIT_DEBUG"};
	for (size_t i = 0; i < sizeof OFF / sizeof *OFF; i++)
		unsetenv(OFF[i]);
}

static void write_config(const char *text) {
	if (paths_mkdir_p(paths_config_dir()) != 0 ||
		paths_atomic_write(paths_config_file(), text, strlen(text)) != 0) {
		fprintf(stderr, "settings_check: could not write config.toml\n");
		exit(1);
	}
}

static void open_settings(const char *config) {
	write_config(config);
	if (settings_state_open(&st, &wls) != 0) {
		fprintf(stderr, "settings_check: could not build the settings rows\n");
		exit(1);
	}
	win = (struct ui_window){.scale120 = 120, .user = &st};
	st.win = &win;
}

static size_t key_count(const char *key) {
	size_t n = 0;
	for (size_t i = 0; i < st.n_pages; i++)
		for (size_t j = 0; j < st.pages[i].n; j++)
			n += strcmp(st.pages[i].rows[j]->key, key) == 0;
	return n;
}

static const char *page_of(const char *key) {
	struct set_row *r = set_find_row(&st, key);
	return r ? r->page : NULL;
}

static bool active(const char *key) {
	struct set_row *r = set_find_row(&st, key);
	return r && !set_inactive_reason(&st, r);
}

static void check_pages(void) {
	open_settings("");
	for (size_t i = 0; i < st.n_pages; i++) {
		const struct set_page *p = &st.pages[i];
		check(strcmp(p->title, "Other") != 0, "a known key has no page and landed on Other (%s)",
			  p->n ? p->rows[0]->key : "-");
		for (size_t j = 0; j < p->n; j++) {
			const struct set_row *r = p->rows[j];
			if (r->no_config) continue;
			check(!r->info.internal, "internal key %s has a row on %s", r->key, p->title);
			size_t seen = key_count(r->key);
			check(seen == 1, "%s appears %zu times", r->key, seen);
			if (!r->dep) continue;
			const char *home = page_of(r->dep->needs);
			check(home && strcmp(home, p->title) == 0, "%s is on %s but %s, which controls it, is on %s",
				  r->key, p->title, r->dep->needs, home ? home : "no page");
		}
	}
	struct set_row *cfg = set_find_row(&st, "~/config/grabit/config.toml");
	check(cfg && cfg->action && page_of(cfg->key) && strcmp(page_of(cfg->key), "General") == 0,
		  "General has no row for ~/config/grabit/config.toml with an action");
	settings_state_close(&st);
}

static void check_format_deps(void) {
	open_settings("");
	check(active("png.level") && active("capture.hdr"), "png settings inactive for png");
	check(!active("jpeg.quality"), "jpeg.quality active for png");
	check(!active("webp.lossless") && !active("webp.quality"), "webp settings active for png");
	struct set_row *q = set_find_row(&st, "webp.quality");
	const char *why = q ? set_inactive_reason(&st, q) : NULL;
	check(why && strstr(why, "webp"), "webp.quality greyed for png should name webp, got %s",
		  why ? why : "nothing");
	set_apply(&st, set_find_row(&st, "format"), "webp");
	struct config written;
	config_load(&written);
	const char *fmt = config_get(&written, "format");
	check(fmt && strcmp(fmt, "webp") == 0, "format change not written (read back %s)",
		  fmt ? fmt : "nothing");
	config_free(&written);
	check(!active("png.level") && !active("capture.hdr"), "png settings active for webp");
	check(active("webp.lossless") && active("webp.quality"), "webp settings inactive for webp");
	set_apply(&st, set_find_row(&st, "webp.lossless"), "true");
	check(!active("webp.quality"), "webp.quality active while lossless is on");
	settings_state_close(&st);

	open_settings("format = \"jpg\"\n");
	check(active("jpeg.quality"), "jpeg.quality inactive for a hand-written jpg");
	check(!active("png.level"), "png.level active for a hand-written jpg");
	settings_state_close(&st);
}

static void check_unknown_keys(void) {
	int saved = dup(STDERR_FILENO);
	int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
	if (null >= 0) dup2(null, STDERR_FILENO);
	open_settings("bogus_key = 1\n");
	if (saved >= 0) dup2(saved, STDERR_FILENO);
	if (null >= 0) close(null);
	if (saved >= 0) close(saved);
	const char *p = page_of("bogus_key");
	check(p && strcmp(p, "General") == 0, "unknown key not listed on General (got %s)",
		  p ? p : "no page");
	settings_state_close(&st);
}

static void check_upload_test(void) {
	static const struct {
		const char *toml;
		const char *service;
		enum upload_test_status want;
		const char *missing;
	} CASES[] = {
		{"", NULL, UPLOAD_TEST_NO_SERVICE, NULL},
		{"", "nosuch", UPLOAD_TEST_UNKNOWN, NULL},
		{"", "zipline", UPLOAD_TEST_MISSING, "services.zipline.domain"},
		{"[services.zipline]\ndomain = \"https://example.invalid/api/upload\"\n", "zipline",
		 UPLOAD_TEST_MISSING, "services.zipline.auth"},
		{"[services.zipline]\ndomain = \"https://example.invalid/api/upload\"\nauth = \"t\"\n",
		 "zipline", UPLOAD_TEST_READY, NULL},
	};
	for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
		struct config c;
		const char *missing = NULL;
		write_config(CASES[i].toml);
		config_load(&c);
		enum upload_test_status got = upload_test_check(&c, CASES[i].service, &missing);
		config_free(&c);
		const char *svc = CASES[i].service ? CASES[i].service : "no service";
		check(got == CASES[i].want, "upload check case %zu (%s): status %d, want %d", i, svc, got,
			  CASES[i].want);
		if (CASES[i].missing)
			check(missing && strcmp(missing, CASES[i].missing) == 0,
				  "upload check case %zu (%s): missing %s, want %s", i, svc,
				  missing ? missing : "nothing", CASES[i].missing);
	}
}

int main(void) {
	setup_env();
	log_init(true, false);
	check_pages();
	check_format_deps();
	check_unknown_keys();
	check_upload_test();
	nftw(g_root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (g_failed) {
		fprintf(stderr, "settings checks: %d failed, %d passed\n", g_failed, g_passed);
		return 1;
	}
	printf("settings checks passed (%d)\n", g_passed);
	return 0;
}
