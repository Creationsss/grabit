// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PICKER_H
#define GRABIT_PICKER_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

int grabit_pick_path(bool dir, char *out, size_t cap);

int grabit_pick_path_start(bool dir, pid_t *pid);
int grabit_pick_path_finish(int fd, pid_t pid, char *out, size_t cap);

#endif
