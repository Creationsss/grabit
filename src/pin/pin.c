// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin.h"

#include "log.h"
#include "notify/notify.h"
#include "pin/pin_state.h"
#include "region/region.h"
#include "util.h"
#include "wl.h"

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

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static volatile sig_atomic_t g_term = 0;
static void on_term(int sig) {
	(void)sig;
	g_term = 1;
}

static void show_pid_path(char *out, size_t cap) {
	char dir[512];
	if (grabit_runtime_dir(dir, sizeof dir) != 0) {
		snprintf(out, cap, "/tmp/grabit-show.pid");
		return;
	}
	snprintf(out, cap, "%s/grabit-show.pid", dir);
}

static void kill_previous_show(void) {
	char path[1024];
	show_pid_path(path, sizeof path);
	FILE *f = fopen(path, "r");
	if (!f) return;
	long pid = 0;
	if (fscanf(f, "%ld", &pid) == 1 && pid > 1 && grabit_is_grabit_process((pid_t)pid)) {
		kill((pid_t)pid, SIGTERM);
	}
	fclose(f);
	unlink(path);
}

static void write_show_pid_self(void) {
	char path[1024];
	show_pid_path(path, sizeof path);
	FILE *f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "%ld\n", (long)getpid());
	fclose(f);
}

static void clear_show_pid_self(void) {
	char path[1024];
	show_pid_path(path, sizeof path);
	FILE *f = fopen(path, "r");
	if (!f) return;
	long pid = 0;
	bool mine = (fscanf(f, "%ld", &pid) == 1 && (pid_t)pid == getpid());
	fclose(f);
	if (mine) unlink(path);
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
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	*mx_out = mx;
	*my_out = my;
}

struct transient_extras {
	const char *position;
	const char *output_name;
	const char *hover_caption;
	const char *click_open;
};

static struct grabit_output *find_output_by_name(struct grabit_wl_state *s, const char *name) {
	if (!name || !name[0]) return NULL;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->name && strcmp(o->name, name) == 0) return o;
	}
	return NULL;
}

static int pin_main(cairo_surface_t *img, bool have_rect, struct rect r,
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
	st.scale = 1;
	st.ipc_fd = -1;
	st.dismiss_timer_fd = -1;
	st.dismiss_secs = dismiss_secs;
	st.transient = transient;
	if (transient && te && te->hover_caption && te->hover_caption[0]) {
		st.hover_caption = te->hover_caption;
		st.input_grabbed = true;
	}
	if (transient && te && te->click_open && te->click_open[0]) {
		st.click_open = te->click_open;
		st.input_grabbed = true;
	}

	struct grabit_output *target = NULL;
	if (have_rect) {
		target = grabit_wl_output_at(&wls, r.x, r.y);
	}
	if (!target && transient && te && te->output_name && te->output_name[0]) {
		target = find_output_by_name(&wls, te->output_name);
		if (!target) {
			log_warn("show: output `%s` not found; falling back to primary",
					 te->output_name);
		}
	}
	if (!target) target = grabit_wl_primary_output(&wls);

	if (target && target->scale > 0) st.scale = target->scale;

	if (have_rect) {
		st.width = r.w;
		st.height = r.h;
	} else {
		st.width = st.img_w / st.scale;
		st.height = st.img_h / st.scale;
		if (st.width <= 0) st.width = st.img_w;
		if (st.height <= 0) st.height = st.img_h;
	}

	if (have_rect && target) {
		st.margin_x = r.x - target->x;
		st.margin_y = r.y - target->y;
		if (st.margin_x < 0) st.margin_x = 0;
		if (st.margin_y < 0) st.margin_y = 0;
	} else if (transient) {
		st.margin_x = PIN_TRANSIENT_MARGIN;
		st.margin_y = PIN_TRANSIENT_MARGIN;
	} else {
		int32_t out_w = target ? target->logical_width : 1920;
		int32_t out_h = target ? target->logical_height : 1080;
		compute_centered_jitter(st.width, st.height, out_w, out_h,
								&st.margin_x, &st.margin_y);
	}

	st.surface = wl_compositor_create_surface(wls.compositor);
	st.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		wls.layer_shell, st.surface,
		target ? target->wl_output : NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "grabit-pin");

	pin_render_attach_layer(&st);
	pin_input_attach(&st);
	pin_input_load_cursors(&st);

	zwlr_layer_surface_v1_set_size(st.layer_surface,
								   (uint32_t)st.width, (uint32_t)st.height);
	uint32_t anchor;
	int32_t mt = 0, mr = 0, mb = 0, ml = 0;
	if (!transient) {
		anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
		mt = st.margin_y;
		ml = st.margin_x;
	} else {
		const char *pos = (te && te->position) ? te->position : "top-right";
		bool is_top = strncmp(pos, "top", 3) == 0;
		bool is_bottom = strncmp(pos, "bottom", 6) == 0;
		bool is_left = strstr(pos, "left") != NULL;
		bool is_right = strstr(pos, "right") != NULL;
		anchor = 0;
		if (is_top) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
		if (is_bottom) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if (is_left) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
		if (is_right) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if (anchor == 0) anchor = 0; /* "center": no anchor */
		mt = is_top ? PIN_TRANSIENT_MARGIN : 0;
		mb = is_bottom ? PIN_TRANSIENT_MARGIN : 0;
		ml = is_left ? PIN_TRANSIENT_MARGIN : 0;
		mr = is_right ? PIN_TRANSIENT_MARGIN : 0;
	}
	zwlr_layer_surface_v1_set_anchor(st.layer_surface, anchor);
	zwlr_layer_surface_v1_set_margin(st.layer_surface, mt, mr, mb, ml);
	zwlr_layer_surface_v1_set_exclusive_zone(st.layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
		st.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	grabit_wl_clear_input_region(wls.compositor, st.surface);

	wl_surface_commit(st.surface);

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
	grabit_wl_callback_drop(&st.drag_frame_cb);
	pin_render_free_buffer(&st);
	pin_input_destroy_cursors(&st);
	if (st.layer_surface) zwlr_layer_surface_v1_destroy(st.layer_surface);
	if (st.surface) wl_surface_destroy(st.surface);
	if (st.relative_pointer) zwp_relative_pointer_v1_destroy(st.relative_pointer);
	if (st.pointer) wl_pointer_release(st.pointer);
	wl_display_roundtrip(wls.display);
	grabit_wl_finish(&wls);
	if (st.image) cairo_surface_destroy(st.image);
	return 0;
}

