// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/sc_backend.h"

#include "log.h"
#include "region/region.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>

#define MU_DEST "org.gnome.Mutter.ScreenCast"
#define MU_PATH "/org/gnome/Mutter/ScreenCast"
#define MU_IFACE "org.gnome.Mutter.ScreenCast"
#define MU_SESSION_IFACE "org.gnome.Mutter.ScreenCast.Session"
#define MU_STREAM_IFACE "org.gnome.Mutter.ScreenCast.Stream"

#define MU_CALL_TIMEOUT_MS 10000
#define MU_NODE_TIMEOUT_MS 10000

#define MU_CURSOR_HIDDEN 0
#define MU_CURSOR_EMBEDDED 1

struct sc_gnome {
	DBusConnection *bus;
	char *session_path;
};

static DBusConnection *bus_open(bool quiet) {
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		if (!quiet)
			log_error("gnome-screencast: no user dbus session (%s)",
					  err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return NULL;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);
	dbus_error_free(&err);
	return bus;
}

bool sc_gnome_available(void) {
	DBusConnection *bus = bus_open(true);
	if (!bus) return false;
	DBusError err;
	dbus_error_init(&err);
	dbus_bool_t owned = dbus_bus_name_has_owner(bus, MU_DEST, &err);
	dbus_error_free(&err);
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	return owned == TRUE;
}

static void append_empty_dict(DBusMessageIter *iter) {
	DBusMessageIter sub;
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &sub);
	dbus_message_iter_close_container(iter, &sub);
}

static void append_uint_option(DBusMessageIter *dict, const char *key,
							   dbus_uint32_t value) {
	DBusMessageIter entry, var;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &value);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(dict, &entry);
}

static DBusMessage *call(DBusConnection *bus, const char *path,
						 const char *iface, const char *method,
						 void (*args)(DBusMessageIter *, void *), void *user) {
	DBusMessage *msg = dbus_message_new_method_call(MU_DEST, path, iface, method);
	if (!msg) return NULL;
	if (args) {
		DBusMessageIter iter;
		dbus_message_iter_init_append(msg, &iter);
		args(&iter, user);
	}
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(
		bus, msg, MU_CALL_TIMEOUT_MS, &err);
	dbus_message_unref(msg);
	if (!reply) {
		log_error("gnome-screencast: %s failed: %s", method,
				  err.message ? err.message : "no reply");
		dbus_error_free(&err);
		return NULL;
	}
	dbus_error_free(&err);
	return reply;
}

static char *reply_object_path(DBusMessage *reply) {
	const char *path = NULL;
	DBusError err;
	dbus_error_init(&err);
	if (!dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH, &path,
							   DBUS_TYPE_INVALID)) {
		log_error("gnome-screencast: malformed reply: %s",
				  err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return NULL;
	}
	dbus_error_free(&err);
	return path ? strdup(path) : NULL;
}

static void args_empty_dict(DBusMessageIter *iter, void *user) {
	(void)user;
	append_empty_dict(iter);
}

struct area_args {
	struct rect r;
	bool cursor;
};

static void args_record_area(DBusMessageIter *iter, void *user) {
	struct area_args *a = user;
	dbus_int32_t v[4] = {a->r.x, a->r.y, a->r.w, a->r.h};
	for (int i = 0; i < 4; i++)
		dbus_message_iter_append_basic(iter, DBUS_TYPE_INT32, &v[i]);

	DBusMessageIter dict;
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
	append_uint_option(&dict, "cursor-mode",
					   a->cursor ? MU_CURSOR_EMBEDDED : MU_CURSOR_HIDDEN);
	dbus_message_iter_close_container(iter, &dict);
}

