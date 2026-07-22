// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/spawn.h"

#include "util/util.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int plugin_run_in(const char *cwd, char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (cwd && chdir(cwd) != 0) _exit(127);
		execvp(argv[0], argv);
		_exit(127);
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}