static int probe_layer_shell(void) {
	struct grabit_wl_state probe;
	if (grabit_wl_probe(&probe) != 0) {
		log_error("pin: cannot connect to wayland");
		return -1;
	}
	bool have_ls = probe.layer_shell != NULL;
	bool have_compositor = probe.compositor != NULL;
	grabit_wl_finish(&probe);
	if (!have_ls) {
		log_error("pin: compositor lacks zwlr_layer_shell_v1");
		return -1;
	}
	if (!have_compositor) {
		log_error("pin: compositor lacks wl_compositor");
		return -1;
	}
	return 0;
}

static int pin_spawn_common(const char *path, const struct rect *r,
							bool transient, int dismiss_secs,
							const struct transient_extras *te) {
	if (!path) return -1;

	if (probe_layer_shell() != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "compositor lacks layer-shell; see terminal for details",
		});
		return -1;
	}

	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		log_error("pin: load %s: %s", path,
				  cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not load image",
			.force = true,
		});
		return -1;
	}

	int sync_p[2] = {-1, -1};
	if (transient && pipe(sync_p) != 0) {
		log_warn("pin: sync pipe failed (%s); pid file may race", strerror(errno));
		sync_p[0] = sync_p[1] = -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("pin: fork: %s", strerror(errno));
		if (sync_p[0] >= 0) close(sync_p[0]);
		if (sync_p[1] >= 0) close(sync_p[1]);
		cairo_surface_destroy(img);
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not fork pin process",
			.force = true,
		});
		return -1;
	}
	if (pid == 0) {
		if (sync_p[0] >= 0) close(sync_p[0]);
		grabit_double_fork_detach();
		if (transient) {
			write_show_pid_self();
			if (sync_p[1] >= 0) {
				char b = '1';
				ssize_t _w = write(sync_p[1], &b, 1);
				(void)_w;
			}
		}
		if (sync_p[1] >= 0) close(sync_p[1]);
		int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}
		struct rect rcopy = r ? *r : (struct rect){0};
		int prc = pin_main(img, r != NULL, rcopy, transient, dismiss_secs, te);
		if (transient) clear_show_pid_self();
		_exit(prc);
	}
	if (sync_p[1] >= 0) close(sync_p[1]);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) break;
	}
	if (sync_p[0] >= 0) {
		char b;
		ssize_t _r = read(sync_p[0], &b, 1);
		(void)_r;
		close(sync_p[0]);
	}
	cairo_surface_destroy(img);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		log_error("pin: detach failed");
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not detach pin process",
			.force = true,
		});
		return -1;
	}
	return 0;
}

int pin_spawn(struct config *cfg, const char *path, const struct rect *r) {
	(void)cfg;
	return pin_spawn_common(path, r, false, 0, NULL);
}

int pin_spawn_show(struct config *cfg, const char *path, const struct pin_show_opts *opts) {
	(void)cfg;
	kill_previous_show();
	struct transient_extras te = {0};
	int dismiss_secs = 0;
	if (opts) {
		dismiss_secs = opts->dismiss_secs;
		te.position = opts->position;
		te.output_name = opts->output_name;
		te.hover_caption = opts->hover_caption;
		te.click_open = opts->click_open;
	}
	return pin_spawn_common(path, NULL, true, dismiss_secs, &te);
}
