// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "log.h"

#include "util/util.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define LOG_FILE_MAX_BYTES ((size_t)1024 * 1024)
#define LOG_LINE_MAX 2048

static bool g_silent;
static bool g_debug;
static bool g_color;
static int g_file_fd = -1;
static bool g_file_on = true;
static bool g_file_locked;
static size_t g_file_written;

static const char *C_RED = "";
static const char *C_YELLOW = "";
static const char *C_CYAN = "";
static const char *C_RESET = "";

bool log_is_silent(void) {
	return g_silent;
}

void log_init(bool silent, bool debug) {
	g_silent = silent;
	g_debug = debug;

	const char *env = getenv("GRABIT_DEBUG");
	if (env && env[0] && strcmp(env, "0") != 0) {
		g_debug = true;
	}

	const char *lf = getenv("GRABIT_LOG_FILE");
	g_file_locked = lf && lf[0];
	if (g_file_locked) g_file_on = strcmp(lf, "0") != 0;

	g_color = isatty(STDERR_FILENO) && getenv("NO_COLOR") == NULL;
	if (g_color) {
		C_RED = "\033[31m";
		C_YELLOW = "\033[33m";
		C_CYAN = "\033[36m";
		C_RESET = "\033[0m";
	}
}

static const char *log_file_path(void) {
	static char path[256];
	if (path[0]) return path;
	char dir[200];
	if (grabit_runtime_dir(dir, sizeof dir) != 0)
		snprintf(dir, sizeof dir, "/tmp");
	if (strcmp(dir, "/tmp") == 0)
		snprintf(path, sizeof path, "/tmp/grabit-%u.log", (unsigned)getuid());
	else
		snprintf(path, sizeof path, "%s/grabit.log", dir);
	return path;
}

bool log_file_enabled(void) {
	return g_file_on;
}

void log_file_close(void) {
	if (g_file_fd >= 0) close(g_file_fd);
	g_file_fd = -1;
	g_file_written = 0;
}

void log_file_disable(void) {
	if (g_file_locked) return;
	g_file_on = false;
	log_file_close();
}

static int log_file_fd(void) {
	static bool opening;
	if (g_file_fd >= 0 || !g_file_on || opening) return g_file_fd;

	opening = true;
	int fd = open(log_file_path(),
				  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
	opening = false;
	if (fd < 0) {
		g_file_on = false;
		return -1;
	}
	struct stat st;
	g_file_written = (fstat(fd, &st) == 0 && st.st_size > 0) ? (size_t)st.st_size : 0;
	g_file_fd = fd;
	return fd;
}

static void log_file_cap(int fd) {
	if (g_file_written <= LOG_FILE_MAX_BYTES) return;
	if (ftruncate(fd, 0) != 0) return;
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1) return;
	g_file_written = 0;
}

static void emit_file(const char *prefix, const char *msg) {
	int fd = log_file_fd();
	if (fd < 0) return;
	log_file_cap(fd);

	char stamp[32] = "";
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);

	char line[LOG_LINE_MAX + 64];
	int n = snprintf(line, sizeof line, "%s [%d] %s %s\n",
					 stamp, (int)getpid(), prefix, msg);
	if (n <= 0) return;
	size_t len = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1;
	if (grabit_write_all(fd, line, len) == 0) g_file_written += len;
}

static void emit(const char *prefix, const char *color, const char *fmt, va_list ap) {
	char msg[LOG_LINE_MAX];
	int n = vsnprintf(msg, sizeof msg, fmt, ap);
	if (n < 0) return;

	fprintf(stderr, "%s%s%s %s\n", color, prefix, C_RESET, msg);
	emit_file(prefix, msg);
}

void log_debug(const char *fmt, ...) {
	if (!g_debug) return;
	va_list ap;
	va_start(ap, fmt);
	emit("[debug]", C_CYAN, fmt, ap);
	va_end(ap);
}

void log_info(const char *fmt, ...) {
	if (g_silent) return;
	va_list ap;
	va_start(ap, fmt);
	emit("[info]", "", fmt, ap);
	va_end(ap);
}

void log_warn(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[warn]", C_YELLOW, fmt, ap);
	va_end(ap);
}

void log_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[error]", C_RED, fmt, ap);
	va_end(ap);
}

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[error]", C_RED, fmt, ap);
	va_end(ap);
	exit(1);
}
