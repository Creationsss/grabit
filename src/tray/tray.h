// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_H
#define GRABIT_TRAY_H

#include <sys/types.h>

struct tray_state;

struct tray_state *tray_start(void);
void tray_stop(struct tray_state *t);
pid_t tray_get_pid(const struct tray_state *t);

#endif
