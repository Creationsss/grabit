// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/record.h"

#include "args.h"
#include "capture/capture.h"
#include "clipboard/clipboard.h"
#include "config.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "record/compose.h"
#include "record/controls.h"
#include "record/ffmpeg.h"
#include "record/overlay.h"
#include "record/pid.h"
#include "record/ring.h"
#include "region/region.h"
#include "tray/tray.h"
#include "upload/upload.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static atomic_int g_stop;
static atomic_int g_pause;

static void on_record_signal(int sig) {
	(void)sig;
	atomic_store(&g_stop, 1);
}

static void on_pause_signal(int sig) {
	(void)sig;
	atomic_fetch_xor(&g_pause, 1);
}

struct prev_sigs {
	struct sigaction sigint;
	struct sigaction sigterm;
	struct sigaction sighup;
	struct sigaction sigusr1;
	struct sigaction sigpipe;
};

static void install_signal_handlers(struct prev_sigs *prev) {
	struct sigaction sa = {0};
	sa.sa_handler = on_record_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, &prev->sigint);
	sigaction(SIGTERM, &sa, &prev->sigterm);
	sigaction(SIGHUP, &sa, &prev->sighup);

	struct sigaction pa = {0};
	pa.sa_handler = on_pause_signal;
	sigemptyset(&pa.sa_mask);
	sigaction(SIGUSR1, &pa, &prev->sigusr1);

	struct sigaction ign = {0};
	ign.sa_handler = SIG_IGN;
	sigemptyset(&ign.sa_mask);
	sigaction(SIGPIPE, &ign, &prev->sigpipe);
}

static void restore_signal_handlers(const struct prev_sigs *prev) {
	sigaction(SIGINT, &prev->sigint, NULL);
	sigaction(SIGTERM, &prev->sigterm, NULL);
	sigaction(SIGHUP, &prev->sighup, NULL);
	sigaction(SIGUSR1, &prev->sigusr1, NULL);
	sigaction(SIGPIPE, &prev->sigpipe, NULL);
}

static int read_int_cfg(struct config *cfg, const char *key, int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	long n = strtol(v, NULL, 10);
	if (n < lo) return def;
	if (n > hi) return hi;
	return (int)n;
}

static int read_fps(struct config *cfg) {
	return read_int_cfg(cfg, "recording.fps", 30, 1, 120);
}

static int read_crf(struct config *cfg) {
	return read_int_cfg(cfg, "recording.crf", 23, 0, 51);
}

static bool read_cursor(struct config *cfg) {
	const char *v = config_get(cfg, "recording.cursor");
	return !v || strcmp(v, "true") == 0;
}

static const char *read_format(struct config *cfg) {
	const char *v = config_get(cfg, "recording.format");
	if (v && (strcmp(v, "webm") == 0 || strcmp(v, "gif") == 0)) return v;
	return "mp4";
}

static const char *read_ffmpeg(struct config *cfg) {
	const char *v = config_get(cfg, "recording.ffmpeg");
	return (v && v[0]) ? v : "ffmpeg";
}

static const char *read_preset(struct config *cfg) {
	const char *v = config_get(cfg, "recording.preset");
	return (v && v[0]) ? v : "fast";
}

static const char *read_tune(struct config *cfg) {
	const char *v = config_get(cfg, "recording.tune");
	return (v && v[0]) ? v : NULL;
}

static const char *read_pix_fmt(struct config *cfg) {
	const char *v = config_get(cfg, "recording.pix_fmt");
	return (v && v[0]) ? v : "yuv420p";
}

static int64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static char *build_record_path(struct config *cfg, const struct args *a,
							   const char *format, bool keep_locally) {
	enum paths_dest dest = keep_locally ? PATHS_DEST_VIDEOS : PATHS_DEST_TEMP;
	char ext[16];
	snprintf(ext, sizeof ext, ".%s", format);
	return paths_build_output(cfg, a->filename_tpl, ext, dest);
}

struct seg_ctx {
	const char *ffmpeg_bin;
	const char *format;
	const char *preset;
	const char *tune;
	const char *pix_fmt;
	int w;
	int h;
	int fps;
	int crf;
	const char *final_path;
	char **segs;
	size_t n_segs;
	size_t cap_segs;
	pid_t *pending;
	size_t n_pending;
	size_t cap_pending;
	pid_t pid;
	int fd;
	struct ring *ring;
	pthread_t enc;
	struct enc_state es;
	bool enc_running;
	bool failed;
};

