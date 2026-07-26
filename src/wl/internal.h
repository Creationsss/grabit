// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_INTERNAL_H
#define GRABIT_WL_INTERNAL_H

#include <wayland-client.h>

#include "xdg-output-unstable-v1-client-protocol.h"

struct grabit_wl_state;
struct grabit_output;

extern const struct wl_output_listener grabit_wl_output_listener;
extern const struct zxdg_output_v1_listener grabit_xdg_output_listener;
extern const struct wl_registry_listener grabit_wl_registry_listener;

int gwl_outputs_push(struct grabit_wl_state *s, struct grabit_output *o);
void gwl_output_attach_xdg(struct grabit_wl_state *s, struct grabit_output *o);
void gwl_output_finalize(struct grabit_output *o);

#endif
