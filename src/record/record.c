// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/record.h"

#include "args.h"
#include "capture/capture.h"
#include "config/config.h"
#include "log.h"
#include "notify/notify.h"
#include "record/compose.h"
#include "record/controls.h"
#include "record/loop.h"
#include "record/overlay.h"
#include "record/pid.h"
#include "record/publish.h"
#include "record/rec_cfg.h"
#include "record/ring.h"
#include "record/segments.h"
#include "record/setup.h"
#include "region/region.h"
#include "tray/tray.h"
#include "upload/upload.h"
#include "util/util.h"
#include "wl/wl.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int record_toggle(struct config *cfg, const struct args *a) {
	loop_init_shared();
	if (stop_running_recording() == 0) return 0;

	const char *ffmpeg_bin = rec_cfg_ffmpeg(cfg);
	if (!grabit_in_path(ffmpeg_bin)) {
		log_error("recording: `%s` not found in $PATH (install ffmpeg or set recording.ffmpeg)",
				  ffmpeg_bin);
		rec_fail_notify("ffmpeg not found; install ffmpeg or set recording.ffmpeg");
		return 1;
	}

	const char *upload_service = NULL;
	if (!a->no_upload) {
		const char *def_action = config_get(cfg, "default_action");
		bool default_is_upload = def_action && strcmp(def_action, "upload") == 0;
		if (a->service || default_is_upload) {
			if (upload_preflight(cfg, a, &upload_service) != 0) return 1;
		}
	}

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "could not connect to wayland compositor",
			.force = true,
		});
		return 1;
	}

	if (!capture_is_streaming_capable(&s)) {
		log_error("recording needs a frame-streaming capture protocol");
		log_error("  KWin's org.kde.KWin.ScreenShot2 is single-shot, so only "
				  "screenshots work on KDE Plasma");
		rec_fail_notify("recording is not supported on KDE Plasma");
		grabit_wl_finish(&s);
		return 1;
	}

	struct rect r = {0};
	int rc = rec_pick_region(&s, cfg, a, &r);
	if (rc != 0 || r.w <= 0 || r.h <= 0) {
		grabit_wl_finish(&s);
		log_info("recording cancelled");
		notify_send(&(struct notify_opts){
			.summary = "Recording cancelled",
		});
		return 0;
	}

	struct rec_layout layout = {0};
	char *output_path = NULL;
	if (rec_layout_build(&s, r, &layout) != 0) {
		log_error("region does not overlap any output");
		rec_fail_notify("selected region did not intersect any output");
		goto err_wl;
	}

	bool keep_locally = !upload_service || config_also_save(cfg);
	const char *format = rec_cfg_format(cfg);
	output_path = rec_record_path(cfg, a, format, keep_locally);
	if (!output_path) {
		log_error("recording: could not build output path");
		rec_fail_notify("could not build output path");
		goto err_layout;
	}

	if (write_pid_file_excl(getpid()) != 0) {
		if (errno == EEXIST) {
			log_error("another grabit recording started concurrently; aborting this one");
			rec_fail_notify("another recording is already starting");
		} else {
			log_error("could not write recording pidfile: %s", strerror(errno));
			rec_fail_notify("could not write pid file");
		}
		goto err_path;
	}

	atomic_store_explicit(&grabit_rec_stop, 0, memory_order_relaxed);
	atomic_store_explicit(&grabit_rec_pause, 0, memory_order_relaxed);
	atomic_store_explicit(&grabit_rec_abort, 0, memory_order_relaxed);
	struct prev_sigs prev = {0};
	record_signals_install(&prev);

	struct ring ring;
	ring_init(&ring);

	int fps = rec_cfg_fps(cfg);
	struct seg_ctx sc = {
		.ffmpeg_bin = ffmpeg_bin,
		.format = format,
		.preset = rec_cfg_preset(cfg),
		.tune = rec_cfg_tune(cfg),
		.pix_fmt = rec_cfg_pix_fmt(cfg),
		.w = layout.dst_w,
		.h = layout.dst_h,
		.fps = fps,
		.crf = rec_cfg_crf(cfg),
		.final_path = output_path,
		.stop = &grabit_rec_stop,
		.pid = -1,
		.fd = -1,
		.ring = &ring,
	};

	struct buf_pool pool = {0};
	size_t buf_size = (size_t)layout.dst_stride * (size_t)layout.dst_h;
	struct tray_state *tray =
		(a->no_tray || !rec_cfg_tray(cfg)) ? NULL : tray_start();
	if (tray) loop_set_tray_pid(tray_get_pid(tray));
	if (seg_begin(&sc) != 0) {
		rec_fail_notify("ffmpeg failed to start; install ffmpeg or set recording.ffmpeg");
		goto err_pipeline;
	}
	size_t pool_slots = pool_slots_for(buf_size);
	log_debug("recording: frame pool %zu x %zu KiB", pool_slots, buf_size / 1024);
	if (pool_init(&pool, pool_slots, buf_size) != 0) {
		log_error("recording: could not allocate frame pool");
		rec_fail_notify("could not allocate the frame pool");
		goto err_pipeline;
	}

	log_info("recording %dx%d (%zu output%s) @ %d fps -> %s; "
			 "use the on-screen controls or re-run `grabit --record` to stop "
			 "(SIGUSR1 toggles pause)",
			 layout.dst_w, layout.dst_h, layout.n, layout.n == 1 ? "" : "s",
			 fps, output_path);

	struct overlay_state *overlay = overlay_start(&s, r);
	struct rec_controls *controls =
		controls_start(&s, r, &grabit_rec_stop, &grabit_rec_pause, &grabit_rec_abort);

	double secs = rec_capture_loop(&s, &layout, &pool, rec_cfg_cursor(cfg),
								   &sc, controls);

	bool aborted = atomic_load_explicit(&grabit_rec_abort, memory_order_relaxed) != 0;

	tray_stop(tray);
	controls_stop(controls);
	overlay_stop(overlay);

	seg_finish(&sc, NULL);

	bool ok = true;
	if (aborted) {
		for (size_t i = 0; i < sc.n_pending; i++) {
			if (sc.pending[i] > 0) kill(sc.pending[i], SIGKILL);
		}
		seg_reap_all(&sc);
		seg_unlink_all(&sc);
		unlink(output_path);
		log_info("recording aborted; output discarded");
		notify_send(&(struct notify_opts){
			.summary = "Recording aborted",
		});
	} else {
		if (seg_any_pending_alive(&sc) || sc.n_segs > 1) {
			log_info("recording: finishing %zu segment%s...",
					 sc.n_segs, sc.n_segs == 1 ? "" : "s");
			notify_send(&(struct notify_opts){
				.summary = "Recording finishing",
				.body = grabit_basename(output_path),
			});
		}
		seg_reap_all(&sc);

		ok = seg_assemble(&sc, output_path) == 0;

		log_info("recording: %zu frames captured, %zu encoded, %zu dropped (%.2fs)",
				 ring.pushed, ring.popped, ring.dropped, secs);

		if (ok) {
			struct publish_opts po = {
				.ffmpeg_bin = ffmpeg_bin,
				.format = format,
				.output_path = output_path,
				.upload_service = upload_service,
				.keep_locally = keep_locally,
				.chunked = a->chunked,
				.secs = secs,
				.stop = &grabit_rec_stop,
			};
			record_publish(cfg, &po);
		} else {
			log_error("recording failed; output may be incomplete: %s", output_path);
			rec_fail_notify(grabit_basename(output_path));
		}
	}

	record_signals_restore(&prev);
	ring_destroy(&ring);
	pool_destroy(&pool);
	seg_ctx_free(&sc);
	unlink_pid_file();
	free(output_path);
	rec_layout_free(&layout);
	grabit_wl_finish(&s);
	return ok ? 0 : 1;

err_pipeline:
	tray_stop(tray);
	seg_finish(&sc, NULL);
	seg_reap_all(&sc);
	record_signals_restore(&prev);
	ring_destroy(&ring);
	seg_unlink_all(&sc);
	seg_ctx_free(&sc);
	unlink_pid_file();
err_path:
	free(output_path);
err_layout:
	rec_layout_free(&layout);
err_wl:
	grabit_wl_finish(&s);
	return 1;
}
