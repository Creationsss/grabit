// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/menu.h"
#include "tray/internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

dbus_uint32_t gmenu_revision = 1;

static bool dispatch_event(const struct tray_menu *m, dbus_int32_t id,
						   const char *event_id) {
	const struct tray_menu_item *it = gmenu_find_item(m->items, m->n, id);
	if (!it) return false;
	if (strcmp(event_id, "clicked") == 0 && it->on_click && it->n_children == 0)
		it->on_click(it);
	return true;
}

static bool read_event(DBusMessageIter *iter, const struct tray_menu *m) {
	dbus_int32_t id;
	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_INT32) return false;
	dbus_message_iter_get_basic(iter, &id);
	dbus_message_iter_next(iter);
	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) return false;
	const char *event_id;
	dbus_message_iter_get_basic(iter, &event_id);
	return dispatch_event(m, id, event_id);
}

static DBusHandlerResult handle_event(DBusConnection *bus, DBusMessage *msg,
									  const struct tray_menu *m) {
	DBusMessageIter iter;
	if (dbus_message_iter_init(msg, &iter)) read_event(&iter, m);
	gsni_send_empty_reply(bus, msg);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_event_group(DBusConnection *bus, DBusMessage *msg,
											const struct tray_menu *m) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;

	DBusMessageIter out, errs;
	dbus_message_iter_init_append(reply, &out);
	dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &errs);

	DBusMessageIter iter, arr;
	if (dbus_message_iter_init(msg, &iter) &&
		dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse(&iter, &arr);
		while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
			DBusMessageIter ev;
			dbus_message_iter_recurse(&arr, &ev);
			dbus_int32_t id = 0;
			if (dbus_message_iter_get_arg_type(&ev) == DBUS_TYPE_INT32)
				dbus_message_iter_get_basic(&ev, &id);
			if (!read_event(&ev, m))
				dbus_message_iter_append_basic(&errs, DBUS_TYPE_INT32, &id);
			dbus_message_iter_next(&arr);
		}
	}

	dbus_message_iter_close_container(&out, &errs);
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult handle_about_to_show(DBusConnection *bus, DBusMessage *msg) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	dbus_bool_t need_update = FALSE;
	dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &need_update,
							 DBUS_TYPE_INVALID);
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult handle_about_to_show_group(DBusConnection *bus,
													DBusMessage *msg) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter out, updates, errs;
	dbus_message_iter_init_append(reply, &out);
	dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &updates);
	dbus_message_iter_close_container(&out, &updates);
	dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "i", &errs);
	dbus_message_iter_close_container(&out, &errs);
	return gsni_send_reply(bus, reply);
}

static bool append_menu_property(DBusMessageIter *parent, const char *prop) {
	DBusMessageIter var;
	if (strcmp(prop, "Version") == 0) {
		dbus_uint32_t v = 3;
		dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "u", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &v);
		dbus_message_iter_close_container(parent, &var);
		return true;
	}
	const char *s = NULL;
	if (strcmp(prop, "Status") == 0) s = "normal";
	if (strcmp(prop, "TextDirection") == 0) s = "ltr";
	if (!s) return false;
	gsni_append_variant_s(parent, s);
	return true;
}

