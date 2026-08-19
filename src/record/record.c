// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/record.h"

#include "app/app.h"

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
#include "record/pw.h"
#include "record/rec_cfg.h"
#include "record/ring.h"
#include "record/screencast.h"
#include "record/segments.h"
#include "record/setup.h"
#include "region/edit_persist.h"
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

static const char *format_encoder(const char *format) {
	if (strcmp(format, "webm") == 0) return "libvpx-vp9";
	if (strcmp(format, "gif") == 0) return NULL;
	return "libx264";
}

static bool ffmpeg_has_encoder(const char *bin, const char *enc) {
	char *const argv[] = {(char *)bin, (char *)"-hide_banner", (char *)"-loglevel",
						  (char *)"error", (char *)"-encoders", NULL};
	struct grabit_buf out = {0};
	bool capped = false;
	int status = 0;
	if (grabit_spawn_capture(argv, true, 1 << 20, &out, &capped, &status) != 0) {
		grabit_buf_free(&out);
		return true;
	}
	bool found = out.data && strstr(out.data, enc) != NULL;
	grabit_buf_free(&out);
	return found;
}

static bool explain_missing_encoder(const char *bin, const char *format) {
	const char *enc = format_encoder(format);
	if (!enc || ffmpeg_has_encoder(bin, enc)) return false;

	log_error("recording: `%s` has no %s encoder, which %s output needs",
			  bin, enc, format);
	static const char *const ALT[] = {"webm", "mp4", "gif"};
	for (size_t i = 0; i < sizeof ALT / sizeof ALT[0]; i++) {
		if (strcmp(ALT[i], format) == 0) continue;
		const char *aenc = format_encoder(ALT[i]);
		if (aenc && !ffmpeg_has_encoder(bin, aenc)) continue;
		log_error("  this ffmpeg can do %s: `grabit set recording.format %s`",
				  ALT[i], ALT[i]);
		break;
	}
	return true;
}

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

	const char *format = rec_cfg_format(cfg);

	const char *upload_service = NULL;
	if (!a->no_upload) {
		bool default_is_upload =
			gapp_default_action(config_get(cfg, "default_action")) == ACTION_UPLOAD;
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

	bool have_stills = capture_backend_available(&s);
	bool can_stream = have_stills && capture_is_streaming_capable(&s);
	bool use_screencast = !can_stream && screencast_available(&s);

	if (!can_stream && !use_screencast) {
		if (have_stills)
			log_error("recording needs a streaming capture protocol; this "
					  "compositor only does single-shot screenshots");
		else
			(void)grabit_wl_require_capture(&s);
		screencast_explain_unavailable();
		rec_fail_notify("recording is not supported on this compositor");
		grabit_wl_finish(&s);
		return 1;
	}
	if (use_screencast)
		log_debug("recording: using the %s screencast source",
				  screencast_backend_name(&s));

	struct rect r = {0};
	int32_t corner_radius = 0;
	int32_t border_size = 0;
	int rc = rec_pick_region(&s, cfg, a, &r, &corner_radius, &border_size);
	if (rc != 0 && rc != REGION_SELECT_CANCELLED) {
		grabit_wl_finish(&s);
		return 1;
	}
	if (rc != 0 || r.w <= 0 || r.h <= 0) {
		grabit_wl_finish(&s);
		log_debug("recording cancelled");
		notify_send(&(struct notify_opts){
			.summary = "Recording cancelled",
		});
		return 0;
	}
	if (!a->fullscreen) persist_capture_state(cfg, NULL, &r);
	grabit_sleep_secs(a->delay_secs);

	int fps = rec_cfg_fps(cfg);
	bool cursor = rec_cfg_cursor(cfg);
	struct rec_layout layout = {0};
	struct screencast *scast = NULL;
	struct pw_capture *cap = NULL;
	char *output_path = NULL;
	int32_t frame_w, frame_h, frame_stride;

	if (use_screencast) {
		uint32_t node = 0;
		scast = screencast_start(&s, r, cursor, &node);
		if (!scast) {
			rec_fail_notify("the compositor refused to start a screencast");
			goto err_wl;
		}
		cap = pw_capture_open(node, fps);
		if (!cap) {
			rec_fail_notify("could not read the screencast stream from pipewire");
			goto err_source;
		}
		pw_capture_size(cap, &frame_w, &frame_h, &frame_stride);
	} else {
		if (rec_layout_build(&s, r, corner_radius, border_size, &layout) != 0) {
			log_error("region does not overlap any output");
			rec_fail_notify("selected region did not intersect any output");
			goto err_wl;
		}
		frame_w = layout.dst_w;
		frame_h = layout.dst_h;
		frame_stride = layout.dst_stride;
	}

	bool keep_locally = !upload_service || config_also_save(cfg);
	output_path = rec_record_path(cfg, a, format, keep_locally);
	if (!output_path) {
		log_error("recording: could not build output path");
		rec_fail_notify("could not build output path");
		goto err_source;
	}

	if (write_pid_file() != 0) {
		if (errno == EWOULDBLOCK) {
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

	struct seg_ctx sc = {
		.ffmpeg_bin = ffmpeg_bin,
		.format = format,
		.preset = rec_cfg_preset(cfg),
		.tune = rec_cfg_tune(cfg),
		.pix_fmt = rec_cfg_pix_fmt(cfg),
		.w = frame_w,
		.h = frame_h,
		.fps = fps,
		.crf = rec_cfg_crf(cfg),
		.final_path = output_path,
		.stop = &grabit_rec_stop,
		.pid = -1,
		.fd = -1,
		.ring = &ring,
	};

	struct buf_pool pool = {0};
	size_t buf_size = (size_t)frame_stride * (size_t)frame_h;
	struct tray_state *tray =
		(a->no_tray || !rec_cfg_tray(cfg)) ? NULL : tray_start();
	if (tray) loop_set_tray_pid(tray_get_pid(tray));
	if (seg_begin(&sc) != 0) {
		if (explain_missing_encoder(ffmpeg_bin, format))
			rec_fail_notify("ffmpeg lacks the encoder for this recording format");
		else
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

	if (use_screencast)
		log_debug("recording via the %s screencast", screencast_backend_name(&s));
	else
		log_debug("recording %zu output%s", layout.n, layout.n == 1 ? "" : "s");
	log_info("recording %dx%d @ %d fps -> %s", frame_w, frame_h, fps, output_path);

	bool show_dims = rec_cfg_show_dimensions(cfg);
	bool rounded_ui = rec_cfg_rounded_gui(cfg);
	struct overlay_state *overlay = overlay_start(&s, r, show_dims, corner_radius, rounded_ui);
	struct rec_controls *controls =
		controls_start(&s, r, &grabit_rec_stop, &grabit_rec_pause, &grabit_rec_abort, rounded_ui);

	double secs;
	if (use_screencast) {
		pw_capture_bind(cap, &pool, &ring);
		secs = rec_pw_loop(&s, cap, &sc, controls);
	} else {
		secs = rec_capture_loop(&s, &layout, &pool, cursor, &sc, controls);
	}

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
		log_debug("recording aborted; output discarded");
		notify_send(&(struct notify_opts){
			.summary = "Recording aborted",
		});
	} else {
		if (seg_any_pending_alive(&sc) || sc.n_segs > 1) {
			log_debug("recording: finishing %zu segment%s...",
					  sc.n_segs, sc.n_segs == 1 ? "" : "s");
			notify_send(&(struct notify_opts){
				.summary = "Recording finishing",
				.body = grabit_basename(output_path),
			});
		}
		seg_reap_all(&sc);

		ok = seg_assemble(&sc, output_path) == 0;

		log_debug("recording: %zu frames captured, %zu encoded, %zu dropped (%.2fs)",
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
			if (explain_missing_encoder(ffmpeg_bin, format))
				rec_fail_notify("ffmpeg lacks the encoder for this recording format");
			else
				rec_fail_notify(grabit_basename(output_path));
		}
	}

	record_signals_restore(&prev);
	ring_destroy(&ring);
	pool_destroy(&pool);
	seg_ctx_free(&sc);
	unlink_pid_file();
	free(output_path);
	pw_capture_close(cap);
	screencast_stop(scast);
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
err_source:
	pw_capture_close(cap);
	screencast_stop(scast);
	rec_layout_free(&layout);
err_wl:
	grabit_wl_finish(&s);
	return 1;
}
