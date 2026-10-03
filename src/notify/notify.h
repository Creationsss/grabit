// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_NOTIFY_H
#define GRABIT_NOTIFY_H

#include <stdbool.h>
#include <stdint.h>

struct config;

void notify_init(struct config *cfg, bool silent);

enum notify_urgency {
	NOTIFY_URGENCY_AUTO = 0,
	NOTIFY_LOW,
	NOTIFY_NORMAL,
	NOTIFY_CRITICAL,
};

struct notify_opts {
	const char *summary;
	const char *body;
	const char *icon_path;
	enum notify_urgency urgency;
	bool transient;
	bool force;
	bool log_hint;
	unsigned int *replaces;
};

void notify_send(const struct notify_opts *o);
void notify_finish(void);

#endif
