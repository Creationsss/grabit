// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_SNI_H
#define GRABIT_TRAY_SNI_H

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

struct tray_menu;

struct sni_pixmap {
	int w;
	int h;
	unsigned char *argb;
};

struct sni_cfg {
	const char *icon_name;
	const struct sni_pixmap *pixmaps;
	size_t n_pixmaps;
	const char *tooltip_body;
	bool persist;
	void (*on_activate)(void);
	bool (*refresh_icon)(void);
	int watch_fd;
	const struct tray_menu *menu;
};

int sni_run(volatile sig_atomic_t *stop, volatile sig_atomic_t *layout_update,
			const struct sni_cfg *cfg);

#endif