static bool node_from_signal(DBusMessage *msg, const char *stream_path,
							 uint32_t *out) {
	if (!dbus_message_is_signal(msg, MU_STREAM_IFACE, "PipeWireStreamAdded"))
		return false;
	const char *path = dbus_message_get_path(msg);
	if (!path || strcmp(path, stream_path) != 0) return false;

	dbus_uint32_t node = 0;
	DBusError err;
	dbus_error_init(&err);
	bool ok = dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &node,
									DBUS_TYPE_INVALID);
	dbus_error_free(&err);
	if (!ok) return false;
	*out = node;
	return true;
}

static int wait_for_node(DBusConnection *bus, const char *stream_path,
						 uint32_t *out_node_id) {
	int waited = 0;
	while (waited < MU_NODE_TIMEOUT_MS) {
		dbus_connection_read_write(bus, 50);
		waited += 50;
		DBusMessage *msg;
		while ((msg = dbus_connection_pop_message(bus)) != NULL) {
			bool hit = node_from_signal(msg, stream_path, out_node_id);
			dbus_message_unref(msg);
			if (hit) return 0;
		}
	}
	log_error("gnome-screencast: timed out waiting for PipeWireStreamAdded");
	return -1;
}

struct sc_gnome *sc_gnome_start(struct rect r, bool cursor, uint32_t *out_node_id) {
	if (r.w <= 0 || r.h <= 0) return NULL;

	struct sc_gnome *g = calloc(1, sizeof *g);
	if (!g) return NULL;
	g->bus = bus_open(false);
	if (!g->bus) {
		free(g);
		return NULL;
	}

	DBusMessage *reply = call(g->bus, MU_PATH, MU_IFACE, "CreateSession",
							  args_empty_dict, NULL);
	if (!reply) {
		sc_gnome_stop(g);
		return NULL;
	}
	g->session_path = reply_object_path(reply);
	dbus_message_unref(reply);
	if (!g->session_path) {
		sc_gnome_stop(g);
		return NULL;
	}

	struct area_args aa = {.r = r, .cursor = cursor};
	reply = call(g->bus, g->session_path, MU_SESSION_IFACE, "RecordArea",
				 args_record_area, &aa);
	if (!reply) {
		sc_gnome_stop(g);
		return NULL;
	}
	char *stream_path = reply_object_path(reply);
	dbus_message_unref(reply);
	if (!stream_path) {
		sc_gnome_stop(g);
		return NULL;
	}

	DBusError err;
	dbus_error_init(&err);
	char match[256];
	snprintf(match, sizeof match,
			 "type='signal',interface='%s',member='PipeWireStreamAdded',path='%s'",
			 MU_STREAM_IFACE, stream_path);
	dbus_bus_add_match(g->bus, match, &err);
	dbus_connection_flush(g->bus);
	if (dbus_error_is_set(&err)) {
		log_error("gnome-screencast: add_match failed: %s", err.message);
		dbus_error_free(&err);
		free(stream_path);
		sc_gnome_stop(g);
		return NULL;
	}
	dbus_error_free(&err);

	reply = call(g->bus, g->session_path, MU_SESSION_IFACE, "Start", NULL, NULL);
	if (!reply) {
		free(stream_path);
		sc_gnome_stop(g);
		return NULL;
	}
	dbus_message_unref(reply);

	int rc = wait_for_node(g->bus, stream_path, out_node_id);
	free(stream_path);
	if (rc != 0) {
		sc_gnome_stop(g);
		return NULL;
	}

	log_debug("gnome-screencast: streaming %dx%d at %d,%d on pipewire node %u",
			  r.w, r.h, r.x, r.y, *out_node_id);
	return g;
}

void sc_gnome_stop(struct sc_gnome *g) {
	if (!g) return;
	if (g->bus && g->session_path) {
		DBusMessage *reply = call(g->bus, g->session_path, MU_SESSION_IFACE,
								  "Stop", NULL, NULL);
		if (reply) dbus_message_unref(reply);
	}
	free(g->session_path);
	if (g->bus) {
		dbus_connection_close(g->bus);
		dbus_connection_unref(g->bus);
	}
	free(g);
}
