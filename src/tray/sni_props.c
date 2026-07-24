// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <dbus/dbus.h>

const char gsni_introspect_xml[] =
	"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\""
	" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
	"<node>"
	" <interface name=\"" IFACE_INTROSPECT "\">"
	"  <method name=\"Introspect\"><arg name=\"data\" type=\"s\" direction=\"out\"/></method>"
	" </interface>"
	" <interface name=\"" IFACE_PROPS "\">"
	"  <method name=\"Get\">"
	"   <arg name=\"interface\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"property\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"value\" type=\"v\" direction=\"out\"/>"
	"  </method>"
	"  <method name=\"GetAll\">"
	"   <arg name=\"interface\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>"
	"  </method>"
	" </interface>"
	" <interface name=\"" ITEM_IFACE "\">"
	"  <property name=\"Category\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Id\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Title\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Status\" type=\"s\" access=\"read\"/>"
	"  <property name=\"IconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
	"  <property name=\"Menu\" type=\"o\" access=\"read\"/>"
	"  <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
	"  <property name=\"WindowId\" type=\"u\" access=\"read\"/>"
	"  <method name=\"Activate\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"SecondaryActivate\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"ContextMenu\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"Scroll\"><arg type=\"i\"/><arg type=\"s\"/></method>"
	" </interface>"
	"</node>";

static void append_variant_str(DBusMessageIter *parent, const char *s) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "s", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_uint32(DBusMessageIter *parent, dbus_uint32_t u) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "u", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &u);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_bool(DBusMessageIter *parent, dbus_bool_t b) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "b", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_empty_pixmap(DBusMessageIter *parent) {
	DBusMessageIter v, a;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "a(iiay)", &v);
	dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "(iiay)", &a);
	dbus_message_iter_close_container(&v, &a);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_tooltip(DBusMessageIter *parent) {
	DBusMessageIter v, st, pixmap;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &v);
	dbus_message_iter_open_container(&v, DBUS_TYPE_STRUCT, NULL, &st);
	const char *empty = "";
	const char *name = "grabit";
	const char *body = "Left click to stop, Right click for options";
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
	dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &pixmap);
	dbus_message_iter_close_container(&st, &pixmap);
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &name);
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &body);
	dbus_message_iter_close_container(&v, &st);
	dbus_message_iter_close_container(parent, &v);
}

bool gsni_append_property_value(DBusMessageIter *parent, const char *name,
								const struct sni_props *p) {
	if (strcmp(name, "Category") == 0) {
		append_variant_str(parent, p->category);
		return true;
	}
	if (strcmp(name, "Id") == 0) {
		append_variant_str(parent, p->id);
		return true;
	}
	if (strcmp(name, "Title") == 0) {
		append_variant_str(parent, p->title);
		return true;
	}
	if (strcmp(name, "Status") == 0) {
		append_variant_str(parent, p->status);
		return true;
	}
	if (strcmp(name, "IconName") == 0) {
		append_variant_str(parent, p->icon_name);
		return true;
	}
	if (strcmp(name, "OverlayIconName") == 0) {
		append_variant_str(parent, p->overlay_icon_name);
		return true;
	}
	if (strcmp(name, "AttentionIconName") == 0) {
		append_variant_str(parent, p->attention_icon_name);
		return true;
	}
	if (strcmp(name, "WindowId") == 0) {
		append_variant_uint32(parent, p->window_id);
		return true;
	}
	if (strcmp(name, "ItemIsMenu") == 0) {
		append_variant_bool(parent, p->item_is_menu);
		return true;
	}
	if (strcmp(name, "IconPixmap") == 0 || strcmp(name, "OverlayIconPixmap") == 0 ||
		strcmp(name, "AttentionIconPixmap") == 0) {
		append_variant_empty_pixmap(parent);
		return true;
	}
	if (strcmp(name, "ToolTip") == 0) {
		append_variant_tooltip(parent);
		return true;
	}
	if (strcmp(name, "Menu") == 0) {
		const char *path = "/MenuBar";
		DBusMessageIter v;
		dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "o", &v);
		dbus_message_iter_append_basic(&v, DBUS_TYPE_OBJECT_PATH, &path);
		dbus_message_iter_close_container(parent, &v);
		return true;
	}
	return false;
}

const char *const gsni_all_prop_names[] = {
	"Category",
	"Id",
	"Title",
	"Status",
	"IconName",
	"IconPixmap",
	"OverlayIconName",
	"OverlayIconPixmap",
	"AttentionIconName",
	"AttentionIconPixmap",
	"ToolTip",
	"Menu",
	"ItemIsMenu",
	"WindowId",
	NULL,
};
