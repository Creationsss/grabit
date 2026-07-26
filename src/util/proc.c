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
#include <sys/wait.h>
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
	return 0;
}

void grabit_redirect_stdio_devnull(void) {
	int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
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

bool grabit_is_grabit_process(pid_t pid) {
	if (pid <= 0) return false;
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char comm[32] = {0};
	bool ok = fgets(comm, sizeof comm, f) != NULL;
	fclose(f);
	if (!ok) return false;
	char *nl = strchr(comm, '\n');
	if (nl) *nl = '\0';
	const char *base = comm;
	if (base[0] == '.') base++;
	if (strncmp(base, "grabit", 6) != 0) return false;
	return base[6] == '\0' || base[6] == '-';
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

void grabit_double_fork_detach(void) {
	pid_t gp = fork();
	if (gp < 0) _exit(2);
	if (gp != 0) _exit(0);
	setsid();
}
