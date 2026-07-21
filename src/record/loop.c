// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/loop.h"

#include "log.h"
#include "record/compose.h"
#include "record/controls.h"
#include "record/ring.h"
#include "record/segments.h"
#include "util.h"
#include "wl.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>

atomic_int grabit_rec_stop;
atomic_int grabit_rec_pause;
atomic_int grabit_rec_abort;

static void on_stop_signal(int sig) {
	(void)sig;
	atomic_store(&grabit_rec_stop, 1);
}

static void on_pause_signal(int sig) {
	(void)sig;
	atomic_fetch_xor(&grabit_rec_pause, 1);
}

void record_signals_install(struct prev_sigs *prev) {
	struct sigaction sa = {0};
	sa.sa_handler = on_stop_signal;
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

void record_signals_restore(const struct prev_sigs *prev) {
	sigaction(SIGINT, &prev->sigint, NULL);
	sigaction(SIGTERM, &prev->sigterm, NULL);
	sigaction(SIGHUP, &prev->sighup, NULL);
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
						struct seg_ctx *sc, struct rec_controls *ctrl,
						struct rect region, void *bg_buf) {
	int64_t period_ns = 1000000000 / sc->fps;
	struct ring *ring = sc->ring;
	bool paused = false;
	int64_t seg_start = grabit_now_ns();
	int64_t active_ns = 0;
	int64_t frame_idx = 0;
	int consec_fail = 0;
	bool direct = rec_layout_is_direct(layout);

	struct rect bar_history[5];
	memset(bar_history, 0, sizeof(bar_history));
	int bar_idx = 0;

	while (!atomic_load_explicit(&grabit_rec_stop, memory_order_relaxed)) {
		bool want_pause =
			atomic_load_explicit(&grabit_rec_pause, memory_order_relaxed) != 0;
		if (want_pause != paused) {
			paused = want_pause;
			controls_set_paused(ctrl, paused);
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

		if (ctrl && bg_buf) {

			bar_history[bar_idx] = controls_bar_rect(ctrl);
			bar_idx = (bar_idx + 1) % 5;

			int32_t min_x = INT32_MAX, min_y = INT32_MAX;
			int32_t max_r = 0, max_b = 0;
			for (int i = 0; i < 5; i++) {
				if (bar_history[i].w > 0) {
					if (bar_history[i].x < min_x) min_x = bar_history[i].x;
					if (bar_history[i].y < min_y) min_y = bar_history[i].y;
					if (bar_history[i].x + bar_history[i].w > max_r) max_r = bar_history[i].x + bar_history[i].w;
					if (bar_history[i].y + bar_history[i].h > max_b) max_b = bar_history[i].y + bar_history[i].h;
				}
			}

			if (min_x < INT32_MAX) {
				struct rect mask = {min_x, min_y, max_r - min_x, max_b - min_y};
				int32_t lx = i32max(mask.x, region.x);
				int32_t ly = i32max(mask.y, region.y);
				int32_t rx = i32min(mask.x + mask.w, region.x + region.w);
				int32_t ry = i32min(mask.y + mask.h, region.y + region.h);

				if (lx < rx && ly < ry) {
					int32_t ox = lx - region.x;
					int32_t oy = ly - region.y;
					int32_t ow = rx - lx;
					int32_t oh = ry - ly;

					if (ox >= 0 && oy >= 0 && ox + ow <= layout->dst_w && oy + oh <= layout->dst_h) {
						for (int32_t y = 0; y < layout->dst_h; y++) {
							if (y < oy || y >= oy + oh) {
								memcpy((uint8_t *)bg_buf + y * layout->dst_stride,
									   (uint8_t *)frame_buf + y * layout->dst_stride, (size_t)layout->dst_w * 4);
							} else {
								if (ox > 0)
									memcpy((uint8_t *)bg_buf + y * layout->dst_stride,
										   (uint8_t *)frame_buf + y * layout->dst_stride, (size_t)ox * 4);
								if (ox + ow < layout->dst_w)
									memcpy((uint8_t *)bg_buf + y * layout->dst_stride + (size_t)(ox + ow) * 4,
										   (uint8_t *)frame_buf + y * layout->dst_stride + (size_t)(ox + ow) * 4,
										   (size_t)(layout->dst_w - (ox + ow)) * 4);
							}
						}
						for (int32_t y = oy; y < oy + oh; y++) {
							memcpy((uint8_t *)frame_buf + y * layout->dst_stride + (size_t)ox * 4,
								   (uint8_t *)bg_buf + y * layout->dst_stride + (size_t)ox * 4, (size_t)ow * 4);
						}
					}
				} else {
					memcpy(bg_buf, frame_buf, (size_t)layout->dst_stride * (size_t)layout->dst_h);
				}
			}
		}

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
