// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util/util.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

bool grabit_in_path(const char *bin) {
	char tmp[4096];
	return grabit_resolve_in_path(bin, tmp, sizeof tmp) == 0;
}

int grabit_resolve_in_path(const char *bin, char *out, size_t cap) {
	if (!bin || !bin[0] || !out || cap == 0) return -1;
	if (strchr(bin, '/')) {
		if (access(bin, X_OK) != 0) return -1;
		size_t n = strlen(bin);
		if (n + 1 > cap) return -1;
		memcpy(out, bin, n + 1);
		return 0;
	}
	const char *path = getenv("PATH");
	if (!path || !path[0]) return -1;
	const char *p = path;
	while (*p) {
		const char *colon = strchr(p, ':');
		size_t len = colon ? (size_t)(colon - p) : strlen(p);
		if (len > 0) {
			int n = snprintf(out, cap, "%.*s/%s", (int)len, p, bin);
			if (n > 0 && (size_t)n < cap && access(out, X_OK) == 0) {
				return 0;
			}
		}
		if (!colon) break;
		p = colon + 1;
	}
	out[0] = '\0';
	return -1;
}

bool grabit_process_alive(pid_t pid) {
	if (pid <= 0) return false;
	if (kill(pid, 0) == 0) return true;
	return errno != ESRCH;
}

int grabit_self_exe(char *out, size_t cap) {
	if (!out || cap == 0) return -1;
	ssize_t n = readlink("/proc/self/exe", out, cap - 1);
	if (n <= 0 || (size_t)n >= cap - 1) return -1;
	out[n] = '\0';
	static const char gone[] = " (deleted)";
	size_t g = sizeof gone - 1;
	if ((size_t)n > g && strcmp(out + n - g, gone) == 0) out[n - g] = '\0';
	return 0;
}

void grabit_redirect_stdio_devnull(void) {
	int fd = open("/dev/null", O_RDWR);
	if (fd < 0) return;
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	if (fd > STDERR_FILENO) close(fd);
}

int grabit_waitpid_intr(pid_t pid, int *status) {
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) continue;
		return -1;
	}
	return 0;
}

int grabit_reap_grace(pid_t pid, int *status, int grace_ms) {
	int64_t deadline = grabit_now_ms() + grace_ms;
	while (grabit_now_ms() < deadline) {
		pid_t r = waitpid(pid, status, WNOHANG);
		if (r == pid) return 0;
		if (r < 0 && errno != EINTR) return -1;
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 10 * 1000000};
		nanosleep(&ts, NULL);
	}
	kill(pid, SIGKILL);
	return grabit_waitpid_intr(pid, status);
}

void grabit_format_duration(long long secs, char *out, size_t n) {
	if (secs >= 3600)
		snprintf(out, n, "%lld:%02lld:%02lld", secs / 3600, secs / 60 % 60, secs % 60);
	else
		snprintf(out, n, "%lld:%02lld", secs / 60, secs % 60);
}

int grabit_waitpid_intr_stop(pid_t pid, int *status, atomic_int *stop) {
	bool sent_stop = false;
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) {
			if (!sent_stop && stop && atomic_load(stop)) {
				kill(pid, SIGINT);
				sent_stop = true;
			}
			continue;
		}
		return -1;
	}
	return 0;
}

int grabit_spawn_capture(char *const argv[], bool merge_stderr, size_t max_bytes,
						 struct grabit_buf *out, bool *capped, int *status) {
	if (capped) *capped = false;
	int p[2];
	if (pipe(p) != 0) {
		log_error("%s: pipe: %s", argv[0], strerror(errno));
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		log_error("%s: fork: %s", argv[0], strerror(errno));
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		if (merge_stderr && dup2(p[1], STDERR_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		execvp(argv[0], argv);
		_exit(errno == ENOENT ? 127 : 126);
	}
	close(p[1]);

	char chunk[4096];
	for (;;) {
		ssize_t n = read(p[0], chunk, sizeof chunk);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		if (max_bytes > 0 && out->len + (size_t)n > max_bytes) {
			if (capped) *capped = true;
			break;
		}
		if (grabit_buf_putn(out, chunk, (size_t)n) != 0) {
			log_error("%s: oom reading output", argv[0]);
			close(p[0]);
			kill(pid, SIGTERM);
			(void)grabit_waitpid_intr(pid, NULL);
			return -1;
		}
	}
	close(p[0]);

	if (grabit_waitpid_intr(pid, status) != 0) return -1;
	if (out->data && grabit_buf_putc(out, '\0') == 0) out->len--;
	return 0;
}

static int lock_open_current(const char *path) {
	for (int tries = 0; tries < 8; tries++) {
		int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (fd < 0) return -1;
		if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
			close(fd);
			errno = EWOULDBLOCK;
			return -1;
		}
		struct stat fs, ps;
		if (fstat(fd, &fs) == 0 && stat(path, &ps) == 0 &&
			fs.st_dev == ps.st_dev && fs.st_ino == ps.st_ino)
			return fd;
		close(fd);
	}
	errno = EAGAIN;
	return -1;
}

int grabit_lock_acquire(const char *path) {
	int fd = lock_open_current(path);
	if (fd < 0) return -1;
	char buf[32];
	int n = snprintf(buf, sizeof buf, "%d\n", (int)getpid());
	if (ftruncate(fd, 0) != 0 || grabit_write_all(fd, buf, (size_t)n) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

void grabit_lock_release(int *fd, const char *path) {
	if (path && path[0]) unlink(path);
	if (*fd >= 0) close(*fd);
	*fd = -1;
}

pid_t grabit_lock_owner(const char *path) {
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return 0;
	if (flock(fd, LOCK_SH | LOCK_NB) == 0) {
		close(fd);
		return 0;
	}
	char buf[32] = {0};
	ssize_t r = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (r <= 0) return 0;
	long v = strtol(buf, NULL, 10);
	return v > 1 ? (pid_t)v : 0;
}

void grabit_install_signal_handler(int sig, void (*handler)(int)) {
	struct sigaction sa = {0};
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

void grabit_ignore_signal(int sig) {
	struct sigaction sa = {0};
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

void grabit_quiet_stdio(void) {
	int null = open("/dev/null", O_RDWR);
	if (null < 0) return;
	for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++)
		if (fd != null) dup2(null, fd);
	if (null > STDERR_FILENO) close(null);
}

int grabit_xdg_open(const char *target) {
	if (!grabit_in_path("xdg-open")) return -2;
	int rc = grabit_fork_detached();
	if (rc == 0) {
		grabit_quiet_stdio();
		execlp("xdg-open", "xdg-open", target, (char *)NULL);
		_exit(127);
	}
	return rc < 0 ? -1 : 0;
}

int grabit_fork_detached(void) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		pid_t gp = fork();
		if (gp < 0) _exit(2);
		if (gp != 0) _exit(0);
		setsid();
		return 0;
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 1;
}