static int seg_begin(struct seg_ctx *sc) {
	if (sc->n_segs == sc->cap_segs) {
		size_t cap = sc->cap_segs ? sc->cap_segs * 2 : 4;
		char **p = realloc(sc->segs, cap * sizeof *p);
		if (!p) return -1;
		sc->segs = p;
		sc->cap_segs = cap;
	}
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s.seg%zu.%s",
						 sc->final_path, sc->n_segs, sc->format) != 0)
		return -1;
	if (spawn_ffmpeg(sc->ffmpeg_bin, sc->format, sc->preset, sc->tune, sc->pix_fmt,
					 sc->w, sc->h, sc->fps, sc->crf, path, &sc->pid, &sc->fd) != 0) {
		free(path);
		return -1;
	}
	sc->segs[sc->n_segs++] = path;
	ring_reset(sc->ring);
	sc->es = (struct enc_state){
		.ring = sc->ring,
		.write_fd = sc->fd,
		.stop = &g_stop,
	};
	if (pthread_create(&sc->enc, NULL, encoder_thread, &sc->es) != 0) {
		log_error("pthread_create: %s", strerror(errno));
		close(sc->fd);
		sc->fd = -1;
		(void)wait_ffmpeg(sc->pid);
		sc->pid = -1;
		return -1;
	}
	sc->enc_running = true;
	return 0;
}

static void seg_finish(struct seg_ctx *sc, struct grabit_wl_state *s) {
	if (!sc->enc_running) return;
	ring_stop(sc->ring);
	int64_t t0 = now_ns();
	if (s) {
		while (!atomic_load_explicit(&sc->es.done, memory_order_acquire)) {
			if (grabit_wl_pump(s, 30) != 0) break;
		}
	}
	pthread_join(sc->enc, NULL);
	sc->enc_running = false;
	if (sc->fd >= 0) {
		close(sc->fd);
		sc->fd = -1;
	}
	log_debug("recording: segment drained in %lld ms; ffmpeg finalizes in background",
			  (long long)((now_ns() - t0) / 1000000));
	if (sc->n_pending == sc->cap_pending) {
		size_t cap = sc->cap_pending ? sc->cap_pending * 2 : 4;
		pid_t *p = realloc(sc->pending, cap * sizeof *p);
		if (!p) {
			if (wait_ffmpeg(sc->pid) != 0) sc->failed = true;
			sc->pid = -1;
			return;
		}
		sc->pending = p;
		sc->cap_pending = cap;
	}
	sc->pending[sc->n_pending++] = sc->pid;
	sc->pid = -1;
}

static bool seg_any_pending_alive(struct seg_ctx *sc) {
	bool alive = false;
	for (size_t i = 0; i < sc->n_pending; i++) {
		if (sc->pending[i] <= 0) continue;
		int status = 0;
		pid_t r = waitpid(sc->pending[i], &status, WNOHANG);
		if (r == sc->pending[i]) {
			if (ffmpeg_exit_rc(status) != 0) sc->failed = true;
			sc->pending[i] = -1;
		} else if (r == 0) {
			alive = true;
		}
	}
	return alive;
}

static void seg_reap_all(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_pending; i++) {
		if (sc->pending[i] <= 0) continue;
		if (wait_ffmpeg(sc->pending[i]) != 0) sc->failed = true;
	}
	sc->n_pending = 0;
}

static void seg_ctx_free(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_segs; i++)
		free(sc->segs[i]);
	free(sc->segs);
	sc->segs = NULL;
	sc->n_segs = sc->cap_segs = 0;
	free(sc->pending);
	sc->pending = NULL;
	sc->n_pending = sc->cap_pending = 0;
}

static void seg_unlink_all(struct seg_ctx *sc) {
	for (size_t i = 0; i < sc->n_segs; i++)
		unlink(sc->segs[i]);
}

