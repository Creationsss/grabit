// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_IPC_H
#define GRABIT_WM_IPC_H

#include <stdint.h>

struct json_object;

int64_t gwm_ipc_deadline(int ms);

int gwm_ipc_query(const char *path, const char *req,
				  struct json_object **root_out);

#endif
