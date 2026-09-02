// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin.h"

#include "log.h"
#include "notify/notify.h"
#include "pin/pin_state.h"
#include "region/region.h"
#include "util/util.h"
#include "wl/wl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

static volatile sig_atomic_t g_term = 0;
static void on_term(int sig) {
	(void)sig;
	g_term = 1;
}

static void compute_centered_jitter(int32_t img_w, int32_t img_h,
									int32_t out_w, int32_t out_h,
									int32_t *mx_out, int32_t *my_out) {
	int32_t cx = (out_w - img_w) / 2;
	int32_t cy = (out_h - img_h) / 2;
	uint32_t seed = (uint32_t)getpid() ^ (uint32_t)time(NULL);
	int32_t jx = (int32_t)((seed * 2654435761u) % 241u) - 120;
	int32_t jy = (int32_t)((seed * 40503u) % 241u) - 120;
	int32_t mx = cx + jx;
	int32_t my = cy + jy;
	int32_t max_x = out_w - img_w, max_y = out_h - img_h;
	if (max_x < 0) max_x = 0;
	if (max_y < 0) max_y = 0;
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	if (mx > max_x) mx = max_x;
	if (my > max_y) my = max_y;
	*mx_out = mx;
	*my_out = my;
}

static void place_transient(struct pin_state *st, const struct grabit_output *o,
							const char *pos) {
	if (!pos || !pos[0]) pos = "top-right";
	bool is_top = strncmp(pos, "top", 3) == 0;
	bool is_bottom = strncmp(pos, "bottom", 6) == 0;
	bool is_left = strstr(pos, "left") != NULL;
	bool is_right = strstr(pos, "right") != NULL;
	int32_t m = PIN_TRANSIENT_MARGIN;

	if (is_left)
		st->px = o->x + m;
	else if (is_right)
		st->px = o->x + o->logical_width - st->width - m;
	else
		st->px = o->x + (o->logical_width - st->width) / 2;

	if (is_top)
		st->py = o->y + m;
	else if (is_bottom)
		st->py = o->y + o->logical_height - st->height - m;
	else
		st->py = o->y + (o->logical_height - st->height) / 2;
}

