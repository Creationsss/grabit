// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "clipboard/clipboard.h"
#include "log.h"
#include "paths.h"
#include "upload/upload.h"
#include "util/util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct set_upload_test {
	pthread_t thread;
	atomic_bool done;
	struct config cfg;
	char service[128];
	char *path;
	int rc;
	int deleted;
	char url[1024];
	char error[256];
	char delete_error[256];
};

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

static void *upload_thread(void *data) {
	struct set_upload_test *t = data;
	struct upload_result r = {0};
	log_capture_errors(t->error, sizeof t->error);
	t->rc = upload_perform(t->service, t->path, &t->cfg, false, &r);
	log_capture_errors(NULL, 0);
	if (t->rc == 0 && r.url)
		snprintf(t->url, sizeof t->url, "%s", r.url);
	else if (!t->error[0])
		upload_friendly_error(&r, t->error, sizeof t->error);
	if (t->rc == 0 && !r.url) t->rc = -1;
	if (t->rc == 0) {
		log_capture_errors(t->delete_error, sizeof t->delete_error);
		t->deleted = upload_delete(t->service, &t->cfg, &r);
		log_capture_errors(NULL, 0);
	}
	upload_result_free(&r);
	unlink(t->path);
	atomic_store(&t->done, true);
	return NULL;
}

static void free_test(struct set_upload_test *t) {
	config_free(&t->cfg);
	free(t->path);
	free(t);
}

void set_test_upload(struct settings_state *st, struct set_row *r) {
	(void)r;
	if (st->upload_test) {
		set_toast(st, "Already testing the upload");
		return;
	}
	const char *service = config_get(&st->cfg, "service");
	if (!service || !service[0]) {
		set_toast(st, "Pick an upload service first");
		return;
	}
	if (!upload_service_known(service)) {
		set_toast(st, "%s is not a built-in service or an installed uploader", service);
		return;
	}
	const char *missing = upload_missing_setting(&st->cfg, service);
	if (missing) {
		struct set_row *m = set_find_row(st, missing);
		set_toast(st, "Set %s first", m ? m->label : missing);
		return;
	}
	struct set_upload_test *t = calloc(1, sizeof *t);
	if (t && config_copy(&t->cfg, &st->cfg) != 0) {
		free(t);
		t = NULL;
	}
	if (!t || !(t->path = test_image())) {
		if (t) config_free(&t->cfg);
		free(t);
		set_toast(st, "Could not make the test image");
		return;
	}
	snprintf(t->service, sizeof t->service, "%s", service);
	if (pthread_create(&t->thread, NULL, upload_thread, t) != 0) {
		unlink(t->path);
		free_test(t);
		set_toast(st, "Could not start the test upload");
		return;
	}
	st->upload_test = t;
	set_toast(st, "Uploading a test image to %s", service);
}

static void copy_url(struct settings_state *st, const char *url) {
	if (clipboard_set_text(url) == 0)
		set_toast(st, "Link copied");
	else
		set_toast(st, "Could not copy the link");
}

bool set_tests_poll(struct settings_state *st) {
	struct set_upload_test *t = st->upload_test;
	if (!t || !atomic_load(&t->done)) return false;
	pthread_join(t->thread, NULL);
	st->upload_test = NULL;
	if (t->rc == 0 && t->deleted == 0) {
		set_toast(st, "Upload to %s works; the test image was deleted from it", t->service);
	} else if (t->rc == 0) {
		if (t->deleted > 0)
			set_toast(st, "Upload to %s works, but it gives no way to delete the test image: %s",
					  t->service, t->url);
		else
			set_toast(st, "Upload to %s works, but deleting the test image failed (%s): %s",
					  t->service, t->delete_error[0] ? t->delete_error : "no reason given", t->url);
		char *url = strdup(t->url);
		if (url) set_ask(st, "Copy", false, copy_url, url);
	} else {
		set_toast(st, "Upload to %s failed: %s", t->service,
				  t->error[0] ? t->error : "no link came back");
	}
	free_test(t);
	return true;
}

void set_tests_free(struct settings_state *st) {
	struct set_upload_test *t = st->upload_test;
	st->upload_test = NULL;
	if (!t) return;
	if (atomic_load(&t->done)) {
		pthread_join(t->thread, NULL);
		free_test(t);
	} else {
		pthread_detach(t->thread);
	}
}
