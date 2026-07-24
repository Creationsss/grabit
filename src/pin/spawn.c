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
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

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
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) return;
	FILE *f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		return;
	}
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
			.summary = "grabit: layer-shell unsupported",
			.body = "compositor lacks layer-shell",
			.log_hint = true,
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
		int prc = gpin_main(img, r != NULL, rcopy, transient, dismiss_secs, te);
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
