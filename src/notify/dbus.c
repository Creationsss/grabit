// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "notify/notify.h"

#include "config/config.h"
#include "log.h"
#include "util/util.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dbus/dbus.h>

#define BUS_DEST "org.freedesktop.Notifications"
#define BUS_PATH "/org/freedesktop/Notifications"
#define BUS_IFACE "org.freedesktop.Notifications"
#define BUS_METHOD "Notify"

#define APP_NAME "grabit"
#define EXPIRE_DEFAULT (-1)
#define DBUS_TIMEOUT_MS 2000

static bool g_show;
static bool g_silent;
static bool g_warned_bus;
static bool g_warned_daemon;

void notify_init(struct config *cfg, bool silent) {
	g_silent = silent;
	if (silent) {
		g_show = false;
		return;
	}
	const char *v = cfg ? config_get(cfg, "notifications") : NULL;
	g_show = !v || strcmp(v, "true") == 0;
}

static const char *utf8_safe(const char *s, char *buf, size_t cap) {
	if (!s || !*s) return "";
	size_t n = strlen(s);
	if (n >= cap) n = cap - 1;
	n = grabit_utf8_valid_prefix(s, n);
	memcpy(buf, s, n);
	buf[n] = '\0';
	return buf;
}

static bool hint_basic(DBusMessageIter *hints, const char *key, int type,
					   const char *sig, const void *val) {
	DBusMessageIter entry, var;
	if (!dbus_message_iter_open_container(hints, DBUS_TYPE_DICT_ENTRY, NULL, &entry))
		return false;
	if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) return false;
	if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, sig, &var))
		return false;
	if (!dbus_message_iter_append_basic(&var, type, val)) return false;
	if (!dbus_message_iter_close_container(&entry, &var)) return false;
	return dbus_message_iter_close_container(hints, &entry);
}

static unsigned char urgency_byte(const struct notify_opts *o) {
	switch (o->urgency) {
	case NOTIFY_LOW:
		return 0;
	case NOTIFY_NORMAL:
		return 1;
	case NOTIFY_CRITICAL:
		return 2;
	case NOTIFY_URGENCY_AUTO:
		break;
	}
	return o->force ? 2 : 1;
}

static bool pack_notify_args(DBusMessage *msg, const struct notify_opts *o) {
	DBusMessageIter args;
	dbus_message_iter_init_append(msg, &args);

	char body_buf[1024];
	char summary_buf[512], icon_buf[4096], scrub_buf[1024];
	const char *app = APP_NAME;
	const char *icon = utf8_safe(o->icon_path, icon_buf, sizeof icon_buf);
	const char *summary = utf8_safe(o->summary, summary_buf, sizeof summary_buf);
	const char *body = utf8_safe(o->body, scrub_buf, sizeof scrub_buf);
	if ((o->log_hint || o->force) && log_file_enabled()) {
		snprintf(body_buf, sizeof body_buf, "%s%ssee %s", body, body[0] ? "\n" : "",
				 log_file_path());
		body = body_buf;
	}
	dbus_uint32_t replaces = o->replaces ? (dbus_uint32_t)*o->replaces : 0;
	unsigned char urgency = urgency_byte(o);
	dbus_int32_t expire = urgency == 2 ? 0 : EXPIRE_DEFAULT;
	const char *no_icon = "";

	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &no_icon)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body)) return false;

	DBusMessageIter actions;
	if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions)) return false;
	if (!dbus_message_iter_close_container(&args, &actions)) return false;

	DBusMessageIter hints;
	if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints)) return false;
	if (!hint_basic(&hints, "urgency", DBUS_TYPE_BYTE, "y", &urgency)) return false;
	const char *entry = APP_NAME;
	if (!hint_basic(&hints, "desktop-entry", DBUS_TYPE_STRING, "s", &entry)) return false;
	if (icon[0] && !hint_basic(&hints, "image-path", DBUS_TYPE_STRING, "s", &icon))
		return false;
	if (o->transient) {
		dbus_bool_t yes = TRUE;
		if (!hint_basic(&hints, "transient", DBUS_TYPE_BOOLEAN, "b", &yes)) return false;
	}
	if (!dbus_message_iter_close_container(&args, &hints)) return false;

	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire)) return false;
	return true;
}

static DBusConnection *g_bus;

void notify_finish(void) {
	if (!g_bus) return;
	dbus_connection_close(g_bus);
	dbus_connection_unref(g_bus);
	g_bus = NULL;
}

static DBusConnection *notify_bus(DBusError *err) {
	if (g_bus) return g_bus;
	g_bus = dbus_bus_get_private(DBUS_BUS_SESSION, err);
	if (g_bus) dbus_connection_set_exit_on_disconnect(g_bus, FALSE);
	return g_bus;
}

void notify_send(const struct notify_opts *o) {
	if (!o || !o->summary) return;
	if ((g_silent || !g_show) && !o->force) return;

	DBusError err;
	dbus_error_init(&err);

	DBusConnection *bus = notify_bus(&err);
	if (!bus) {
		if (!g_warned_bus) {
			log_warn("notifications unavailable: no user dbus session (%s)",
					 err.message ? err.message : "unknown");
			g_warned_bus = true;
		}
		dbus_error_free(&err);
		return;
	}

	DBusMessage *msg = dbus_message_new_method_call(BUS_DEST, BUS_PATH, BUS_IFACE, BUS_METHOD);
	if (!msg || !pack_notify_args(msg, o)) {
		log_warn("notify: oom building message");
		goto cleanup;
	}

	DBusMessage *reply = dbus_connection_send_with_reply_and_block(
		bus, msg, DBUS_TIMEOUT_MS, &err);
	if (!reply) {
		if (!g_warned_daemon) {
			const char *name = err.name ? err.name : "";
			if (strstr(name, "ServiceUnknown") || strstr(name, "NameHasNoOwner")) {
				log_warn("notifications unavailable: no notification daemon running");
			} else {
				log_warn("notify: %s: %s", name[0] ? name : "(no name)",
						 err.message ? err.message : "send failed");
			}
			g_warned_daemon = true;
		}
	} else {
		dbus_uint32_t id = 0;
		if (o->replaces &&
			dbus_message_get_args(reply, NULL, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID))
			*o->replaces = (unsigned int)id;
		dbus_message_unref(reply);
	}

cleanup:
	if (msg) dbus_message_unref(msg);
	dbus_error_free(&err);
}
