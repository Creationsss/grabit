// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_KWIN_INTERNAL_H
#define GRABIT_CAPTURE_KWIN_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dbus/dbus.h>

#define KWIN_MAX_IMAGE_BYTES ((size_t)512 * 1024 * 1024)

struct kwin_meta {
	uint32_t width, height, stride, format;
};

bool gkwin_qimage_to_shm(uint32_t qfmt, uint32_t *out_shm);
bool gkwin_parse_meta(DBusMessage *reply, struct kwin_meta *m);

#endif
