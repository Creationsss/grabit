// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard_internal.h"

#include "log.h"
#include "wl.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-data-control-unstable-v1-client-protocol.h"

#define CLIP_WRITE_STALL_MS 10000
#define CLIP_READY_TIMEOUT_MS 5000

struct clip_state {
	const void *bytes;
	size_t size;
	bool cancelled;
};

static void source_send(void *data, struct zwlr_data_control_source_v1 *src,
						const char *mime, int32_t fd) {
	(void)src;
	(void)mime;
	struct clip_state *st = data;

	signal(SIGPIPE, SIG_IGN);

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0 && (flags & O_NONBLOCK)) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

	const uint8_t *p = st->bytes;
	size_t left = st->size;
	while (left > 0) {
		ssize_t w = write(fd, p, left);
		if (w < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				struct pollfd pfd = {.fd = fd, .events = POLLOUT};
				int pr = poll(&pfd, 1, CLIP_WRITE_STALL_MS);
				if (pr < 0) {
					if (errno == EINTR) continue;
					break;
				}
				if (pr == 0) break;
				continue;
			}
			break;
		}
		if (w == 0) break;
		p += w;
		left -= (size_t)w;
	}
	close(fd);
}

static void source_cancelled(void *data, struct zwlr_data_control_source_v1 *src) {
	(void)src;
	struct clip_state *st = data;
	st->cancelled = true;
}

static const struct zwlr_data_control_source_v1_listener source_listener_g = {
	.send = source_send,
	.cancelled = source_cancelled,
};

static void clip_close_inherited_fds(int keep_fd) {
	DIR *d = opendir("/proc/self/fd");
	if (d) {
		int dfd = dirfd(d);
		struct dirent *e;
		while ((e = readdir(d))) {
			int fd = atoi(e->d_name);
			if (fd > STDERR_FILENO && fd != dfd && fd != keep_fd) close(fd);
		}
		closedir(d);
		return;
	}
	long max = sysconf(_SC_OPEN_MAX);
	if (max < 0 || max > 4096) max = 4096;
	for (int fd = STDERR_FILENO + 1; fd < max; fd++)
		if (fd != keep_fd) close(fd);
}

static void clip_signal_ready(int ready_fd, char status) {
	if (ready_fd < 0) return;
	ssize_t w;
	do {
		w = write(ready_fd, &status, 1);
	} while (w < 0 && errno == EINTR);
	close(ready_fd);
}

__attribute__((noreturn)) static void clip_child(const void *bytes, size_t size,
												 const char *const *mimes, size_t n_mimes,
												 int ready_fd) {
	setsid();
	clip_close_inherited_fds(ready_fd);

	int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		dup2(devnull, STDOUT_FILENO);
		if (devnull > STDERR_FILENO) close(devnull);
	}

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		log_error("clipboard: child wayland init failed");
		clip_signal_ready(ready_fd, 0);
		_exit(1);
	}
	if (!s.data_control_manager || !s.seat) {
		log_error("clipboard: compositor lacks zwlr_data_control_manager_v1 or wl_seat");
		clip_signal_ready(ready_fd, 0);
		grabit_wl_finish(&s);
		_exit(1);
	}

	struct zwlr_data_control_device_v1 *dev =
		zwlr_data_control_manager_v1_get_data_device(s.data_control_manager, s.seat);

	struct zwlr_data_control_source_v1 *src =
		zwlr_data_control_manager_v1_create_data_source(s.data_control_manager);

	struct clip_state st = {.bytes = bytes, .size = size};
	zwlr_data_control_source_v1_add_listener(src, &source_listener_g, &st);

	for (size_t i = 0; i < n_mimes; i++) {
		zwlr_data_control_source_v1_offer(src, mimes[i]);
	}

	zwlr_data_control_device_v1_set_selection(dev, src);
	if (wl_display_roundtrip(s.display) < 0) {
		clip_signal_ready(ready_fd, 0);
		grabit_wl_finish(&s);
		_exit(1);
	}
	clip_signal_ready(ready_fd, 1);

	int devnull2 = open("/dev/null", O_WRONLY | O_CLOEXEC);
	if (devnull2 >= 0) {
		dup2(devnull2, STDERR_FILENO);
		close(devnull2);
	}

	while (!st.cancelled) {
		if (wl_display_dispatch(s.display) < 0) break;
	}

	zwlr_data_control_source_v1_destroy(src);
	zwlr_data_control_device_v1_destroy(dev);
	grabit_wl_finish(&s);
	_exit(0);
}

int clipboard_send_bytes(const void *bytes, size_t size,
						 const char *const *mimes, size_t n_mimes) {
	if (!bytes || n_mimes == 0) return -1;

	{
		struct grabit_wl_state probe;
		if (grabit_wl_probe(&probe) != 0) {
			log_error("clipboard: cannot connect to wayland");
			return -1;
		}
		bool have_dc = probe.data_control_manager != NULL;
		bool have_seat = probe.seat != NULL;
		grabit_wl_finish(&probe);
		if (!have_dc) {
			log_error("clipboard: compositor lacks zwlr_data_control_manager_v1");
			return -1;
		}
		if (!have_seat) {
			log_error("clipboard: no wl_seat available");
			return -1;
		}
	}

	int ready[2];
	bool have_pipe = pipe(ready) == 0;

	pid_t pid = fork();
	if (pid < 0) {
		log_error("fork: %s", strerror(errno));
		if (have_pipe) {
			close(ready[0]);
			close(ready[1]);
		}
		return -1;
	}
	if (pid == 0) {
		if (have_pipe) close(ready[0]);
		clip_child(bytes, size, mimes, n_mimes, have_pipe ? ready[1] : -1);
	}

	if (!have_pipe) return 0;

	close(ready[1]);
	struct pollfd pfd = {.fd = ready[0], .events = POLLIN};
	int pr;
	do {
		pr = poll(&pfd, 1, CLIP_READY_TIMEOUT_MS);
	} while (pr < 0 && errno == EINTR);

	char status = 0;
	if (pr > 0) {
		ssize_t r;
		do {
			r = read(ready[0], &status, 1);
		} while (r < 0 && errno == EINTR);
		if (r != 1) status = 0;
	}
	close(ready[0]);

	if (status != 1) {
		log_error("clipboard: child did not acquire the selection");
		return -1;
	}
	return 0;
}