static double capture_loop(struct grabit_wl_state *s, struct rec_layout *layout,
						   struct buf_pool *pool, bool cursor,
						   struct seg_ctx *sc, struct rec_controls *ctrl) {
	int64_t period_ns = 1000000000 / sc->fps;
	struct ring *ring = sc->ring;
	bool paused = false;
	int64_t seg_start = now_ns();
	int64_t active_ns = 0;
	int64_t frame_idx = 0;
	int consec_fail = 0;
	bool direct = rec_layout_is_direct(layout);

	while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
		bool want_pause = atomic_load_explicit(&g_pause, memory_order_relaxed) != 0;
		if (want_pause != paused) {
			paused = want_pause;
			controls_set_paused(ctrl, paused);
			if (paused) {
				active_ns += now_ns() - seg_start;
				seg_finish(sc, s);
				log_info("recording paused");
			} else {
				if (seg_begin(sc) != 0) {
					log_error("recording: could not start a new segment");
					sc->failed = true;
					atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
					break;
				}
				seg_start = now_ns();
				frame_idx = 0;
				log_info("recording resumed");
			}
		}

		int64_t active_now = active_ns + (paused ? 0 : now_ns() - seg_start);
		controls_tick(ctrl, active_now / 1000000000);

		if (paused) {
			if (grabit_wl_pump(s, 30) != 0) {
				log_error("recording: lost wayland connection");
				atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
				break;
			}
			continue;
		}

		int64_t deadline = seg_start + frame_idx * period_ns;
		int64_t cur = now_ns();
		if (cur - deadline > period_ns * 4)
			frame_idx = (cur - seg_start) / period_ns;
		bool interrupted = false;
		while (cur < deadline) {
			if (atomic_load_explicit(&g_stop, memory_order_relaxed) ||
				atomic_load_explicit(&g_pause, memory_order_relaxed) != 0) {
				interrupted = true;
				break;
			}
			int64_t rem_ms = (deadline - cur) / 1000000;
			if (rem_ms >= 1) {
				if (grabit_wl_pump(s, (int)rem_ms) != 0) {
					log_error("recording: lost wayland connection");
					atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
					interrupted = true;
					break;
				}
			} else {
				struct timespec ts = {
					.tv_sec = deadline / 1000000000,
					.tv_nsec = deadline % 1000000000,
				};
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
			}
			cur = now_ns();
		}
		if (interrupted) continue;

		void *frame_buf = pool_try_acquire(pool);
		if (!frame_buf) {
			ring_record_drop(ring);
			frame_idx++;
			continue;
		}
		struct buf_pool *frame_pool = pool;
		int rc;
		if (direct) {
			rc = rec_layout_capture_direct_into(s, layout, cursor, frame_buf,
												layout->dst_stride, layout->dst_h);
		} else {
			rc = rec_layout_capture_compose(s, layout, cursor, frame_buf);
		}
		if (rc != 0) {
			pool_release(pool, frame_buf);
			frame_buf = NULL;
			frame_pool = NULL;
		}

		if (rc != 0) {
			if (++consec_fail == 1) log_warn("recording: frame capture failed");
			if (consec_fail > 30) {
				log_error("recording: too many consecutive capture failures; stopping");
				atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
				break;
			}
			frame_idx++;
			continue;
		}
		consec_fail = 0;

		struct frame f = {
			.data = frame_buf,
			.width = layout->dst_w,
			.height = layout->dst_h,
			.stride = layout->dst_stride,
			.format = WL_SHM_FORMAT_ARGB8888,
			.pool = frame_pool,
		};
		ring_push(ring, &f);
		frame_idx++;
	}

	if (!paused) active_ns += now_ns() - seg_start;
	return (double)active_ns / 1e9;
}

