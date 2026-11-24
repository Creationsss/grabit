// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "app/app.h"
#include "config/config.h"
#include "history/history.h"
#include "log.h"
#include "paths.h"
#include "ui/settings_grid.h"
#include "ui/settings_internal.h"
#include "ui/window.h"
#include "upload/upload.h"
#include "util/util.h"
#include "wl/wl.h"

#include <ftw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cairo/cairo.h>

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

static void write_file(const char *path, const char *text) {
	if (paths_atomic_write(path, text, strlen(text)) != 0) {
		fprintf(stderr, "settings_check: could not write %s\n", path);
		exit(1);
	}
}

static void write_config(const char *text) {
	paths_mkdir_p(paths_config_dir());
	write_file(paths_config_file(), text);
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

static void check_filename(void) {
	open_settings("");
	check(active("filename_preset"), "filename_preset inactive without a template");
	set_apply(&st, set_find_row(&st, "filename"), "%Y-%m-%d");
	check(!active("filename_preset"), "filename_preset active while a template is set");
	set_apply(&st, set_find_row(&st, "filename"), NULL);
	check(active("filename_preset"), "filename_preset inactive after clearing the template");
	settings_state_close(&st);

	open_settings("filename = \"\"\n");
	check(active("filename_preset"), "an empty filename in config.toml should count as unset");
	settings_state_close(&st);
}

static void check_unknown_keys(void) {
	log_mute(true);
	open_settings("bogus_key = 1\n");
	log_mute(false);
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

static void check_reserved(void) {
	check(gapp_name_is_reserved("help"), "plugin name help is not reserved");
	check(gapp_name_is_reserved("settings"), "plugin name settings is not reserved");
	check(!gapp_name_is_reserved("my-plugin"), "an ordinary plugin name is reserved");
}

static void check_overrides(void) {
	char dir[600], ten[700], twenty[700], broken[700], env[700];
	snprintf(dir, sizeof dir, "%s/config.d", paths_config_dir());
	paths_mkdir_p(dir);
	snprintf(ten, sizeof ten, "%s/10-work.toml", dir);
	snprintf(twenty, sizeof twenty, "%s/20-laptop.toml", dir);
	snprintf(broken, sizeof broken, "%s/15-broken.toml", dir);
	snprintf(env, sizeof env, "%s/extra.toml", g_root);
	write_config("format = \"png\"\nnotifications = false\nalso_save = true\n");
	write_file(ten, "format = \"jpeg\"\nnotifications = true\n");
	write_file(twenty, "format = \"webp\"\n");
	write_file(broken, "this is = [not toml\n");
	write_file(env, "notifications = false\n");
	setenv("GRABIT_CONFIG", env, 1);

	struct config c;
	check(config_read(&c) == 0, "config with overrides did not read");
	const char *fmt = config_get(&c, "format");
	check(fmt && strcmp(fmt, "webp") == 0, "later config.d file should win, got format=%s",
		  fmt ? fmt : "nothing");
	const char *notif = config_get(&c, "notifications");
	check(notif && strcmp(notif, "false") == 0, "GRABIT_CONFIG should apply last, got %s",
		  notif ? notif : "nothing");
	const char *save = config_get(&c, "also_save");
	check(save && strcmp(save, "true") == 0, "a key only in config.toml should stay");
	const char *src = config_source(&c, "format");
	check(src && strstr(src, "20-laptop.toml"), "format should come from 20-laptop.toml, got %s",
		  src ? src : "config.toml");
	check(config_source(&c, "also_save") == NULL, "also_save should come from config.toml");
	config_free(&c);

	open_settings("format = \"png\"\nnotifications = false\nalso_save = true\n");
	struct set_row *r = set_find_row(&st, "format");
	check(!set_can_reset(&st, r), "an overridden row should not offer reset");
	set_apply(&st, r, "png");
	check(strstr(r->error, "20-laptop.toml") != NULL, "editing an overridden row should name the file, got %s",
		  r->error);
	struct config merged;
	config_load(&merged);
	check(config_save(&merged) != 0, "a config with overrides must not be saved whole");
	config_free(&merged);
	set_apply(&st, set_find_row(&st, "also_save"), "false");
	char *saved = NULL;
	size_t len = 0;
	grabit_read_file(paths_config_file(), 1 << 16, &saved, &len);
	check(saved && !strstr(saved, "webp") && strstr(saved, "png") && strstr(saved, "also_save = false"),
		  "config.toml must not receive override values, got %s", saved ? saved : "nothing");
	free(saved);
	settings_state_close(&st);

	unsetenv("GRABIT_CONFIG");
	unlink(ten);
	unlink(twenty);
	unlink(broken);
	unlink(env);
	rmdir(dir);
}

static size_t page_rows_with(const char *title, const char *prefix) {
	for (size_t i = 0; i < st.n_pages; i++) {
		if (strcmp(st.pages[i].title, title) != 0) continue;
		size_t n = 0;
		for (size_t j = 0; j < st.pages[i].n; j++)
			n += strncmp(st.pages[i].rows[j]->key, prefix, strlen(prefix)) == 0;
		return n;
	}
	return 0;
}

static void check_history(void) {
	struct config c;
	write_config("");
	config_load(&c);
	history_clear(HISTORY_ALL);
	char pic[600];
	snprintf(pic, sizeof pic, "%s/pic.png", g_root);
	write_file(pic, "x");
	history_record_file(&c, pic, false);
	struct upload_result zip = {.url = "https://z.example/u/a.png",
								.body = "{\"files\":[{\"id\":\"abc\"}]}"};
	config_set(&c, "services.zipline.domain", "https://z.example");
	history_record_upload(&c, "zipline", NULL, false, false, &zip);
	history_record_ocr(&c, "first line\nsecond", "eng", "de");

	struct stat sb;
	check(stat(history_file(), &sb) == 0 && (sb.st_mode & 0777) == 0600,
		  "history.jsonl should be mode 0600");
	struct history_list l;
	history_load(HISTORY_MEDIA, &l);
	check(l.n == 2 && l.v[0].kind == HISTORY_UPLOAD && l.v[1].kind == HISTORY_FILE,
		  "media history should list the upload first, then the file");
	if (l.n == 2) {
		check(l.v[0].delete_url && strcmp(l.v[0].delete_url, "https://z.example/api/user/files/abc") == 0 &&
				  l.v[0].delete_auth,
			  "a zipline entry should carry its delete URL, got %s",
			  l.v[0].delete_url ? l.v[0].delete_url : "none");
		check(history_find(&l, "2") == &l.v[1] && history_find(&l, l.v[0].id) == &l.v[0] &&
				  history_find(&l, "3") == NULL,
			  "entries should resolve by number and by id");
		check(history_has_file(&l.v[1]) && !history_host_keeps(&l.v[0]),
			  "the file entry should see its file, the zipline one a delete");
		char sum[600];
		history_delete_summary(&l.v[1], HISTORY_DELETE_BOTH, sum, sizeof sum);
		check(strstr(sum, "pic.png") && strstr(sum, "forget it"), "delete summary was %s", sum);
	}
	history_list_free(&l);

	struct upload_result sx = {.url = "https://s.example/x", .del_url = "https://s.example/del/x"};
	history_record_upload(&c, "my-sxcu", NULL, false, false, &sx);
	struct upload_result nest = {.url = "https://n.example/x"};
	history_record_upload(&c, "nest", NULL, false, false, &nest);
	history_load(HISTORY_UPLOAD, &l);
	check(l.n == 3 && l.v[1].delete_url && !l.v[1].delete_auth && history_host_keeps(&l.v[0]),
		  "sxcu entries should keep the DeletionURL; nest should have no delete");
	history_list_free(&l);

	config_set(&c, "history.ocr", "false");
	history_record_ocr(&c, "skipped", "eng", NULL);
	config_set(&c, "history.enabled", "false");
	history_record_file(&c, pic, false);
	history_load(HISTORY_ALL, &l);
	check(l.n == 5, "toggles off should record nothing, got %zu entries", l.n);
	char id[64] = "";
	if (l.n) snprintf(id, sizeof id, "%s", l.v[l.n - 1].id);
	history_list_free(&l);
	config_free(&c);

	char *taken = NULL;
	check(history_take(id, 0, &taken) == 1 && taken, "take should hand back the removed line");
	check(history_restore(taken) == 0, "restore should succeed");
	history_load(HISTORY_ALL, &l);
	check(l.n == 5 && strcmp(l.v[l.n - 1].id, id) == 0, "a restored entry should go back to its place by time");
	history_list_free(&l);
	free(taken);
	check(history_remove(id) == 0 && history_remove(id) == 1, "remove should drop an entry once");
	open_settings("");
	struct set_page *gen = &st.pages[0];
	bool in_hist = false, broken = false;
	for (size_t i = 0; i < gen->n; i++) {
		bool h = gen->rows[i]->section && strcmp(gen->rows[i]->section, "History") == 0;
		if (in_hist && !h) broken = true;
		in_hist |= h;
	}
	check(in_hist && !broken, "General should end with one unbroken History group");
	settings_state_close(&st);
	struct config oc = {0};
	history_record_ocr(&oc, "alpha line\nsecret second line words", "eng", NULL);
	open_settings("");
	ui_text_set(&st.search, "secret");
	settings_filter(&st);
	check(st.results.n == 1, "search should find OCR text past the first line, got %zu", st.results.n);
	ui_text_set(&st.search, "copy");
	settings_filter(&st);
	bool ocr_hit = false;
	for (size_t i = 0; i < st.results.n; i++)
		ocr_hit |= strncmp(st.results.rows[i]->key, "grabit history copy --ocr", 25) == 0;
	check(!ocr_hit, "OCR rows should not match on their key");
	settings_state_close(&st);
	struct history_list ol;
	history_load(HISTORY_OCR, &ol);
	if (ol.n) history_remove(ol.v[0].id);
	history_list_free(&ol);
	check(history_clear(HISTORY_OCR) == 1, "clearing OCR should forget one entry");
	history_load(HISTORY_ALL, &l);
	check(l.n == 3, "three uploads should remain, got %zu", l.n);
	history_list_free(&l);

	open_settings("");
	check(st.hist.n == 3 && set_hist_grid_h(&st) > 100, "the History grid should hold 3 entries, got %zu and h %.0f", st.hist.n, set_hist_grid_h(&st));
	check(page_rows_with("OCR history", "grabit history copy --ocr ") == 0, "the OCR page should be empty");
	struct set_row *clear = set_find_row(&st, "grabit history clear");
	check(clear && clear->button && strstr(clear->hint, "3"), "the clear row should count 3 entries");
	check(st.history_page + 2 == st.n_pages && st.pages[st.history_page].bottom &&
			  st.pages[st.ocr_history_page].bottom && !st.pages[0].bottom,
		  "History and OCR history should be the last two pages, pinned to the bottom");
	st.view_h = 720;
	struct set_hist_grid *g = st.grid;
	check(g && set_hist_grid_h(&st) > 0 && g->n_items == 3 && g->n_heads == 1 &&
			  strcmp(g->heads[0].label, "Today") == 0,
		  "the grid should hold 3 entries under one Today header");
	g->type = GT_SAVED;
	g->dirty = true;
	set_hist_grid_h(&st);
	check(g->n_items == 0, "the Saved filter should hide uploads, got %zu", g->n_items);
	g->type = GT_ALL;
	snprintf(g->service, sizeof g->service, "nest");
	g->dirty = true;
	set_hist_grid_h(&st);
	check(g->n_items == 1, "the service filter should keep only nest, got %zu", g->n_items);
	g->service[0] = '\0';
	ui_text_set(&g->query, "Z.EXAMPLE");
	g->dirty = true;
	set_hist_grid_h(&st);
	check(g->n_items == 1, "search should match the link case-insensitively, got %zu", g->n_items);
	ui_text_set(&g->query, "");
	g->size = GS_SMALL;
	g->dirty = true;
	set_hist_grid_h(&st);
	check(g->n_items == 0, "uploads with no file have no size, got %zu", g->n_items);
	g->size = GS_ANY;
	g->dirty = true;
	check(set_side_row_y(&st, st.history_page) > set_side_row_y(&st, st.history_page - 1) + 64,
		  "the bottom pages should sit apart from the rest");
	settings_state_close(&st);

	history_clear(HISTORY_ALL);
	char img[600];
	snprintf(img, sizeof img, "%s/thumb.png", g_root);
	cairo_surface_t *px = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 48);
	cairo_surface_write_to_png(px, img);
	cairo_surface_destroy(px);
	struct config rc = {0};
	for (int i = 0; i < 6; i++)
		history_record_file(&rc, img, false);
	open_settings("");
	st.page = st.history_page;
	st.view_h = 720;
	set_hist_grid_h(&st);
	int64_t t0 = grabit_now_ms();
	for (int i = 0; i < 4; i++)
		set_refresh_history(&st);
	int64_t took = grabit_now_ms() - t0;
	check(took < 500, "refreshing the history with loaders running took %lld ms", (long long)took);
	settings_state_close(&st);
	unlink(img);
	history_clear(HISTORY_ALL);
	for (int i = 0; i < 20; i++) {
		if (fork() != 0) continue;
		struct config k = {0};
		for (int j = 0; j < 10; j++)
			history_record_ocr(&k, "concurrent text that is long enough to need a few writes", "eng", NULL);
		_exit(0);
	}
	while (wait(NULL) > 0)
		;
	history_load(HISTORY_ALL, &l);
	check(l.n == 200, "20 parallel writers should leave 200 entries, got %zu", l.n);
	history_list_free(&l);
	history_clear(HISTORY_ALL);
	unlink(pic);
}

int main(void) {
	setup_env();
	log_init(true, false);
	check_pages();
	check_format_deps();
	check_filename();
	check_unknown_keys();
	check_upload_test();
	check_reserved();
	check_overrides();
	check_history();
	nftw(g_root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
	if (g_failed) {
		fprintf(stderr, "settings checks: %d failed, %d passed\n", g_failed, g_passed);
		return 1;
	}
	printf("settings checks passed (%d)\n", g_passed);
	return 0;
}
