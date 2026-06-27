// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "picker.h"

#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int grabit_pick_path_start(bool dir, pid_t *pid) {
	const char *tool = grabit_in_path("zenity")	   ? "zenity"
					   : grabit_in_path("kdialog") ? "kdialog"
												   : NULL;
	if (!tool) {
		log_warn("picker: no file chooser found (install zenity or kdialog)");
		return -1;
	}
	int p[2];
	if (pipe(p) != 0) return -1;
	pid_t child = fork();
	if (child < 0) {
		close(p[0]);
		close(p[1]);
		return -1;
	}
	if (child == 0) {
		dup2(p[1], STDOUT_FILENO);
		close(p[0]);
		close(p[1]);
		if (strcmp(tool, "zenity") == 0) {
			if (dir)
				execlp("zenity", "zenity", "--file-selection", "--directory", (char *)NULL);
			else
				execlp("zenity", "zenity", "--file-selection", (char *)NULL);
		} else {
			if (dir)
				execlp("kdialog", "kdialog", "--getexistingdirectory", (char *)NULL);
			else
				execlp("kdialog", "kdialog", "--getopenfilename", (char *)NULL);
		}
		_exit(127);
	}
	close(p[1]);
	*pid = child;
	return p[0];
}

int grabit_pick_path_finish(int fd, pid_t pid, char *out, size_t cap) {
	char buf[4096];
	ssize_t n = read(fd, buf, sizeof buf - 1);
	int status = 0;
	grabit_waitpid_intr(pid, &status);
	if (n <= 0) return -1;
	buf[n] = '\0';
	char *nl = strchr(buf, '\n');
	if (nl) *nl = '\0';
	if (!buf[0]) return -1;
	snprintf(out, cap, "%s", buf);
	return 0;
}

int grabit_pick_path(bool dir, char *out, size_t cap) {
	pid_t pid;
	int fd = grabit_pick_path_start(dir, &pid);
	if (fd < 0) return -1;
	int rc = grabit_pick_path_finish(fd, pid, out, cap);
	close(fd);
	return rc;
}
