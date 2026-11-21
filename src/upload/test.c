// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "args.h"
#include "config/config.h"
#include "log.h"
#include "paths.h"
#include "subcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <cairo.h>

static char *test_image(void) {
	char *path = paths_temp_file("upload-test.png");
	if (!path) return NULL;
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 360, 120);
	cairo_t *cr = cairo_create(s);
	cairo_set_source_rgb(cr, 0.10, 0.11, 0.13);
	cairo_paint(cr);
	cairo_set_source_rgb(cr, 1.0, 0.55, 0.32);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 24);
	cairo_move_to(cr, 24, 56);
	cairo_show_text(cr, "grabit test upload");
	char when[64];
	time_t now = time(NULL);
	struct tm tm;
	strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime_r(&now, &tm));
	cairo_set_source_rgb(cr, 0.86, 0.86, 0.86);
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 15);
	cairo_move_to(cr, 24, 88);
	cairo_show_text(cr, when);
	cairo_destroy(cr);
	bool ok = cairo_surface_write_to_png(s, path) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(s);
	if (ok) return path;
	free(path);
	return NULL;
}

enum upload_test_status upload_test_check(struct config *cfg, const char *service,
										  const char **missing) {
	if (!service || !service[0]) return UPLOAD_TEST_NO_SERVICE;
	if (!upload_service_known(service)) return UPLOAD_TEST_UNKNOWN;
	*missing = upload_missing_setting(cfg, service);
	return *missing ? UPLOAD_TEST_MISSING : UPLOAD_TEST_READY;
}

int upload_test_run(const char *service, struct config *cfg, struct upload_test_result *out) {
	*out = (struct upload_test_result){0};
	char *path = test_image();
	if (!path) {
		snprintf(out->error, sizeof out->error, "could not make the test image");
		log_error("%s", out->error);
		return -1;
	}
	struct upload_result r = {0};
	log_capture_errors(out->error, sizeof out->error);
	int rc = upload_perform(service, path, cfg, false, &r);
	log_capture_errors(NULL, 0);
	unlink(path);
	free(path);
	if (rc == 0 && !r.url) rc = -1;
	if (rc != 0) {
		if (!out->error[0]) {
			upload_friendly_error(&r, out->error, sizeof out->error);
			log_error("%s", out->error);
		}
		upload_result_free(&r);
		return -1;
	}
	snprintf(out->url, sizeof out->url, "%s", r.url);
	log_capture_errors(out->error, sizeof out->error);
	int del = upload_delete(service, cfg, &r);
	log_capture_errors(NULL, 0);
	out->deleted = del == 0 ? UPLOAD_TEST_DELETED : del > 0 ? UPLOAD_TEST_KEPT
															: UPLOAD_TEST_DELETE_FAILED;
	upload_result_free(&r);
	return 0;
}

static const struct subcmd SUBCMDS[] = {
	{"test", NULL, "[<service>]", "upload a test image, then delete it where the service allows"},
};
#define N_SUBCMDS (sizeof SUBCMDS / sizeof SUBCMDS[0])

static int test_cmd(const char *service) {
	struct config cfg;
	if (config_load(&cfg) != 0) return 1;
	if (!service) service = config_get(&cfg, "service");
	const char *missing = NULL;
	char near[64];
	struct upload_test_result t;
	int rc = 1;
	switch (upload_test_check(&cfg, service, &missing)) {
	case UPLOAD_TEST_NO_SERVICE:
		log_error("no upload service: pass one, or run `grabit set service <name>`");
		break;
	case UPLOAD_TEST_UNKNOWN:
		if (upload_suggest_service(service, near, sizeof near) == 0)
			log_error("unknown service `%s` (did you mean `%s`?)", service, near);
		else
			log_error("unknown service `%s`: not built in and no such uploader", service);
		break;
	case UPLOAD_TEST_MISSING:
		log_error("%s is not set: run `grabit set %s <value>`", missing, missing);
		break;
	case UPLOAD_TEST_READY:
		if (upload_test_run(service, &cfg, &t) != 0) break;
		printf("%s\n", t.url);
		fflush(stdout);
		if (t.deleted == UPLOAD_TEST_DELETED)
			log_info("upload to %s works; the test image was deleted from it", service);
		else if (t.deleted == UPLOAD_TEST_KEPT)
			log_warn("upload to %s works, but it gives no way to delete the test image", service);
		else
			log_warn("upload to %s works, but deleting the test image failed", service);
		rc = 0;
		break;
	}
	config_free(&cfg);
	return rc;
}

int cmd_upload(int argc, char **argv) {
	for (int i = 0; i < argc; i++)
		if (args_is_help_flag(argv[i])) return subcmd_help(SUBCMDS, N_SUBCMDS, "upload");
	if (argc < 1 || argc > 2 || !subcmd_is(SUBCMDS, N_SUBCMDS, "test", argv[0]))
		return subcmd_usage(SUBCMDS, N_SUBCMDS, "upload");
	return test_cmd(argc == 2 ? argv[1] : NULL);
}
