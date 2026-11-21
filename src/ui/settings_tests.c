// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "ui/settings_internal.h"

#include "clipboard/clipboard.h"
#include "upload/upload.h"

#include <stdlib.h>
#include <string.h>

struct set_upload_test {
	pthread_t thread;
	atomic_bool done;
	struct config cfg;
	char service[128];
	struct upload_test_result result;
};

static void *upload_thread(void *data) {
	struct set_upload_test *t = data;
	upload_test_run(t->service, &t->cfg, &t->result);
	atomic_store(&t->done, true);
	return NULL;
}

static void free_test(struct set_upload_test *t) {
	config_free(&t->cfg);
	free(t);
}

void set_test_upload(struct settings_state *st, struct set_row *r) {
	(void)r;
	if (st->upload_test) {
		set_toast(st, "Already testing the upload");
		return;
	}
	const char *service = config_get(&st->cfg, "service");
	const char *missing = NULL;
	struct set_row *m;
	switch (upload_test_check(&st->cfg, service, &missing)) {
	case UPLOAD_TEST_NO_SERVICE:
		set_toast(st, "Pick an upload service first");
		return;
	case UPLOAD_TEST_UNKNOWN:
		set_toast(st, "%s is not a built-in service or an installed uploader", service);
		return;
	case UPLOAD_TEST_MISSING:
		m = set_find_row(st, missing);
		set_toast(st, "Set %s first", m ? m->label : missing);
		return;
	case UPLOAD_TEST_READY:
		break;
	}
	struct set_upload_test *t = calloc(1, sizeof *t);
	if (!t || config_copy(&t->cfg, &st->cfg) != 0) {
		free(t);
		set_toast(st, "Could not start the test upload");
		return;
	}
	snprintf(t->service, sizeof t->service, "%s", service);
	if (pthread_create(&t->thread, NULL, upload_thread, t) != 0) {
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
	const struct upload_test_result *res = &t->result;
	if (!res->url[0]) {
		set_toast(st, "Upload to %s failed: %s", t->service,
				  res->error[0] ? res->error : "no link came back");
	} else if (res->deleted == UPLOAD_TEST_DELETED) {
		set_toast(st, "Upload to %s works; the test image was deleted from it", t->service);
	} else {
		if (res->deleted == UPLOAD_TEST_KEPT)
			set_toast(st, "Upload to %s works, but it gives no way to delete the test image: %s",
					  t->service, res->url);
		else
			set_toast(st, "Upload to %s works, but deleting the test image failed (%s): %s",
					  t->service, res->error[0] ? res->error : "no reason given", res->url);
		char *url = strdup(res->url);
		if (url) set_ask(st, "Copy", false, copy_url, url);
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
