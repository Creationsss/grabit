// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/capture.h"
#include "capture/freeze.h"
#include "capture/save.h"
#include "clipboard/clipboard.h"
#include "config/config.h"
#include "exit.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "paths.h"
#include "pin/pin.h"
#include "record/record.h"
#include "region/edit_persist.h"
#include "sound/sound.h"
#include "upload/upload.h"
#include "util/rect.h"
#include "util/util.h"
#include "wl/wl.h"

#include "app/app.h"

int gapp_run_upload(struct config *cfg, const struct args *a) {
	const char *service = NULL;
	if (upload_preflight(cfg, a, &service) != 0) return 1;

	bool is_temp = false;
	int status = GRABIT_EXIT_FAIL;
	char *path = gapp_acquire_source(a, cfg, ACTION_UPLOAD, &is_temp, NULL, &status);
	if (!path) return status;

	struct upload_result r = {0};
	int rc = upload_perform(service, path, cfg, a->chunked, &r);

	if (rc == 0) {
		bool copied = clipboard_set_text(r.url) == 0;
		char *m = mime_for_file(path);
		const char *summary = mime_is_video(m) ? "Video uploaded" : "Uploaded";
		struct notify_opts opts = {
			.summary = summary,
			.body = copied ? "link copied to clipboard"
						   : "link is on stdout; clipboard write failed",
			.icon_path = mime_is_image(m) ? path : NULL,
		};
		notify_send(&opts);
		grabit_sound_play(cfg);
		if (mime_is_image(m)) {
			gapp_maybe_show_preview(cfg, path, "Uploaded", r.url);
		}
		free(m);
		if (r.thumb_url && *r.thumb_url) log_info("thumbnail: %s", r.thumb_url);
		if (r.del_url && *r.del_url) log_info("deletion: %s", r.del_url);
		puts(r.url);
		fflush(stdout);
	} else {
		char body[256];
		upload_friendly_error(&r, body, sizeof body);
		char kept[512] = {0};
		if (is_temp) {
			log_error("upload failed; capture kept at %s", path);
			log_error("retry with: grabit -f %s --%s", path, service);
			snprintf(kept, sizeof kept, "%s\nkept at %s", body, path);
			is_temp = false;
			gapp_clear_tmpfile();
		}
		notify_send(&(struct notify_opts){
			.summary = "Upload failed",
			.body = kept[0] ? kept : body,
			.force = true,
		});
	}

	upload_result_free(&r);
	gapp_release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}

int gapp_run_copy(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	int status = GRABIT_EXIT_FAIL;
	char *path = gapp_acquire_source(a, cfg, ACTION_COPY, &is_temp, NULL, &status);
	if (!path) return status;

	int rc = clipboard_set_image_file(path);

	const char *base = grabit_basename(path);
	if (!base || !base[0]) base = path;
	if (rc == 0) {
		notify_send(&(struct notify_opts){
			.summary = "Copied to clipboard",
			.body = base,
			.icon_path = path,
		});
		grabit_sound_play(cfg);
		gapp_maybe_show_preview(cfg, path, "Copied", NULL);
	} else {
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = "wayland clipboard rejected the image",
			.force = true,
		});
	}

	gapp_release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}

int gapp_run_output(struct config *cfg, const struct args *a) {
	if (a->file && !a->edit) {
		puts(a->file);
		return 0;
	}
	bool is_temp = false;
	int status = GRABIT_EXIT_FAIL;
	char *path = gapp_acquire_source(a, cfg, ACTION_OUTPUT, &is_temp, NULL, &status);
	if (!path) return status;

	puts(path);
	notify_send(&(struct notify_opts){
		.summary = "Saved",
		.body = grabit_basename(path),
		.icon_path = path,
	});
	grabit_sound_play(cfg);
	char dir[4096];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash && slash != dir) *slash = '\0';
	gapp_maybe_show_preview(cfg, path, grabit_basename(path), dir);
	free(path);
	return 0;
}

int gapp_run_pin(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	struct rect r = {0};
	int status = GRABIT_EXIT_FAIL;
	char *path = gapp_acquire_source(a, cfg, ACTION_PIN, &is_temp, &r, &status);
	if (!path) return status;
	bool have_rect = (r.w > 0 && r.h > 0);

	int rc = pin_spawn(cfg, path, have_rect ? &r : NULL);

	if (rc == 0) grabit_sound_play(cfg);

	gapp_release_source(path, is_temp);
	return rc == 0 ? 0 : 1;
}
