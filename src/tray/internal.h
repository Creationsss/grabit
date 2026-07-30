// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_INTERNAL_H
#define GRABIT_TRAY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <dbus/dbus.h>

#define ITEM_PATH "/StatusNotifierItem"
#define ITEM_IFACE "org.kde.StatusNotifierItem"

#define MENU_PATH "/MenuBar"
#define MENU_IFACE "com.canonical.dbusmenu"

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
	const char *tooltip_body;
	dbus_uint32_t window_id;
	dbus_bool_t item_is_menu;
};

static inline DBusHandlerResult gsni_send_reply(DBusConnection *bus, DBusMessage *reply) {
	dbus_connection_send(bus, reply, NULL);
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static inline void gsni_send_empty_reply(DBusConnection *bus, DBusMessage *msg) {
	DBusMessage *r = dbus_message_new_method_return(msg);
	if (r) (void)gsni_send_reply(bus, r);
}

static inline DBusHandlerResult gsni_reply_string(DBusConnection *bus, DBusMessage *msg,
												  const char *s) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	dbus_message_append_args(reply, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID);
	return gsni_send_reply(bus, reply);
}

static inline void gsni_append_variant_s(DBusMessageIter *parent, const char *val) {
	DBusMessageIter var;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &val);
	dbus_message_iter_close_container(parent, &var);
}

extern const char gsni_introspect_xml[];
extern const char *const gsni_all_prop_names[];

bool gsni_append_property_value(DBusMessageIter *parent, const char *name,
								const struct sni_props *p);

struct tray_menu;
struct tray_menu_item;

extern dbus_uint32_t gmenu_revision;

const struct tray_menu_item *gmenu_find_item(const struct tray_menu_item *items,
											 size_t n, int id);
DBusHandlerResult gmenu_handle_get_layout(DBusConnection *bus, DBusMessage *msg,
										  const struct tray_menu *m);
DBusHandlerResult gmenu_handle_group_props(DBusConnection *bus, DBusMessage *msg,
										   const struct tray_menu *m);

#endif
