// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_H
#define GRABIT_PIN_H

struct config;
struct rect;

int pin_spawn(struct config *cfg, const char *path, const struct rect *r);

struct pin_show_opts {
	int dismiss_secs;
	const char *position;	 /* "top-right" (default) | "top-left" | ... | "center" */
	const char *output_name; /* NULL or "" = primary, otherwise output name */
};

int pin_spawn_show(struct config *cfg, const char *path, const struct pin_show_opts *opts);

int pin_grab(void);
int pin_release(void);
int pin_close_all(void);

#endif
