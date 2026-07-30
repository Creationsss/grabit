// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/internal.h"
#include "tray/menu.h"

#include <stdbool.h>
#include <stddef.h>

const struct tray_menu_item *gmenu_find_item(const struct tray_menu_item *items,
											 size_t n, int id) {
	for (size_t i = 0; i < n; i++) {
		if (items[i].id == id) return &items[i];
		const struct tray_menu_item *hit =
			gmenu_find_item(items[i].children, items[i].n_children, id);
		if (hit) return hit;
	}
	return NULL;
}

static const char *item_label(const struct tray_menu_item *it) {
	if (it->label_fn) return it->label_fn();
	return it->label ? it->label : "";
}

static void append_prop_s(DBusMessageIter *dict, const char *key, const char *val) {
	DBusMessageIter entry;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	gsni_append_variant_s(&entry, val);
	dbus_message_iter_close_container(dict, &entry);
}

static void append_item_props(DBusMessageIter *parent,
							  const struct tray_menu_item *it, bool submenu) {
	DBusMessageIter dict;
	dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "{sv}", &dict);
	if (it) append_prop_s(&dict, "label", item_label(it));
	if (submenu) append_prop_s(&dict, "children-display", "submenu");
	dbus_message_iter_close_container(parent, &dict);
}

static void append_layout_node(DBusMessageIter *parent, int id,
							   const struct tray_menu_item *it,
							   const struct tray_menu_item *children, size_t n) {
	DBusMessageIter st, kids;
	dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, NULL, &st);
	dbus_int32_t did = id;
	dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &did);
	append_item_props(&st, it, n > 0);
	dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &kids);
	for (size_t i = 0; i < n; i++) {
		DBusMessageIter var;
		dbus_message_iter_open_container(&kids, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
		append_layout_node(&var, children[i].id, &children[i],
						   children[i].children, children[i].n_children);
		dbus_message_iter_close_container(&kids, &var);
	}
	dbus_message_iter_close_container(&st, &kids);
	dbus_message_iter_close_container(parent, &st);
}

DBusHandlerResult gmenu_handle_get_layout(DBusConnection *bus, DBusMessage *msg,
										  const struct tray_menu *m) {
	dbus_int32_t parent_id = 0;
	DBusMessageIter args;
	if (dbus_message_iter_init(msg, &args) &&
		dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32)
		dbus_message_iter_get_basic(&args, &parent_id);

	const struct tray_menu_item *node = NULL;
	const struct tray_menu_item *children = m->items;
	size_t n = m->n;
	if (parent_id != 0) {
		node = gmenu_find_item(m->items, m->n, parent_id);
		if (!node) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		children = node->children;
		n = node->n_children;
	}

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter iter;
	dbus_message_iter_init_append(reply, &iter);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &gmenu_revision);
	append_layout_node(&iter, parent_id, node, children, n);
	return gsni_send_reply(bus, reply);
}

static void append_group_entry(DBusMessageIter *arr, int id,
							   const struct tray_menu_item *it) {
	DBusMessageIter st;
	dbus_message_iter_open_container(arr, DBUS_TYPE_STRUCT, NULL, &st);
	dbus_int32_t did = id;
	dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &did);
	append_item_props(&st, it, it ? it->n_children > 0 : true);
	dbus_message_iter_close_container(arr, &st);
}

static void append_group_all(DBusMessageIter *arr,
							 const struct tray_menu_item *items, size_t n) {
	for (size_t i = 0; i < n; i++) {
		append_group_entry(arr, items[i].id, &items[i]);
		append_group_all(arr, items[i].children, items[i].n_children);
	}
}

DBusHandlerResult gmenu_handle_group_props(DBusConnection *bus, DBusMessage *msg,
										   const struct tray_menu *m) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter out, arr;
	dbus_message_iter_init_append(reply, &out);
	dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);

	bool any = false;
	DBusMessageIter args, ids;
	if (dbus_message_iter_init(msg, &args) &&
		dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse(&args, &ids);
		while (dbus_message_iter_get_arg_type(&ids) == DBUS_TYPE_INT32) {
			dbus_int32_t id;
			dbus_message_iter_get_basic(&ids, &id);
			any = true;
			if (id == 0) {
				append_group_entry(&arr, 0, NULL);
			} else {
				const struct tray_menu_item *it = gmenu_find_item(m->items, m->n, id);
				if (it) append_group_entry(&arr, id, it);
			}
			dbus_message_iter_next(&ids);
		}
	}
	if (!any) {
		append_group_entry(&arr, 0, NULL);
		append_group_all(&arr, m->items, m->n);
	}

	dbus_message_iter_close_container(&out, &arr);
	return gsni_send_reply(bus, reply);
}
