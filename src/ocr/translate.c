// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TRANS_BIN "trans"
#define TRANSLATE_TIMEOUT_MS 20000

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void strip_ansi(char *s) {
	char *w = s;
	const char *r = s;
	while (*r) {
		if (*r == '\x1b' && r[1] == '[') {
			r += 2;
			while (*r && !(*r >= 0x40 && *r <= 0x7e))
				r++;
			if (*r) r++;
			continue;
		}
		*w++ = *r++;
	}
	*w = '\0';
}

static int reap_with_grace(pid_t pid, int *status) {
	int64_t deadline = now_ms() + 2000;
	while (now_ms() < deadline) {
		pid_t r = waitpid(pid, status, WNOHANG);
		if (r == pid) return 0;
		if (r < 0 && errno != EINTR) return -1;
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 50 * 1000000};
		nanosleep(&ts, NULL);
	}
	kill(pid, SIGKILL);
	return grabit_waitpid_intr(pid, status);
}

char *grabit_translate(const char *text, const char *target) {
	if (!text || !target || !target[0]) return NULL;
	if (!grabit_in_path(TRANS_BIN)) {
		log_error("translate: `%s` not found in $PATH (install translate-shell)", TRANS_BIN);
		return NULL;
	}

	struct sigaction prev_pipe, ign = {.sa_handler = SIG_IGN};
	sigemptyset(&ign.sa_mask);
	sigaction(SIGPIPE, &ign, &prev_pipe);

	int in_p[2], out_p[2];
	if (pipe(in_p) != 0) {
		log_error("translate: pipe: %s", strerror(errno));
		sigaction(SIGPIPE, &prev_pipe, NULL);
		return NULL;
	}
	if (pipe(out_p) != 0) {
		log_error("translate: pipe: %s", strerror(errno));
		close(in_p[0]);
		close(in_p[1]);
		sigaction(SIGPIPE, &prev_pipe, NULL);
		return NULL;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("translate: fork: %s", strerror(errno));
		close(in_p[0]);
		close(in_p[1]);
		close(out_p[0]);
		close(out_p[1]);
		sigaction(SIGPIPE, &prev_pipe, NULL);
		return NULL;
	}
	if (pid == 0) {
		dup2(in_p[0], STDIN_FILENO);
		dup2(out_p[1], STDOUT_FILENO);
		close(in_p[0]);
		close(in_p[1]);
		close(out_p[0]);
		close(out_p[1]);
		char *argv[] = {
			(char *)TRANS_BIN,
			(char *)"-b",
			(char *)"-no-warn",
			(char *)"-no-ansi",
			(char *)"-t",
			(char *)target,
			NULL,
		};
		execvp(TRANS_BIN, argv);
		_exit(127);
	}
	close(in_p[0]);
	close(out_p[1]);

	size_t tlen = strlen(text);
	const char *p = text;
	while (tlen > 0) {
		ssize_t w = write(in_p[1], p, tlen);
		if (w < 0) {
			if (errno == EINTR) continue;
			break;
		}
		p += w;
		tlen -= (size_t)w;
	}
	close(in_p[1]);

	struct grabit_buf buf = {0};
	char chunk[4096];
	int64_t deadline = now_ms() + TRANSLATE_TIMEOUT_MS;
	bool timed_out = false;
	for (;;) {
		int64_t remaining = deadline - now_ms();
		if (remaining <= 0) {
			timed_out = true;
			break;
		}
		struct pollfd pfd = {.fd = out_p[0], .events = POLLIN};
		int pr = poll(&pfd, 1, (int)remaining);
		if (pr < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (pr == 0) {
			timed_out = true;
			break;
		}
		ssize_t n = read(out_p[0], chunk, sizeof chunk);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		if (grabit_buf_putn(&buf, chunk, (size_t)n) != 0) {
			grabit_buf_free(&buf);
			close(out_p[0]);
			kill(pid, SIGTERM);
			(void)reap_with_grace(pid, NULL);
			sigaction(SIGPIPE, &prev_pipe, NULL);
			log_error("translate: oom reading output");
			return NULL;
		}
	}
	close(out_p[0]);
	int status = 0;
	if (timed_out) {
		log_error("translate: timed out after %ds; killing `%s`",
				  TRANSLATE_TIMEOUT_MS / 1000, TRANS_BIN);
		kill(pid, SIGTERM);
		(void)reap_with_grace(pid, &status);
	} else if (grabit_waitpid_intr(pid, &status) != 0) {
		grabit_buf_free(&buf);
		sigaction(SIGPIPE, &prev_pipe, NULL);
		return NULL;
	}
	sigaction(SIGPIPE, &prev_pipe, NULL);
	if (timed_out || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		grabit_buf_free(&buf);
		if (!timed_out) log_error("translate: `%s` exited non-zero", TRANS_BIN);
		return NULL;
	}

	if (!buf.data) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}
	buf.data[buf.len] = '\0';
	strip_ansi(buf.data);
	size_t n = strlen(buf.data);
	while (n > 0 && (buf.data[n - 1] == '\n' || buf.data[n - 1] == '\r' ||
					 buf.data[n - 1] == ' ' || buf.data[n - 1] == '\t')) {
		n--;
	}
	buf.data[n] = '\0';
	return buf.data;
}
