// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_INTERNAL_H
#define GRABIT_TRAY_INTERNAL_H

#include <stdbool.h>

#include <dbus/dbus.h>

#define ITEM_PATH "/StatusNotifierItem"
#define ITEM_IFACE "org.kde.StatusNotifierItem"

#define IFACE_PROPS "org.freedesktop.DBus.Properties"
#define IFACE_INTROSPECT "org.freedesktop.DBus.Introspectable"

struct sni_props {
	const char *category;
	const char *id;
	const char *title;
	const char *status;
	const char *icon_name;
	const char *overlay_icon_name;
	const char *attention_icon_name;
	dbus_uint32_t window_id;
	dbus_bool_t item_is_menu;
};

extern const char gsni_introspect_xml[];
extern const char *const gsni_all_prop_names[];

bool gsni_append_property_value(DBusMessageIter *parent, const char *name,
								const struct sni_props *p);

#endif
