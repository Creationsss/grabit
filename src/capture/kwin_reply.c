// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/kwin_internal.h"

#include "log.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dbus/dbus.h>
#include <wayland-client.h>

enum {
	QIMAGE_RGB32 = 4,
	QIMAGE_ARGB32 = 5,
	QIMAGE_ARGB32_PREMUL = 6,
	QIMAGE_RGBX8888 = 24,
	QIMAGE_RGBA8888 = 25,
	QIMAGE_RGBA8888_PREMUL = 26,
};

bool gkwin_qimage_to_shm(uint32_t qfmt, uint32_t *out_shm) {
	switch (qfmt) {
	case QIMAGE_RGB32:
		*out_shm = WL_SHM_FORMAT_XRGB8888;
		return true;
	case QIMAGE_ARGB32:
	case QIMAGE_ARGB32_PREMUL:
		*out_shm = WL_SHM_FORMAT_ARGB8888;
		return true;
	case QIMAGE_RGBX8888:
		*out_shm = WL_SHM_FORMAT_XBGR8888;
		return true;
	case QIMAGE_RGBA8888:
	case QIMAGE_RGBA8888_PREMUL:
		*out_shm = WL_SHM_FORMAT_ABGR8888;
		return true;
	default:
		return false;
	}
}

bool gkwin_parse_meta(DBusMessage *reply, struct kwin_meta *m) {
	DBusMessageIter args, dict;
	if (!dbus_message_iter_init(reply, &args) ||
		dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
		log_error("kwin-screenshot: reply is not a{sv}");
		return false;
	}
	dbus_message_iter_recurse(&args, &dict);

	bool got_type = false;
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, var;
		dbus_message_iter_recurse(&dict, &entry);

		const char *key = NULL;
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&entry, &key);
			dbus_message_iter_next(&entry);
		}
		if (key && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
			dbus_message_iter_recurse(&entry, &var);
			int t = dbus_message_iter_get_arg_type(&var);
			if (t == DBUS_TYPE_UINT32 || t == DBUS_TYPE_INT32) {
				dbus_uint32_t v = 0;
				dbus_message_iter_get_basic(&var, &v);
				if (strcmp(key, "width") == 0)
					m->width = v;
				else if (strcmp(key, "height") == 0)
					m->height = v;
				else if (strcmp(key, "stride") == 0)
					m->stride = v;
				else if (strcmp(key, "format") == 0)
					m->format = v;
			} else if (t == DBUS_TYPE_STRING && strcmp(key, "type") == 0) {
				const char *v = NULL;
				dbus_message_iter_get_basic(&var, &v);
				got_type = v && strcmp(v, "raw") == 0;
			}
		}
		dbus_message_iter_next(&dict);
	}

	if (!got_type) {
		log_error("kwin-screenshot: reply is not a raw image");
		return false;
	}
	if (m->width == 0 || m->height == 0 ||
		m->width > GRABIT_MAX_PIXEL_SIDE || m->height > GRABIT_MAX_PIXEL_SIDE ||
		m->stride < m->width * 4u) {
		log_error("kwin-screenshot: bogus geometry %ux%u stride=%u",
				  m->width, m->height, m->stride);
		return false;
	}
	if ((size_t)m->stride > KWIN_MAX_IMAGE_BYTES / m->height) {
		log_error("kwin-screenshot: %ux%u stride=%u exceeds the %zu-byte cap",
				  m->width, m->height, m->stride, KWIN_MAX_IMAGE_BYTES);
		return false;
	}
	return true;
}
