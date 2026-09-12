// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_REC_FLAGS_H
#define GRABIT_RECORD_REC_FLAGS_H

#include <stdatomic.h>
#include <sys/types.h>

extern atomic_int *grabit_rec_stop_ptr;
extern atomic_int *grabit_rec_pause_ptr;
extern atomic_int *grabit_rec_abort_ptr;

#define grabit_rec_stop (*grabit_rec_stop_ptr)
#define grabit_rec_pause (*grabit_rec_pause_ptr)
#define grabit_rec_abort (*grabit_rec_abort_ptr)

void loop_init_shared(void);
void loop_set_tray_pid(pid_t pid);

#endif
