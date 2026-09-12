// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "util/dbus.h"

#include "log.h"

DBusConnection *grabit_dbus_session_open(const char *tag, bool quiet) {
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		if (!quiet)
			log_error("%s: no user dbus session (%s)", tag,
					  err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return NULL;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);
	dbus_error_free(&err);
	return bus;
}

void grabit_dbus_session_close(DBusConnection *bus) {
	if (!bus) return;
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
}

bool grabit_dbus_name_owned(const char *name) {
	DBusConnection *bus = grabit_dbus_session_open(NULL, true);
	if (!bus) return false;
	DBusError err;
	dbus_error_init(&err);
	dbus_bool_t owned = dbus_bus_name_has_owner(bus, name, &err);
	if (dbus_error_is_set(&err)) owned = FALSE;
	dbus_error_free(&err);
	grabit_dbus_session_close(bus);
	return owned == TRUE;
}