static DBusHandlerResult handle_prop_get(DBusConnection *bus, DBusMessage *msg) {
	const char *iface = NULL, *prop = NULL;
	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
							   DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (strcmp(iface, MENU_IFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter out;
	dbus_message_iter_init_append(reply, &out);
	if (!append_menu_property(&out, prop)) {
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult handle_prop_get_all(DBusConnection *bus, DBusMessage *msg) {
	const char *iface = NULL;
	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
							   DBUS_TYPE_INVALID))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (iface[0] && strcmp(iface, MENU_IFACE) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	static const char *const props[] = {"Version", "Status", "TextDirection", NULL};
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter out, dict;
	dbus_message_iter_init_append(reply, &out);
	dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &dict);
	for (size_t i = 0; props[i]; i++) {
		DBusMessageIter entry;
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &props[i]);
		append_menu_property(&entry, props[i]);
		dbus_message_iter_close_container(&dict, &entry);
	}
	dbus_message_iter_close_container(&out, &dict);
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult handle_introspect(DBusConnection *bus, DBusMessage *msg) {
	static const char xml[] =
		"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\""
		" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
		"<node>"
		" <interface name=\"" MENU_IFACE "\">"
		"  <property name=\"Version\" type=\"u\" access=\"read\"/>"
		"  <property name=\"Status\" type=\"s\" access=\"read\"/>"
		"  <property name=\"TextDirection\" type=\"s\" access=\"read\"/>"
		"  <method name=\"GetLayout\">"
		"   <arg name=\"parentId\" type=\"i\" direction=\"in\"/>"
		"   <arg name=\"recursionDepth\" type=\"i\" direction=\"in\"/>"
		"   <arg name=\"propertyNames\" type=\"as\" direction=\"in\"/>"
		"   <arg name=\"revision\" type=\"u\" direction=\"out\"/>"
		"   <arg name=\"layout\" type=\"(ia{sv}av)\" direction=\"out\"/>"
		"  </method>"
		"  <method name=\"GetGroupProperties\">"
		"   <arg name=\"ids\" type=\"ai\" direction=\"in\"/>"
		"   <arg name=\"propertyNames\" type=\"as\" direction=\"in\"/>"
		"   <arg name=\"properties\" type=\"a(ia{sv})\" direction=\"out\"/>"
		"  </method>"
		"  <method name=\"Event\">"
		"   <arg name=\"id\" type=\"i\" direction=\"in\"/>"
		"   <arg name=\"eventId\" type=\"s\" direction=\"in\"/>"
		"   <arg name=\"data\" type=\"v\" direction=\"in\"/>"
		"   <arg name=\"timestamp\" type=\"u\" direction=\"in\"/>"
		"  </method>"
		"  <method name=\"EventGroup\">"
		"   <arg name=\"events\" type=\"a(isvu)\" direction=\"in\"/>"
		"   <arg name=\"idErrors\" type=\"ai\" direction=\"out\"/>"
		"  </method>"
		"  <method name=\"AboutToShow\">"
		"   <arg name=\"id\" type=\"i\" direction=\"in\"/>"
		"   <arg name=\"needUpdate\" type=\"b\" direction=\"out\"/>"
		"  </method>"
		"  <method name=\"AboutToShowGroup\">"
		"   <arg name=\"ids\" type=\"ai\" direction=\"in\"/>"
		"   <arg name=\"updatesNeeded\" type=\"ai\" direction=\"out\"/>"
		"   <arg name=\"idErrors\" type=\"ai\" direction=\"out\"/>"
		"  </method>"
		"  <signal name=\"LayoutUpdated\">"
		"   <arg name=\"revision\" type=\"u\"/>"
		"   <arg name=\"parent\" type=\"i\"/>"
		"  </signal>"
		" </interface>"
		"</node>";
	return gsni_reply_string(bus, msg, xml);
}

DBusHandlerResult gmenu_handle(DBusConnection *bus, DBusMessage *msg,
							   const struct tray_menu *m) {
	const char *iface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);
	if (!iface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (strcmp(iface, MENU_IFACE) == 0) {
		if (strcmp(member, "GetLayout") == 0)
			return gmenu_handle_get_layout(bus, msg, m);
		if (strcmp(member, "GetGroupProperties") == 0)
			return gmenu_handle_group_props(bus, msg, m);
		if (strcmp(member, "Event") == 0) return handle_event(bus, msg, m);
		if (strcmp(member, "EventGroup") == 0) return handle_event_group(bus, msg, m);
		if (strcmp(member, "AboutToShow") == 0) return handle_about_to_show(bus, msg);
		if (strcmp(member, "AboutToShowGroup") == 0)
			return handle_about_to_show_group(bus, msg);
	}
	if (strcmp(iface, IFACE_PROPS) == 0) {
		if (strcmp(member, "Get") == 0) return handle_prop_get(bus, msg);
		if (strcmp(member, "GetAll") == 0) return handle_prop_get_all(bus, msg);
	}
	if (strcmp(iface, IFACE_INTROSPECT) == 0 && strcmp(member, "Introspect") == 0)
		return handle_introspect(bus, msg);
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void gmenu_emit_layout_updated(DBusConnection *bus) {
	gmenu_revision++;
	DBusMessage *msg = dbus_message_new_signal(MENU_PATH, MENU_IFACE, "LayoutUpdated");
	if (!msg) return;
	dbus_int32_t parent = 0;
	dbus_message_append_args(msg, DBUS_TYPE_UINT32, &gmenu_revision,
							 DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
	dbus_connection_send(bus, msg, NULL);
	dbus_message_unref(msg);
}
