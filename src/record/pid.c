// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/pid.h"

#include "log.h"
#include "util/util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_pid_fd = -1;

static const char *pid_file_path(void) {
	static char path[256];
	char dir[200];
	if (grabit_runtime_dir(dir, sizeof dir) != 0) return "/tmp/grabit_recording.pid";
	snprintf(path, sizeof path, "%s/grabit_recording.pid", dir);
	return path;
}

int write_pid_file(void) {
	g_pid_fd = grabit_lock_acquire(pid_file_path());
	return g_pid_fd < 0 ? -1 : 0;
}

void unlink_pid_file(void) {
	if (g_pid_fd >= 0) {
		close(g_pid_fd);
		g_pid_fd = -1;
	}
	unlink(pid_file_path());
}

int stop_running_recording(void) {
	pid_t prev = grabit_lock_owner(pid_file_path());
	if (prev <= 0) return -1;
	log_debug("stopping recording (pid %d)", (int)prev);
	if (kill(prev, SIGINT) != 0) {
		if (errno == EPERM) {
			log_error("recording PID %d not owned by us; cannot signal", (int)prev);
		} else {
			log_error("kill(%d): %s", (int)prev, strerror(errno));
		}
		return -1;
	}
	return 0;
}
