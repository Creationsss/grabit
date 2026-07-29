// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/sni.h"
#include "tray/internal.h"

#include "log.h"
#include "notify/notify.h"
#include "tray/menu.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>

#define WATCHER_DEST "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"

struct sni_ctx {
	struct sni_props props;
	const struct sni_cfg *cfg;
};

static void notify_tray_unavailable(const char *body) {
	notify_send(&(struct notify_opts){
		.summary = "grabit: tray unavailable",
		.body = body,
		.log_hint = true,
	});
}

static DBusHandlerResult handle_get(DBusConnection *bus, DBusMessage *msg,
									const struct sni_props *p) {
	const char *iface = NULL, *prop = NULL;
	if (!dbus_message_get_args(msg, NULL,
							   DBUS_TYPE_STRING, &iface,
							   DBUS_TYPE_STRING, &prop,
							   DBUS_TYPE_INVALID)) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (strcmp(iface, ITEM_IFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter args;
	dbus_message_iter_init_append(reply, &args);
	if (!gsni_append_property_value(&args, prop, p)) {
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult handle_get_all(DBusConnection *bus, DBusMessage *msg,
										const struct sni_props *p) {
	const char *iface = NULL;
	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID)) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (strcmp(iface, ITEM_IFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter args, dict;
	dbus_message_iter_init_append(reply, &args);
	dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
	for (size_t i = 0; gsni_all_prop_names[i]; i++) {
		DBusMessageIter entry;
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &gsni_all_prop_names[i]);
		gsni_append_property_value(&entry, gsni_all_prop_names[i], p);
		dbus_message_iter_close_container(&dict, &entry);
	}
	dbus_message_iter_close_container(&args, &dict);
	return gsni_send_reply(bus, reply);
}

static DBusHandlerResult sni_handler(DBusConnection *bus, DBusMessage *msg, void *data) {
	const struct sni_ctx *ctx = data;
	const char *iface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);
	if (!iface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (strcmp(iface, IFACE_PROPS) == 0) {
		if (strcmp(member, "Get") == 0) return handle_get(bus, msg, &ctx->props);
		if (strcmp(member, "GetAll") == 0) return handle_get_all(bus, msg, &ctx->props);
	}
	if (strcmp(iface, IFACE_INTROSPECT) == 0 && strcmp(member, "Introspect") == 0) {
		return gsni_reply_string(bus, msg, gsni_introspect_xml);
	}
	if (strcmp(iface, ITEM_IFACE) == 0) {
		if (strcmp(member, "Activate") == 0) {
			if (ctx->cfg->on_activate) ctx->cfg->on_activate();
			gsni_send_empty_reply(bus, msg);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
		if (strcmp(member, "SecondaryActivate") == 0 ||
			strcmp(member, "ContextMenu") == 0 ||
			strcmp(member, "Scroll") == 0) {
			gsni_send_empty_reply(bus, msg);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable item_vtable = {
	.message_function = sni_handler,
};

static DBusHandlerResult menu_handler(DBusConnection *bus, DBusMessage *msg, void *data) {
	return gmenu_handle(bus, msg, data);
}

static const DBusObjectPathVTable menu_vtable = {
	.message_function = menu_handler,
};

static DBusMessage *register_msg(const char *name) {
	DBusMessage *reg = dbus_message_new_method_call(WATCHER_DEST, WATCHER_PATH,
													WATCHER_IFACE,
													"RegisterStatusNotifierItem");
	if (reg) dbus_message_append_args(reg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
	return reg;
}

static void register_item_async(DBusConnection *bus, const char *name) {
	DBusMessage *reg = register_msg(name);
	if (!reg) return;
	dbus_connection_send(bus, reg, NULL);
	dbus_message_unref(reg);
}

static int bail(DBusError *err, DBusConnection *bus) {
	dbus_error_free(err);
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	return -1;
}

static DBusHandlerResult watcher_filter(DBusConnection *bus, DBusMessage *msg,
										void *data) {
	const char *name = data;
	if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
		const char *svc = NULL, *old_owner = NULL, *new_owner = NULL;
		if (dbus_message_get_args(msg, NULL,
								  DBUS_TYPE_STRING, &svc,
								  DBUS_TYPE_STRING, &old_owner,
								  DBUS_TYPE_STRING, &new_owner,
								  DBUS_TYPE_INVALID) &&
			strcmp(svc, WATCHER_DEST) == 0 && new_owner[0]) {
			log_info("tray: host appeared; registering item");
			register_item_async(bus, name);
		}
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int sni_run(volatile sig_atomic_t *stop, volatile sig_atomic_t *layout_update,
			const struct sni_cfg *cfg) {
	struct sni_ctx ctx = {
		.props = {
			.category = "ApplicationStatus",
			.id = "grabit",
			.title = "grabit",
			.status = "Active",
			.icon_name = cfg->icon_name,
			.overlay_icon_name = "",
			.attention_icon_name = "",
			.tooltip_body = cfg->tooltip_body,
			.window_id = 0,
			.item_is_menu = FALSE,
		},
		.cfg = cfg,
	};

	DBusError err;
	dbus_error_init(&err);

	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		log_warn("tray: no user dbus session (%s)", err.message ? err.message : "unknown");
		notify_tray_unavailable("tray unavailable; no user dbus session");
		dbus_error_free(&err);
		return -1;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);

	char name[64];
	snprintf(name, sizeof name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
	if (dbus_bus_request_name(bus, name, 0, &err) !=
		DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		log_warn("tray: could not claim bus name %s: %s", name,
				 err.message ? err.message : "unknown");
		notify_tray_unavailable("tray unavailable");
		return bail(&err, bus);
	}

	if (!dbus_connection_register_object_path(bus, ITEM_PATH, &item_vtable, &ctx)) {
		log_warn("tray: could not register SNI object path");
		notify_tray_unavailable("tray unavailable");
		return bail(&err, bus);
	}

	if (cfg->menu &&
		!dbus_connection_register_object_path(bus, MENU_PATH, &menu_vtable,
											  (void *)cfg->menu)) {
		log_warn("tray: could not register menu object path");
	}

	dbus_bus_add_match(bus,
					   "type='signal',sender='org.freedesktop.DBus',"
					   "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
					   "arg0='" WATCHER_DEST "'",
					   NULL);
	dbus_connection_add_filter(bus, watcher_filter, name, NULL);

	DBusMessage *reg = register_msg(name);
	if (!reg) {
		log_warn("tray: oom building register call");
		return bail(&err, bus);
	}

	DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, reg, 2000, &err);
	dbus_message_unref(reg);
	if (!reply) {
		const char *ename = err.name ? err.name : "";
		if (cfg->persist) {
			log_info("tray: no tray host yet (%s); waiting for one to appear",
					 err.message ? err.message : ename);
		} else {
			if (strstr(ename, "ServiceUnknown") || strstr(ename, "NameHasNoOwner")) {
				log_warn("tray: no SNI host running (pass --no-tray to silence)");
				notify_tray_unavailable("no tray host running");
			} else {
				log_warn("tray: register failed: %s",
						 err.message ? err.message : "unknown");
				notify_tray_unavailable("tray register failed");
			}
			return bail(&err, bus);
		}
	} else {
		dbus_message_unref(reply);
	}
	dbus_error_free(&err);

	while (!*stop) {
		if (layout_update && *layout_update) {
			*layout_update = 0;
			gmenu_emit_layout_updated(bus);
		}
		if (!dbus_connection_read_write_dispatch(bus, 100)) break;
	}

	dbus_connection_remove_filter(bus, watcher_filter, name);
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	return 0;
}
