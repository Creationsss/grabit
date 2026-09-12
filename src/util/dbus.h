// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UTIL_DBUS_H
#define GRABIT_UTIL_DBUS_H

#include <stdbool.h>

#include <dbus/dbus.h>

DBusConnection *grabit_dbus_session_open(const char *tag, bool quiet);
void grabit_dbus_session_close(DBusConnection *bus);
bool grabit_dbus_name_owned(const char *name);

#endif
