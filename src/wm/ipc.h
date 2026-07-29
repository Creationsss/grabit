// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_IPC_H
#define GRABIT_WM_IPC_H

#include <stdint.h>

#include "util/util.h"

struct json_object;

int64_t gwm_ipc_deadline(int ms);

struct gwm_lines {
	int fd;
	struct grabit_buf buf;
};

int gwm_ipc_stream_open(const char *path, const char *req,
						struct gwm_lines *out, int64_t deadline);
char *gwm_ipc_readline(struct gwm_lines *r, int64_t deadline);
void gwm_ipc_stream_close(struct gwm_lines *r);

int gwm_ipc_query(const char *path, const char *req,
				  struct json_object **root_out);

#endif
