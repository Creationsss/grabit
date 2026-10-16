// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_IPC_H
#define GRABIT_WM_IPC_H

#include <stdint.h>

#define WM_IPC_TIMEOUT_MS 2000

struct json_object;

int64_t gwm_ipc_deadline(int ms);

int gwm_ipc_query(const char *path, const char *req,
				  struct json_object **root_out);

int gwm_ipc_open(const char *path);

int gwm_ipc_line(int fd, const char *req, int64_t deadline,
				 struct json_object **root_out);

int gwm_ipc_query_i3(const char *path, uint32_t type,
					 struct json_object **root_out);

#endif
