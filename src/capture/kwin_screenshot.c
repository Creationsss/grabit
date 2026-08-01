// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "capture/backend.h"

#include "capture/capture.h"
#include "capture/kwin_internal.h"
#include "capture/pixels.h"
#include "log.h"
#include "util/util.h"
#include "wl/wl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <wayland-client.h>

#define KWIN_DEST "org.kde.KWin.ScreenShot2"
#define KWIN_PATH "/org/kde/KWin/ScreenShot2"
#define KWIN_IFACE "org.kde.KWin.ScreenShot2"

#define KWIN_CALL_TIMEOUT_MS 20000
#define KWIN_READ_CHUNK (256 * 1024)

static DBusConnection *kwin_bus_open(bool quiet) {
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		if (!quiet)
			log_error("kwin-screenshot: no user dbus session (%s)",
					  err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return NULL;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);
	dbus_error_free(&err);
	return bus;
}

static void kwin_bus_close(DBusConnection *bus) {
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
}

bool grabit_kwin_screenshot_available(void) {
	DBusConnection *bus = kwin_bus_open(true);
	if (!bus) return false;

	DBusError err;
	dbus_error_init(&err);
	dbus_bool_t owned = dbus_bus_name_has_owner(bus, KWIN_DEST, &err);
	if (dbus_error_is_set(&err)) owned = FALSE;
	dbus_error_free(&err);

	kwin_bus_close(bus);
	return owned == TRUE;
}

static bool pack_option_bool(DBusMessageIter *dict, const char *key, bool value) {
	DBusMessageIter entry, var;
	dbus_bool_t v = value ? TRUE : FALSE;
	if (!dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry))
		return false;
	bool ok = dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key) &&
			  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var) &&
			  dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &v) &&
			  dbus_message_iter_close_container(&entry, &var);
	return dbus_message_iter_close_container(dict, &entry) && ok;
}

static bool pack_options(DBusMessageIter *args, bool cursor) {
	DBusMessageIter dict;
	if (!dbus_message_iter_open_container(args, DBUS_TYPE_ARRAY, "{sv}", &dict))
		return false;
	bool ok = pack_option_bool(&dict, "include-cursor", cursor) &&
			  pack_option_bool(&dict, "native-resolution", true);
	return dbus_message_iter_close_container(args, &dict) && ok;
}

static int kwin_exchange(DBusConnection *bus, DBusPendingCall *pending, int read_fd,
						 struct kwin_meta *meta, struct grabit_buf *buf) {
	int64_t deadline = grabit_now_ns() + (int64_t)KWIN_CALL_TIMEOUT_MS * 1000000;
	bool replied = false, eof = false;
	size_t want = 0;
	int rc = -1;

	while (!replied || !eof) {
		if (grabit_now_ns() > deadline) {
			log_error("kwin-screenshot: timed out after %d ms", KWIN_CALL_TIMEOUT_MS);
			goto done;
		}

		if (!replied) {
			if (!dbus_connection_read_write_dispatch(bus, eof ? 50 : 0)) {
				log_error("kwin-screenshot: dbus connection lost");
				goto done;
			}
			if (dbus_pending_call_get_completed(pending)) {
				DBusMessage *reply = dbus_pending_call_steal_reply(pending);
				if (!reply) {
					log_error("kwin-screenshot: no reply from KWin");
					goto done;
				}
				bool ok = dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR;
				if (!ok) {
					const char *ename = dbus_message_get_error_name(reply);
					log_error("kwin-screenshot: %s", ename ? ename : "unknown error");
					if (ename && strstr(ename, "NoAuthorized"))
						log_error("kwin-screenshot: not allow-listed; run `make install` "
								  "and re-login so KWin picks up grabit.desktop");
				} else {
					ok = gkwin_parse_meta(reply, meta);
				}
				dbus_message_unref(reply);
				if (!ok) goto done;
				replied = true;
				want = (size_t)meta->stride * meta->height;
			}
		}

		if (replied && buf->len >= want) break;

		if (!eof) {
			struct pollfd pfd = {.fd = read_fd, .events = POLLIN};
			int pr = poll(&pfd, 1, replied ? 1000 : 20);
			if (pr < 0) {
				if (errno == EINTR) continue;
				log_error("kwin-screenshot: poll: %s", strerror(errno));
				goto done;
			}
			if (pr == 0) continue;

			size_t room = want;
			if (!replied) {
				room = buf->len + KWIN_READ_CHUNK;
				if (room > KWIN_MAX_IMAGE_BYTES) {
					log_error("kwin-screenshot: image exceeds the %zu-byte cap",
							  KWIN_MAX_IMAGE_BYTES);
					goto done;
				}
			}
			if (grabit_buf_grow(buf, room) != 0) {
				log_error("kwin-screenshot: oom growing to %zu bytes", room);
				goto done;
			}

			ssize_t n = read(read_fd, buf->data + buf->len, buf->cap - buf->len);
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN) continue;
				log_error("kwin-screenshot: read: %s", strerror(errno));
				goto done;
			}
			if (n == 0)
				eof = true;
			else
				buf->len += (size_t)n;
		}
	}

	if (buf->len < want) {
		log_error("kwin-screenshot: short image (%zu of %zu bytes)", buf->len, want);
		goto done;
	}
	rc = 0;

