// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/loop.h"

#include "cursor.h"
#include "log.h"
#include "record/compose.h"
#include "record/controls.h"
#include "record/controls_internal.h"
#include "record/ring.h"
#include "record/segments.h"
#include "util/util.h"
#include "wl/wl.h"

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

	struct rect bar_history[12];
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
			bar_idx = (bar_idx + 1) % 12;

			struct rect mask = {0, 0, 0, 0};
			for (int i = 0; i < 12; i++) {
				if (bar_history[i].w > 0) {
					mask = rect_union(mask, bar_history[i]);
				}
			}

			if (mask.w > 0) {
				mask.x -= 16;
				mask.y -= 16;
				mask.w += 32;
				mask.h += 32;

				int32_t lx = i32max(mask.x, region.x);
				int32_t ly = i32max(mask.y, region.y);
				int32_t rx = i32min(mask.x + mask.w, region.x + region.w);
				int32_t ry = i32min(mask.y + mask.h, region.y + region.h);

				if (lx < rx && ly < ry) {
					double scale_x = (double)layout->dst_w / region.w;
					double scale_y = (double)layout->dst_h / region.h;

					int32_t ox = lx - region.x;
					int32_t oy = ly - region.y;
					int32_t ow = rx - lx;
					int32_t oh = ry - ly;

					int32_t pox = (int32_t)(ox * scale_x);
					int32_t poy = (int32_t)(oy * scale_y);
					int32_t prx = (int32_t)((ox + ow) * scale_x + 0.99999);
					int32_t pry = (int32_t)((oy + oh) * scale_y + 0.99999);

					if (pox < 0) pox = 0;
					if (poy < 0) poy = 0;
					if (prx > layout->dst_w) prx = layout->dst_w;
					if (pry > layout->dst_h) pry = layout->dst_h;

					int32_t pow = prx - pox;
					int32_t poh = pry - poy;

					if (pow > 0 && poh > 0) {
						for (int32_t y = 0; y < layout->dst_h; y++) {
							if (y < poy || y >= poy + poh) {
								memcpy((uint8_t *)bg_buf + y * layout->dst_stride,
									   (uint8_t *)frame_buf + y * layout->dst_stride, (size_t)layout->dst_w * 4);
							} else {
								if (pox > 0)
									memcpy((uint8_t *)bg_buf + y * layout->dst_stride,
										   (uint8_t *)frame_buf + y * layout->dst_stride, (size_t)pox * 4);
								if (pox + pow < layout->dst_w)
									memcpy((uint8_t *)bg_buf + y * layout->dst_stride + (size_t)(pox + pow) * 4,
										   (uint8_t *)frame_buf + y * layout->dst_stride + (size_t)(pox + pow) * 4,
										   (size_t)(layout->dst_w - (pox + pow)) * 4);
							}
						}
						for (int32_t y = poy; y < poy + poh; y++) {
							memcpy((uint8_t *)frame_buf + y * layout->dst_stride + (size_t)pox * 4,
								   (uint8_t *)bg_buf + y * layout->dst_stride + (size_t)pox * 4, (size_t)pow * 4);
						}
					}
				} else {
					memcpy(bg_buf, frame_buf, (size_t)layout->dst_stride * (size_t)layout->dst_h);
				}

				if (cursor && ctrl) {
					struct rect crect = controls_cursor_rect(ctrl);
					if (crect.w > 0) {
						int32_t clx = i32max(crect.x, lx);
						int32_t cly = i32max(crect.y, ly);
						int32_t crx = i32min(crect.x + crect.w, rx);
						int32_t cry = i32min(crect.y + crect.h, ry);
						if (clx < crx && cly < cry) {
							bool over_btn = false;
							if (ctrl->cx >= ctrl->bx && ctrl->cx < ctrl->bx + ctrl->bw &&
								ctrl->cy >= ctrl->by && ctrl->cy < ctrl->by + ctrl->bh) {
								int rx = ctrl->cx - ctrl->bx;
								int ry = ctrl->cy - ctrl->by;
								if (ry >= 4 && ry < 36 && rx >= 4 && rx < ctrl->bw - 4) {
									over_btn = true;
								}
							}
							const struct raw_cursor_image *img = over_btn ? &ctrl->raw_cursor_hand : &ctrl->raw_cursor_default;
							if (!img->pixels) img = over_btn ? &ctrl->raw_cursor_default : &ctrl->raw_cursor_hand;
							if (img && img->pixels) {
								double scale_x = (double)layout->dst_w / region.w;
								double scale_y = (double)layout->dst_h / region.h;

								int32_t pcx = (int32_t)(((double)(ctrl->cx - region.x)) * scale_x) - img->hotspot_x;
								int32_t pcy = (int32_t)(((double)(ctrl->cy - region.y)) * scale_y) - img->hotspot_y;

								int32_t pox = (int32_t)(((double)(lx - region.x)) * scale_x);
								int32_t poy = (int32_t)(((double)(ly - region.y)) * scale_y);
								int32_t pow = (int32_t)(((double)(rx - lx)) * scale_x);
								int32_t poh = (int32_t)(((double)(ry - ly)) * scale_y);

								for (int32_t cyi = 0; cyi < img->height; cyi++) {
									int32_t dst_y = pcy + cyi;
									if (dst_y < poy || dst_y >= poy + poh || dst_y < 0 || dst_y >= layout->dst_h) continue;
									for (int32_t cxi = 0; cxi < img->width; cxi++) {
										int32_t dst_x = pcx + cxi;
										if (dst_x < pox || dst_x >= pox + pow || dst_x < 0 || dst_x >= layout->dst_w) continue;

										uint32_t src_px = img->pixels[cyi * img->width + cxi];
										uint8_t sa = (src_px >> 24) & 0xff;
										if (sa == 0) continue;

										uint8_t *dst = (uint8_t *)frame_buf + (size_t)dst_y * (size_t)layout->dst_stride + (size_t)dst_x * 4;
										if (sa == 255) {
											dst[0] = src_px & 0xff;
											dst[1] = (src_px >> 8) & 0xff;
											dst[2] = (src_px >> 16) & 0xff;
											dst[3] = 255;
										} else {
											uint8_t inv_a = 255 - sa;
											dst[0] = (src_px & 0xff) + (dst[0] * inv_a) / 255;
											dst[1] = ((src_px >> 8) & 0xff) + (dst[1] * inv_a) / 255;
											dst[2] = ((src_px >> 16) & 0xff) + (dst[2] * inv_a) / 255;
											dst[3] = sa + (dst[3] * inv_a) / 255;
										}
									}
								}
							}
						}
					}
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
