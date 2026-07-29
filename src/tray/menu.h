// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_MENU_H
#define GRABIT_TRAY_MENU_H

#include <stddef.h>

#include <dbus/dbus.h>

struct tray_menu_item {
	int id;
	const char *label;
	const char *(*label_fn)(void);
	void (*on_click)(const struct tray_menu_item *it);
	const void *user;
	const struct tray_menu_item *children;
	size_t n_children;
};

struct tray_menu {
	const struct tray_menu_item *items;
	size_t n;
};

DBusHandlerResult gmenu_handle(DBusConnection *bus, DBusMessage *msg,
							   const struct tray_menu *m);
void gmenu_emit_layout_updated(DBusConnection *bus);

#endif