done:
	return rc;
}

static int kwin_capture(const char *output_name, bool cursor, struct image *out) {
	DBusConnection *bus = kwin_bus_open(false);
	if (!bus) return -1;

	int fds[2] = {-1, -1};
	struct grabit_buf buf = {0};
	struct kwin_meta meta = {0};
	DBusMessage *msg = NULL;
	DBusPendingCall *pending = NULL;
	int rc = -1;

	if (pipe(fds) != 0) {
		log_error("kwin-screenshot: pipe: %s", strerror(errno));
		goto cleanup;
	}

	msg = dbus_message_new_method_call(KWIN_DEST, KWIN_PATH, KWIN_IFACE, "CaptureScreen");
	DBusMessageIter args;
	if (msg) dbus_message_iter_init_append(msg, &args);
	if (!msg ||
		!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &output_name) ||
		!pack_options(&args, cursor) ||
		!dbus_message_iter_append_basic(&args, DBUS_TYPE_UNIX_FD, &fds[1])) {
		log_error("kwin-screenshot: oom building the dbus call");
		goto cleanup;
	}

	close(fds[1]);
	fds[1] = -1;

	bool sent = dbus_connection_send_with_reply(bus, msg, &pending, KWIN_CALL_TIMEOUT_MS);
	dbus_message_unref(msg);
	msg = NULL;
	if (!sent || !pending) {
		log_error("kwin-screenshot: could not send the dbus call");
		goto cleanup;
	}
	dbus_connection_flush(bus);

	if (kwin_exchange(bus, pending, fds[0], &meta, &buf) != 0) goto cleanup;

	uint32_t shm_fmt;
	if (!gkwin_qimage_to_shm(meta.format, &shm_fmt)) {
		log_error("kwin-screenshot: unsupported QImage format %u", meta.format);
		goto cleanup;
	}
	uint32_t resolved;
	enum pixels_conv conv;
	if (!pixels_accept_format(shm_fmt, &resolved, &conv)) {
		log_error("kwin-screenshot: unsupported pixel format %s",
				  pixels_shm_format_name(shm_fmt));
		goto cleanup;
	}

	if (conv != PIX_COPY) {
		pixels_copy(buf.data, (int32_t)meta.stride, buf.data, (int32_t)meta.stride,
					(int32_t)meta.width, (int32_t)meta.height, conv, false);
	}

	out->width = (int32_t)meta.width;
	out->height = (int32_t)meta.height;
	out->stride = (int32_t)meta.stride;
	out->format = pixels_resolved_format(resolved, conv);
	out->bytes = buf.data;
	out->size = (size_t)meta.stride * meta.height;
	buf.data = NULL;
	rc = 0;

cleanup:
	grabit_buf_free(&buf);
	if (fds[0] >= 0) close(fds[0]);
	if (fds[1] >= 0) close(fds[1]);
	if (msg) dbus_message_unref(msg);
	if (pending) {
		dbus_pending_call_cancel(pending);
		dbus_pending_call_unref(pending);
	}
	kwin_bus_close(bus);
	return rc;
}

static void warn_rotation_once(int32_t transform) {
	static bool warned;
	if (transform == 0 || warned) return;
	warned = true;
	log_warn("kwin-screenshot: rotated output (transform=%d); ScreenShot2's "
			 "orientation is untested there, so the image may be skewed",
			 transform);
}

int grabit_kwin_capture_full(struct grabit_wl_state *s, struct grabit_output *o,
							 bool overlay_cursor, struct image *out) {
	(void)s;
	if (!o || !out) return -1;
	if (o->dead || !o->name) return -1;
	memset(out, 0, sizeof *out);
	warn_rotation_once(o->transform);

	int rc = kwin_capture(o->name, overlay_cursor, out);
	if (rc != 0) image_free(out);
	return rc;
}