int gpin_main(cairo_surface_t *img, bool have_rect, struct rect r,
			  bool transient, int dismiss_secs,
			  const struct transient_extras *te) {
	grabit_install_signal_handler(SIGTERM, on_term);
	grabit_install_signal_handler(SIGINT, on_term);
	grabit_install_signal_handler(SIGHUP, on_term);

	struct grabit_wl_state wls;
	if (grabit_wl_init(&wls) != 0) return 1;
	if (!wls.layer_shell || !wls.compositor) {
		grabit_wl_finish(&wls);
		return 1;
	}
	if (wls.n_outputs == 0) {
		log_error("pin: no outputs available; cannot show card");
		notify_send(&(struct notify_opts){
			.summary = "grabit: show failed",
			.body = "no monitor is connected",
			.force = true,
		});
		grabit_wl_finish(&wls);
		return 1;
	}

	struct pin_state st = {0};
	st.wls = &wls;
	st.image = img;
	st.img_w = cairo_image_surface_get_width(img);
	st.img_h = cairo_image_surface_get_height(img);
	st.cursor_scale = 1;
	st.ipc_fd = -1;
	st.ipc_lock_fd = -1;
	st.dismiss_timer_fd = -1;
	st.dismiss_secs = dismiss_secs;
	st.transient = transient;
	if (transient && te && te->hover_caption && te->hover_caption[0]) {
		st.hover_caption = te->hover_caption;
		st.clickable = true;
	}
	if (transient && te && te->click_open && te->click_open[0]) {
		st.click_open = te->click_open;
		st.clickable = true;
	}

	struct grabit_output *target = NULL;
	if (have_rect) {
		target = grabit_wl_output_at(&wls, r.x, r.y);
	}
	if (!target && transient && te && te->output_name && te->output_name[0]) {
		target = grabit_wl_output_by_name(&wls, te->output_name);
		if (!target) {
			log_warn("show: output `%s` not found; falling back to primary",
					 te->output_name);
		}
	}
	if (!target) target = grabit_wl_primary_output(&wls);

	int32_t tscale = target->scale > 0 ? target->scale : 1;
	if (have_rect) {
		st.width = r.w;
		st.height = r.h;
	} else {
		st.width = st.img_w / tscale;
		st.height = st.img_h / tscale;
		if (st.width <= 0) st.width = st.img_w;
		if (st.height <= 0) st.height = st.img_h;
	}

	if (have_rect) {
		st.px = r.x;
		st.py = r.y;
	} else if (transient) {
		place_transient(&st, target, te ? te->position : NULL);
	} else {
		int32_t mx = 0, my = 0;
		compute_centered_jitter(st.width, st.height, target->logical_width,
								target->logical_height, &mx, &my);
		st.px = target->x + mx;
		st.py = target->y + my;
	}

	st.target = target;
	st.outputs_serial = wls.outputs_serial;
	pin_sync_outputs(&st);
	if (st.n == 0) {
		grabit_wl_finish(&wls);
		return 1;
	}

	pin_input_attach(&st);
	pin_cursor_load(&st);

	if (!transient) {
		if (pin_ipc_open(&st) != 0) {
			log_warn("pin: ipc disabled (grab/release won't reach this pin)");
		}
	} else if (dismiss_secs > 0) {
		st.dismiss_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
		if (st.dismiss_timer_fd < 0) {
			log_warn("pin: timerfd_create failed (%s); card will stay until replaced",
					 strerror(errno));
		} else {
			struct itimerspec it = {
				.it_value = {.tv_sec = dismiss_secs, .tv_nsec = 0},
			};
			timerfd_settime(st.dismiss_timer_fd, 0, &it, NULL);
		}
	}

	while (!st.finished && !g_term) {
		while (wl_display_prepare_read(wls.display) != 0) {
			if (wl_display_dispatch_pending(wls.display) < 0) goto out;
		}
		if (wl_display_flush(wls.display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(wls.display);
			goto out;
		}

		struct pollfd pfds[3];
		int nfds = 0;
		pfds[nfds].fd = wl_display_get_fd(wls.display);
		pfds[nfds].events = POLLIN;
		nfds++;
		int ipc_idx = -1, timer_idx = -1;
		if (st.ipc_fd >= 0) {
			pfds[nfds].fd = st.ipc_fd;
			pfds[nfds].events = POLLIN;
			ipc_idx = nfds++;
		}
		if (st.dismiss_timer_fd >= 0) {
			pfds[nfds].fd = st.dismiss_timer_fd;
			pfds[nfds].events = POLLIN;
			timer_idx = nfds++;
		}

		int pr = poll(pfds, (nfds_t)nfds, -1);
		if (pr < 0) {
			wl_display_cancel_read(wls.display);
			if (errno == EINTR) continue;
			break;
		}

		if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			wl_display_cancel_read(wls.display);
			log_warn("pin: lost wayland connection");
			goto out;
		}

		if (pfds[0].revents & POLLIN) {
			if (wl_display_read_events(wls.display) < 0) goto out;
		} else {
			wl_display_cancel_read(wls.display);
		}
		if (wl_display_dispatch_pending(wls.display) < 0) goto out;

		if (st.outputs_serial != wls.outputs_serial) {
			st.outputs_serial = wls.outputs_serial;
			pin_sync_outputs(&st);
		}

		if (ipc_idx >= 0 && (pfds[ipc_idx].revents & POLLIN)) {
			pin_ipc_handle(&st);
		}
		if (timer_idx >= 0 && (pfds[timer_idx].revents & POLLIN)) {
			uint64_t expirations = 0;
			ssize_t rn = read(st.dismiss_timer_fd, &expirations, sizeof expirations);
			(void)rn;
			st.finished = true;
		}
	}

out:
	if (st.dismiss_timer_fd >= 0) close(st.dismiss_timer_fd);
	pin_ipc_close(&st);
	pin_cursor_destroy(&st);
	pin_outputs_finish(&st);
	if (st.pointer) wl_pointer_release(st.pointer);
	if (st.touch) wl_touch_release(st.touch);
	wl_display_roundtrip(wls.display);
	grabit_wl_finish(&wls);
	if (st.image) cairo_surface_destroy(st.image);
	return 0;
}
