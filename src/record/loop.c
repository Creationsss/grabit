// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/loop.h"

#include "log.h"
#include "record/compose.h"
#include "record/controls.h"
#include "record/ring.h"
#include "record/segments.h"
#include "util/util.h"
#include "wl/wl.h"

#include <stdint.h>
#include <sys/mman.h>
#include <time.h>

#include <wayland-client.h>

atomic_int *grabit_rec_stop_ptr;
atomic_int *grabit_rec_pause_ptr;
atomic_int *grabit_rec_abort_ptr;

static atomic_int g_local_flags[3];

void loop_init_shared(void) {
	if (grabit_rec_stop_ptr) return;
	atomic_int *mem = mmap(NULL, sizeof g_local_flags, PROT_READ | PROT_WRITE,
						   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		log_warn("recording: mmap of shared flags failed; tray menu state may go stale");
		mem = g_local_flags;
	}
	grabit_rec_stop_ptr = mem;
	grabit_rec_pause_ptr = mem + 1;
	grabit_rec_abort_ptr = mem + 2;
}

static pid_t g_tray_pid = 0;
void loop_set_tray_pid(pid_t pid) {
	g_tray_pid = pid;
}

static void on_stop_signal(int sig) {
	(void)sig;
	atomic_store(&grabit_rec_stop, 1);
}

static void on_pause_signal(int sig) {
	(void)sig;
	atomic_fetch_xor(&grabit_rec_pause, 1);
}

static void on_abort_signal(int sig) {
	(void)sig;
	atomic_store(&grabit_rec_abort, 1);
	atomic_store(&grabit_rec_stop, 1);
}

void record_signals_install(struct prev_sigs *prev) {
	struct sigaction sa = {0};
	sa.sa_handler = on_stop_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, &prev->sigint);
	sigaction(SIGTERM, &sa, &prev->sigterm);
	sigaction(SIGHUP, &sa, &prev->sighup);

	struct sigaction aa = {0};
	aa.sa_handler = on_abort_signal;
	sigemptyset(&aa.sa_mask);
	sigaction(SIGQUIT, &aa, &prev->sigquit);

	struct sigaction pa = {0};
	pa.sa_handler = on_pause_signal;
	sigemptyset(&pa.sa_mask);
	sigaction(SIGUSR1, &pa, &prev->sigusr1);

	struct sigaction ign = {0};
	ign.sa_handler = SIG_IGN;
	sigemptyset(&ign.sa_mask);
	sigaction(SIGPIPE, &ign, &prev->sigpipe);
}

void record_signals_restore(const struct prev_sigs *prev) {
	sigaction(SIGINT, &prev->sigint, NULL);
	sigaction(SIGTERM, &prev->sigterm, NULL);
	sigaction(SIGHUP, &prev->sighup, NULL);
	sigaction(SIGQUIT, &prev->sigquit, NULL);
	sigaction(SIGUSR1, &prev->sigusr1, NULL);
	sigaction(SIGPIPE, &prev->sigpipe, NULL);
}

static int pump_or_stop(struct grabit_wl_state *s, int timeout_ms) {
	if (grabit_wl_pump(s, timeout_ms) != 0) {
		log_error("recording: lost wayland connection");
		atomic_store_explicit(&grabit_rec_stop, 1, memory_order_relaxed);
		return -1;
	}
	return 0;
}

double rec_capture_loop(struct grabit_wl_state *s, struct rec_layout *layout,
						struct buf_pool *pool, bool cursor,
						struct seg_ctx *sc, struct rec_controls *ctrl) {
	int64_t period_ns = 1000000000 / sc->fps;
	struct ring *ring = sc->ring;
	bool paused = false;
	int64_t seg_start = grabit_now_ns();
	int64_t active_ns = 0;
	int64_t frame_idx = 0;
	int consec_fail = 0;
	bool direct = rec_layout_is_direct(layout);

	while (!atomic_load_explicit(&grabit_rec_stop, memory_order_relaxed)) {
		bool want_pause =
			atomic_load_explicit(&grabit_rec_pause, memory_order_relaxed) != 0;
		if (want_pause != paused) {
			paused = want_pause;
			controls_set_paused(ctrl, paused);
			if (g_tray_pid > 0) kill(g_tray_pid, SIGUSR2);
			if (paused) {
				active_ns += grabit_now_ns() - seg_start;
				seg_finish(sc, s);
				log_info("recording paused");
			} else {
				if (seg_begin(sc) != 0) {
					log_error("recording: could not start a new segment");
					sc->failed = true;
					atomic_store_explicit(&grabit_rec_stop, 1, memory_order_relaxed);
					break;
				}
				seg_start = grabit_now_ns();
				frame_idx = 0;
				log_info("recording resumed");
			}
		}

		int64_t active_now = active_ns + (paused ? 0 : grabit_now_ns() - seg_start);
		controls_tick(ctrl, active_now / 1000000000);

		if (paused) {
			if (pump_or_stop(s, 30) != 0) break;
			continue;
		}

		int64_t deadline = seg_start + frame_idx * period_ns;
		int64_t cur = grabit_now_ns();
		if (cur - deadline > period_ns * 4)
			frame_idx = (cur - seg_start) / period_ns;
		bool interrupted = false;
		while (cur < deadline) {
			if (atomic_load_explicit(&grabit_rec_stop, memory_order_relaxed) ||
				atomic_load_explicit(&grabit_rec_pause, memory_order_relaxed) != 0) {
				interrupted = true;
				break;
			}
			int64_t rem_ms = (deadline - cur) / 1000000;
			if (rem_ms >= 1) {
				if (pump_or_stop(s, (int)rem_ms) != 0) {
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
			cur = grabit_now_ns();
		}
		if (interrupted) continue;

		void *frame_buf = pool_try_acquire(pool);
		if (!frame_buf) {
			ring_record_drop(ring);
			frame_idx++;
			continue;
		}
		int rc;
		if (direct) {
			rc = rec_layout_capture_direct_into(s, layout, cursor, frame_buf,
												layout->dst_stride, layout->dst_h);
		} else {
			rc = rec_layout_capture_compose(s, layout, cursor, frame_buf);
		}
		if (rc != 0) {
			pool_release(pool, frame_buf);
			if (++consec_fail == 1) log_warn("recording: frame capture failed");
			if (consec_fail > 30) {
				log_error("recording: too many consecutive capture failures; stopping");
				atomic_store_explicit(&grabit_rec_stop, 1, memory_order_relaxed);
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
			.pool = pool,
		};
		ring_push(ring, &f);
		frame_idx++;
	}

	if (!paused) active_ns += grabit_now_ns() - seg_start;
	return (double)active_ns / 1e9;
}
