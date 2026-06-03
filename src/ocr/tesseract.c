// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool path_looks_like_game(const char *path) {
	if (!path) return false;
	return strstr(path, "/games/") != NULL ||
		   strstr(path, "/Sauerbraten/") != NULL ||
		   strstr(path, "/tesseract-engine") != NULL;
}

int grabit_ocr_check(const char *bin) {
	if (!bin || !bin[0]) return -1;

	char resolved[4096];
	if (grabit_resolve_in_path(bin, resolved, sizeof resolved) != 0) return -1;
	if (path_looks_like_game(resolved)) {
		log_debug("ocr: rejecting %s (looks like Tesseract FPS game path)", resolved);
		return -1;
	}

	int p[2];
	if (pipe(p) != 0) return -1;

	pid_t pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		if (dup2(p[1], STDERR_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		char *argv[] = {(char *)bin, (char *)"--version", NULL};
		execvp(bin, argv);
		_exit(errno == ENOENT ? 127 : 126);
	}
	close(p[1]);

	char out[1024];
	size_t off = 0;
	for (;;) {
		ssize_t n = read(p[0], out + off, sizeof out - 1 - off);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		off += (size_t)n;
		if (off >= sizeof out - 1) break;
	}
	out[off] = '\0';
	close(p[0]);

	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	if (!WIFEXITED(status)) {
		log_debug("ocr: tesseract --version killed by signal %d", WTERMSIG(status));
		return -1;
	}
	int code = WEXITSTATUS(status);
	log_debug("ocr: %s --version exit=%d output=%.80s", bin, code, out);
	if (code != 0) return -1;
	if (!strstr(out, "tesseract") && !strstr(out, "Tesseract")) {
		log_error("ocr: `%s` --version did not look like Tesseract OCR", bin);
		return -1;
	}
	if (!strstr(out, "leptonica")) {
		log_error("ocr: `%s` is not Tesseract OCR (no leptonica in --version)", bin);
		log_error("  if this is the Tesseract FPS game, set ocr.tesseract to the OCR binary:");
		log_error("    grabit set ocr.tesseract /usr/bin/tesseract-ocr  # or the right path");
		return -1;
	}
	return 0;
}

int grabit_ocr_has_lang(const char *bin, const char *lang) {
	if (!bin || !bin[0] || !lang || !lang[0]) return -1;

	int p[2];
	if (pipe(p) != 0) return -1;

	pid_t pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		if (dup2(p[1], STDERR_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		char *argv[] = {(char *)bin, (char *)"--list-langs", NULL};
		execvp(bin, argv);
		_exit(127);
	}
	close(p[1]);

	char out[4096];
	size_t off = 0;
	for (;;) {
		ssize_t n = read(p[0], out + off, sizeof out - 1 - off);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		off += (size_t)n;
		if (off >= sizeof out - 1) break;
	}
	out[off] = '\0';
	close(p[0]);

	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;

	size_t lang_len = strlen(lang);
	char *p_line = out;
	while (p_line && *p_line) {
		char *nl = strchr(p_line, '\n');
		size_t len = nl ? (size_t)(nl - p_line) : strlen(p_line);
		while (len > 0 && (p_line[len - 1] == '\r' || p_line[len - 1] == ' ' ||
						   p_line[len - 1] == '\t'))
			len--;
		if (len == lang_len && strncmp(p_line, lang, lang_len) == 0) return 0;
		if (!nl) break;
		p_line = nl + 1;
	}
	return -1;
}

char *grabit_ocr_run(const char *bin, const char *path) {
	if (!bin || !bin[0] || !path || !path[0]) return NULL;

	int p[2];
	if (pipe(p) != 0) {
		log_error("ocr: pipe: %s", strerror(errno));
		return NULL;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("ocr: fork: %s", strerror(errno));
		close(p[0]);
		close(p[1]);
		return NULL;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		char *argv[] = {(char *)bin, (char *)path, (char *)"stdout",
						(char *)"-l", (char *)"eng", NULL};
		execvp(bin, argv);
		_exit(127);
	}
	close(p[1]);

	struct grabit_buf buf = {0};
	char chunk[4096];
	for (;;) {
		ssize_t n = read(p[0], chunk, sizeof chunk);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		if (grabit_buf_putn(&buf, chunk, (size_t)n) != 0) {
			grabit_buf_free(&buf);
			close(p[0]);
			kill(pid, SIGTERM);
			(void)grabit_waitpid_intr(pid, NULL);
			log_error("ocr: oom reading tesseract output");
			return NULL;
		}
	}
	close(p[0]);

	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) {
		grabit_buf_free(&buf);
		return NULL;
	}
	if (!WIFEXITED(status)) {
		grabit_buf_free(&buf);
		log_error("ocr: tesseract killed by signal %d", WTERMSIG(status));
		return NULL;
	}
	int code = WEXITSTATUS(status);
	if (code != 0) {
		grabit_buf_free(&buf);
		if (code == 127) {
			log_error("ocr: tesseract not found in $PATH");
		} else {
			log_error("ocr: tesseract exited with code %d (see stderr above)", code);
		}
		return NULL;
	}

	if (!buf.data) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}

	size_t n = buf.len;
	while (n > 0 && (buf.data[n - 1] == '\n' || buf.data[n - 1] == '\r' ||
					 buf.data[n - 1] == ' ' || buf.data[n - 1] == '\t')) {
		n--;
	}
	buf.data[n] = '\0';
	return buf.data;
}
