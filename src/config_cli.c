// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"
#include "region/keybinds.h"
#include "wl.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_help_arg(const char *s) {
	return s && (strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0);
}

static char *split_eq(const char *arg, const char **val_out) {
	const char *eq = strchr(arg, '=');
	if (!eq) return NULL;
	*val_out = eq + 1;
	char *k = strndup(arg, (size_t)(eq - arg));
	return k;
}

static int cfg_store(struct config *c, const char *key, const char *val) {
	int rc = config_set(c, key, val);
	if (rc == 0) rc = config_save(c);
	if (rc == 0) {
		const char *stored = config_get(c, key);
		log_info("set %s = %s", key, stored ? stored : val);
		if (cfg_is_state_key(key)) (void)config_state_clear(key);
	}
	config_free(c);
	return rc == 0 ? 0 : 1;
}

static int cmd_set_watch(const char *key) {
	if (!region_keybind_default(key)) {
		log_error("--watch only applies to keys.* bindings (got `%s`)", key);
		return 2;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		log_error("watch: could not connect to a wayland compositor");
		config_free(&c);
		return 1;
	}
	char binding[256];
	int cap = region_keybind_watch(&s, key, binding, sizeof binding);
	grabit_wl_finish(&s);
	if (cap != 0) {
		log_info("watch cancelled; %s unchanged", key);
		config_free(&c);
		return cap < 0 ? 1 : 0;
	}

	return cfg_store(&c, key, binding);
}

static void print_keys_row(struct config *c, const char *key) {
	const char *cur = config_get(c, key);
	const char *def = region_keybind_default(key);
	if (cur && *cur)
		printf("  %-22s %s\n", key, cur);
	else
		printf("  %-22s %s  (default)\n", key, def ? def : "");
}

static int cmd_set_keys_list(void) {
	struct config c;
	config_load(&c);
	puts("keybinds — set one with `grabit set <key> \"<bindings>\"`,");
	puts("capture it with `--watch`, restore it with `--reset`");
	puts("(`grabit set keys --reset` resets every one):");
	for (int a = 0; a < KA_COUNT; a++)
		print_keys_row(&c, region_keybind_action_key(a));
	for (int t = 0; t < TOOL_COUNT; t++) {
		char key[64];
		snprintf(key, sizeof key, "keys.tool.%s", grabit_tool_names[t]);
		print_keys_row(&c, key);
	}
	config_free(&c);
	return 0;
}

static int cmd_set_reset(const char *key) {
	bool all = strcmp(key, "keys") == 0;
	if (!all && !region_keybind_default(key)) {
		log_error("--reset applies to a keys.* binding or `keys` (all); got `%s`", key);
		return 2;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	size_t removed = all ? cfg_kv_remove(&c, "keys.", true)
						 : cfg_kv_remove(&c, key, false);

	int rc = 0;
	if (removed == 0) {
		log_info("%s already at default", key);
	} else if (config_save(&c) != 0) {
		log_error("could not save config");
		rc = 1;
	} else if (all) {
		log_info("reset all keybinds to defaults (%zu removed)", removed);
	} else {
		log_info("reset %s to default", key);
	}
	config_free(&c);
	return rc;
}

int cmd_set(int argc, char **argv) {
	if (argc == 0 || (argc == 1 && is_help_arg(argv[0]))) {
		cfg_help_print_all_keys();
		return 0;
	}

	if (argv[0] && argv[0][0] == '-') {
		log_error("usage: grabit set <key> [--watch|--reset]");
		return 2;
	}

	bool watch = false, reset = false;
	int positional = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--watch") == 0 || strcmp(argv[i], "-w") == 0)
			watch = true;
		else if (strcmp(argv[i], "--reset") == 0)
			reset = true;
		else
			positional++;
	}
	if (watch || reset) {
		if (watch && reset) {
			log_error("--watch and --reset are mutually exclusive");
			return 2;
		}
		if (positional) {
			log_error("usage: grabit set <key> %s", watch ? "--watch" : "--reset");
			return 2;
		}
		return watch ? cmd_set_watch(argv[0]) : cmd_set_reset(argv[0]);
	}

	if (argc == 1 && strcmp(argv[0], "keys") == 0) return cmd_set_keys_list();

	const char *eq_val = NULL;
	char *eq_key = (argc == 1) ? split_eq(argv[0], &eq_val) : NULL;
	if (eq_key) {
		char *aliased[2] = {eq_key, (char *)eq_val};
		int rc = cmd_set(2, aliased);
		free(eq_key);
		return rc;
	}

	if (argc == 1) {
		const char *ex = NULL, *def = NULL;
		if (cfg_help_example_for_key(argv[0], &ex, &def) != 0) {
			log_error("unknown key: %s", argv[0]);
			const char *hint = cfg_help_suggest_key(argv[0]);
			if (hint) log_info("did you mean: %s ?", hint);
			log_info("run `grabit set` to see all keys");
			return 2;
		}
		printf("%s = ", argv[0]);
		bool starred = cfg_help_print_example(ex, def);
		printf("\n");
		if (def) {
			if (starred)
				printf("(* = default)\n");
			else
				printf("default: %s\n", def);
		}

		struct config c = {0};
		const char *current = NULL;
		bool loaded = config_load_full(&c) == 0;
		if (loaded) current = config_get(&c, argv[0]);
		printf("current: %s\n", current ? current : "(unset)");
		if (loaded) config_free(&c);
		return 0;
	}
	if (argc != 2) {
		log_error("usage: grabit set <key> <value>");
		return 2;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;
	return cfg_store(&c, argv[0], argv[1]);
}

int cmd_get(int argc, char **argv) {
	if (argc == 1 && is_help_arg(argv[0])) {
		puts("usage: grabit get [<key>]");
		return 0;
	}
	if (argc > 1) {
		log_error("usage: grabit get [<key>]");
		return 2;
	}
	struct config c;
	if (config_load_full(&c) != 0) return 1;

	int rc = 0;
	if (argc == 0) {
		if (c.n > 1) qsort(c.kvs, c.n, sizeof *c.kvs, gcfg_cmp_kv);
		for (size_t i = 0; i < c.n; i++) {
			printf("%s = %s\n", c.kvs[i].key, c.kvs[i].val);
		}
	} else {
		const char *v = config_get(&c, argv[0]);
		if (v) {
			puts(v);
		} else {
			log_error("not set: %s", argv[0]);
			rc = 1;
		}
	}
	config_free(&c);
	return rc;
}

int cmd_unset(int argc, char **argv) {
	if (argc == 1 && is_help_arg(argv[0])) {
		puts("usage: grabit unset <key>");
		return 0;
	}
	if (argc != 1) {
		log_error("usage: grabit unset <key>");
		return 2;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	int rc = 0;
	bool found = cfg_kv_remove(&c, argv[0], false) > 0;
	if (cfg_is_state_key(argv[0])) {
		(void)config_state_clear(argv[0]);
		found = true;
	}
	if (!found) {
		log_info("%s was not set", argv[0]);
	} else if (config_save(&c) != 0) {
		log_error("could not save config");
		rc = 1;
	} else {
		log_info("unset %s", argv[0]);
	}
	config_free(&c);
	return rc;
}
