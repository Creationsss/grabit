// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/publish.h"

#include "clipboard/clipboard.h"
#include "log.h"
#include "notify/notify.h"
#include "record/ffmpeg.h"
#include "record/rec_cfg.h"
#include "upload/upload.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void maybe_compress(struct config *cfg, const struct publish_opts *po) {
	int max_mb = rec_cfg_int(cfg, "recording.max_size_mb", 0, 0, 100000);
	if (max_mb <= 0) return;
	if (strcmp(po->format, "mp4") != 0) {
		log_debug("recording: max_size_mb only applies to mp4; skipping");
		return;
	}
	struct stat st;
	if (stat(po->output_path, &st) != 0 ||
		(long long)st.st_size <= (long long)max_mb * 1024 * 1024)
		return;
	log_info("recording: %lld bytes > %d MiB, compressing...",
			 (long long)st.st_size, max_mb);
	notify_send(&(struct notify_opts){
		.summary = "Recording compressing",
		.body = grabit_basename(po->output_path),
	});
	if (compress_to_target_size(po->ffmpeg_bin, po->output_path, max_mb,
								po->secs, po->stop) == 0) {
		if (stat(po->output_path, &st) == 0) {
			log_info("recording: compressed to %lld bytes",
					 (long long)st.st_size);
		}
	} else {
		log_warn("recording: compression failed; original kept");
	}
}

void record_publish(struct config *cfg, const struct publish_opts *po) {
	maybe_compress(cfg, po);

	if (po->keep_locally) log_info("saved: %s", po->output_path);
	if (!po->upload_service) {
		notify_send(&(struct notify_opts){
			.summary = "Recording saved",
			.body = grabit_basename(po->output_path),
		});
		return;
	}

	notify_send(&(struct notify_opts){
		.summary = "Uploading recording",
		.body = po->upload_service,
	});
	struct upload_result ur = {0};
	int up_rc = upload_perform(po->upload_service, po->output_path, cfg,
							   po->chunked, &ur);
	if (up_rc == 0 && ur.url) {
		clipboard_set_text(ur.url);
		puts(ur.url);
		fflush(stdout);
		notify_send(&(struct notify_opts){
			.summary = "Recording uploaded",
			.body = "link copied to clipboard",
		});
		if (!po->keep_locally) unlink(po->output_path);
	} else {
		char body[256];
		upload_friendly_error(&ur, body, sizeof body);
		log_error("recording upload failed; file kept at %s", po->output_path);
		log_error("  retry with: grabit -f %s --%s", po->output_path,
				  po->upload_service);
		notify_send(&(struct notify_opts){
			.summary = "Upload failed",
			.body = body,
			.force = true,
		});
	}
	upload_result_free(&ur);
}
