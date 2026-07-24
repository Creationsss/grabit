// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard_internal.h"

#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

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

#define CLIP_WRITE_STALL_MS 10000
#define CLIP_READY_TIMEOUT_MS 5000

static clip_serve_fn clip_pick(const struct grabit_wl_state *s, const char **name) {
	const char *pref = getenv("GRABIT_CLIPBOARD_BACKEND");
	if (!pref || !pref[0]) pref = "auto";
	if (s->ext_data_control_manager && strcmp(pref, "wlr") != 0) {
		if (name) *name = "ext-data-control";
		return clip_ext_serve;
	}
	if (s->data_control_manager && strcmp(pref, "ext") != 0) {
		if (name) *name = "wlr-data-control";
		return clip_wlr_serve;
	}
	return NULL;
}

void clip_write_all(int fd, const void *bytes, size_t size) {
	signal(SIGPIPE, SIG_IGN);

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0 && (flags & O_NONBLOCK)) fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

	const uint8_t *p = bytes;
	size_t left = size;
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

void clip_signal_ready(int *ready_fd, char status) {
	if (!ready_fd || *ready_fd < 0) return;
	ssize_t w;
	do {
		w = write(*ready_fd, &status, 1);
	} while (w < 0 && errno == EINTR);
	close(*ready_fd);
	*ready_fd = -1;
}

void clip_mute_stderr(void) {
	int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
	if (devnull >= 0) {
		dup2(devnull, STDERR_FILENO);
		close(devnull);
	}
}

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

__attribute__((noreturn)) static void clip_child(const struct clip_payload *pay,
												 int ready_fd) {
	setsid();
	pid_t gc = fork();
	if (gc != 0) _exit(gc < 0 ? 1 : 0);
	log_file_close();
	clip_close_inherited_fds(ready_fd);

	int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		dup2(devnull, STDOUT_FILENO);
		if (devnull > STDERR_FILENO) close(devnull);
	}

	struct grabit_wl_state s;
	if (grabit_wl_probe(&s) != 0) {
		log_error("clipboard: child could not connect to wayland");
		clip_signal_ready(&ready_fd, 0);
		_exit(1);
	}

	int rc;
	const char *name = NULL;
	clip_serve_fn serve = clip_pick(&s, &name);
	if (!s.seat) {
		log_error("clipboard: no wl_seat available");
		rc = -1;
	} else if (!serve) {
		log_error("clipboard: compositor lacks a usable data-control protocol");
		rc = -1;
	} else {
		log_debug("clipboard: using %s backend", name);
		rc = serve(&s, pay, &ready_fd);
	}

	if (rc != 0) clip_signal_ready(&ready_fd, 0);
	grabit_wl_finish(&s);
	_exit(rc == 0 ? 0 : 1);
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
		bool have_dc = clip_pick(&probe, NULL) != NULL;
		bool have_seat = probe.seat != NULL;
		grabit_wl_finish(&probe);
		if (!have_dc) {
			log_error("clipboard: compositor lacks a usable data-control protocol");
			return -1;
		}
		if (!have_seat) {
			log_error("clipboard: no wl_seat available");
			return -1;
		}
	}

	int ready[2];
	bool have_pipe = pipe(ready) == 0;

	struct clip_payload pay = {
		.bytes = bytes,
		.size = size,
		.mimes = mimes,
		.n_mimes = n_mimes,
	};

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
		clip_child(&pay, have_pipe ? ready[1] : -1);
	}

	grabit_waitpid_intr(pid, NULL);

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