int record_toggle(struct config *cfg, const struct args *a) {
	if (stop_running_recording() == 0) return 0;

	const char *ffmpeg_bin = read_ffmpeg(cfg);
	if (!grabit_in_path(ffmpeg_bin)) {
		log_error("recording: `%s` not found in $PATH (install ffmpeg or set recording.ffmpeg)",
				  ffmpeg_bin);
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "ffmpeg not found; install ffmpeg or set recording.ffmpeg",
			.force = true,
		});
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

	struct image *frozen = calloc(s.n_outputs, sizeof *frozen);
	if (!frozen) {
		grabit_wl_finish(&s);
		log_error("oom");
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "out of memory",
			.force = true,
		});
		return 1;
	}
	for (size_t i = 0; i < s.n_outputs; i++) {
		if (capture_output_full(&s, s.outputs[i], false, &frozen[i]) != 0) {
			log_warn("freeze capture of %s failed; selector will be dimmed",
					 s.outputs[i]->name ? s.outputs[i]->name : "?");
			memset(&frozen[i], 0, sizeof frozen[i]);
			continue;
		}
		if (image_apply_transform(&frozen[i], s.outputs[i]->transform) != 0) {
			log_warn("freeze transform of %s failed; output may look skewed",
					 s.outputs[i]->name ? s.outputs[i]->name : "?");
		}
	}

	struct rect r;
	int rc;
	if (a->fullscreen) {
		struct rect fs_rect;
		int plan = grabit_wl_fullscreen_plan(&s, a->fullscreen_target, &fs_rect);
		if (plan < 0) {
			for (size_t i = 0; i < s.n_outputs; i++)
				image_free(&frozen[i]);
			free(frozen);
			grabit_wl_finish(&s);
			notify_send(&(struct notify_opts){
				.summary = "Recording failed",
				.body = "no matching monitor; see terminal for details",
				.force = true,
			});
			return 1;
		}
		if (plan == 0) {
			r = fs_rect;
			rc = 0;
		} else {
			struct rect *mon = NULL;
			size_t n_mon = 0;
			grabit_wl_monitor_rects(&s, &mon, &n_mon);
			rc = region_select(&s, cfg, frozen, false, &r, NULL, NULL, NULL, NULL, NULL,
							   NULL, mon, n_mon);
			free(mon);
		}
	} else {
		rc = region_select(&s, cfg, frozen, false, &r, NULL, NULL, NULL, NULL, NULL, NULL,
						   NULL, 0);
	}
	for (size_t i = 0; i < s.n_outputs; i++)
		image_free(&frozen[i]);
	free(frozen);

	if (rc != 0 || r.w <= 0 || r.h <= 0) {
		grabit_wl_finish(&s);
		log_info("recording cancelled");
		notify_send(&(struct notify_opts){
			.summary = "Recording cancelled",
		});
		return 0;
	}

	struct rec_layout layout = {0};
	if (rec_layout_build(&s, r, &layout) != 0) {
		log_error("region does not overlap any output");
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "selected region did not intersect any output",
			.force = true,
		});
		return 1;
	}

	bool keep_locally = !upload_service || config_also_save(cfg);
	const char *format = read_format(cfg);
	char *output_path = build_record_path(cfg, a, format, keep_locally);
	if (!output_path) {
		log_error("recording: could not build output path");
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "could not build output path; see terminal for details",
			.force = true,
		});
		return 1;
	}

	int fps = read_fps(cfg);
	int crf = read_crf(cfg);
	bool cursor = read_cursor(cfg);
	const char *preset = read_preset(cfg);
	const char *tune = read_tune(cfg);
	const char *pix_fmt = read_pix_fmt(cfg);

	if (write_pid_file_excl(getpid()) != 0) {
		const char *body;
		if (errno == EEXIST) {
			log_error("another grabit recording started concurrently; aborting this one");
			body = "another recording is already starting";
		} else {
			log_error("could not write recording pidfile: %s", strerror(errno));
			body = "could not write pid file; see terminal for details";
		}
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = body,
			.force = true,
		});
		free(output_path);
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	atomic_store_explicit(&g_stop, 0, memory_order_relaxed);
	atomic_store_explicit(&g_pause, 0, memory_order_relaxed);
	struct prev_sigs prev = {0};
	install_signal_handlers(&prev);

	struct ring ring;
	ring_init(&ring);

	struct seg_ctx sc = {
		.ffmpeg_bin = ffmpeg_bin,
		.format = format,
		.preset = preset,
		.tune = tune,
		.pix_fmt = pix_fmt,
		.w = layout.dst_w,
		.h = layout.dst_h,
		.fps = fps,
		.crf = crf,
		.final_path = output_path,
		.pid = -1,
		.fd = -1,
		.ring = &ring,
	};
	if (seg_begin(&sc) != 0) {
		restore_signal_handlers(&prev);
		ring_destroy(&ring);
		seg_unlink_all(&sc);
		seg_ctx_free(&sc);
		unlink_pid_file();
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "ffmpeg failed to start; install ffmpeg or set recording.ffmpeg",
			.force = true,
		});
		free(output_path);
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	log_info("recording %dx%d (%zu output%s) @ %d fps → %s; "
			 "use the on-screen controls or re-run `grabit --record` to stop "
			 "(SIGUSR1 toggles pause)",
			 layout.dst_w, layout.dst_h, layout.n, layout.n == 1 ? "" : "s",
			 fps, output_path);

	struct buf_pool pool = {0};
	{
		size_t buf_size = (size_t)layout.dst_stride * (size_t)layout.dst_h;
		if (pool_init(&pool, POOL_CAP, buf_size) != 0) {
			log_error("recording: could not allocate frame pool");
			seg_finish(&sc, NULL);
			seg_reap_all(&sc);
			restore_signal_handlers(&prev);
			ring_destroy(&ring);
			seg_unlink_all(&sc);
			seg_ctx_free(&sc);
			unlink_pid_file();
			free(output_path);
			rec_layout_free(&layout);
			grabit_wl_finish(&s);
			return 1;
		}
	}

	struct overlay_state *overlay = overlay_start(&s, r);
	struct rec_controls *controls = controls_start(&s, r, &g_stop, &g_pause);
	struct tray_state *tray = a->no_tray ? NULL : tray_start();

	double secs = capture_loop(&s, &layout, &pool, cursor, &sc, controls);

	tray_stop(tray);
	controls_stop(controls);
	overlay_stop(overlay);

	seg_finish(&sc, NULL);
	if (seg_any_pending_alive(&sc) || sc.n_segs > 1) {
		log_info("recording: finishing %zu segment%s...",
				 sc.n_segs, sc.n_segs == 1 ? "" : "s");
		notify_send(&(struct notify_opts){
			.summary = "Recording finishing",
			.body = grabit_basename(output_path),
		});
	}
	seg_reap_all(&sc);

	bool ok = !sc.failed && sc.n_segs > 0;
	if (ok) {
		if (sc.n_segs == 1) {
			if (rename(sc.segs[0], output_path) != 0) {
				log_error("rename(%s -> %s): %s", sc.segs[0], output_path,
						  strerror(errno));
				ok = false;
			}
		} else {
			if (concat_segments(ffmpeg_bin, format, sc.segs, sc.n_segs,
								output_path, &g_stop) == 0) {
				seg_unlink_all(&sc);
			} else {
				log_error("recording: concat failed; segments kept next to %s",
						  output_path);
				ok = false;
			}
		}
	}

	log_info("recording: %zu frames captured, %zu encoded, %zu dropped (%.2fs)",
			 ring.pushed, ring.popped, ring.dropped, secs);

	if (ok) {
		int max_mb = read_int_cfg(cfg, "recording.max_size_mb", 0, 0, 100000);
		if (max_mb > 0 && strcmp(format, "mp4") != 0) {
			log_debug("recording: max_size_mb only applies to mp4; skipping");
			max_mb = 0;
		}
		struct stat st;
		if (max_mb > 0 && stat(output_path, &st) == 0 &&
			(long long)st.st_size > (long long)max_mb * 1024 * 1024) {
			log_info("recording: %lld bytes > %d MiB, compressing...",
					 (long long)st.st_size, max_mb);
			notify_send(&(struct notify_opts){
				.summary = "Recording compressing",
				.body = grabit_basename(output_path),
			});
			if (compress_to_target_size(ffmpeg_bin, output_path, max_mb, secs, &g_stop) == 0) {
				if (stat(output_path, &st) == 0) {
					log_info("recording: compressed to %lld bytes",
							 (long long)st.st_size);
				}
			} else {
				log_warn("recording: compression failed; original kept");
			}
		}
		if (keep_locally) log_info("saved: %s", output_path);
		if (!upload_service) {
			notify_send(&(struct notify_opts){
				.summary = "Recording saved",
				.body = grabit_basename(output_path),
			});
		}

		if (upload_service) {
			notify_send(&(struct notify_opts){
				.summary = "Uploading recording",
				.body = upload_service,
			});
			struct upload_result ur = {0};
			int up_rc = upload_perform(upload_service, output_path, cfg, a->chunked, &ur);
			if (up_rc == 0 && ur.url) {
				clipboard_set_text(ur.url);
				puts(ur.url);
				fflush(stdout);
				notify_send(&(struct notify_opts){
					.summary = "Recording uploaded",
					.body = "link copied to clipboard",
				});
				if (!keep_locally) unlink(output_path);
			} else {
				char body[256];
				upload_friendly_error(&ur, body, sizeof body);
				log_error("recording upload failed; file kept at %s", output_path);
				log_error("  retry with: grabit -f %s --%s", output_path, upload_service);
				notify_send(&(struct notify_opts){
					.summary = "Upload failed",
					.body = body,
					.force = true,
				});
			}
			upload_result_free(&ur);
		}
	} else {
		log_error("recording failed; output may be incomplete: %s", output_path);
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = grabit_basename(output_path),
			.force = true,
		});
	}

	restore_signal_handlers(&prev);
	ring_destroy(&ring);
	pool_destroy(&pool);
	seg_ctx_free(&sc);
	unlink_pid_file();
	free(output_path);
	rec_layout_free(&layout);
	grabit_wl_finish(&s);
	return ok ? 0 : 1;
}
