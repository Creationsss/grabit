// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WM_INTERNAL_H
#define GRABIT_WM_INTERNAL_H

enum wm_kind {
	WM_NONE = 0,
	WM_HYPRLAND,
	WM_NIRI,
};

enum wm_kind grabit_wm_detect(void);

#endif
