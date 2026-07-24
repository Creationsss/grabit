// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "args.h"
#include "config/config.h"
#include "log.h"
#include "notify/notify.h"
#include "upload/internal.h"
#include "upload/sxcu.h"
#include "util/util.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct service SERVICES[] = {
	{"zipline", NULL, "authorization", "files[0].url", false},
	{"nest", "https://nest.rip/api/files/upload", "Authorization", "fileURL", false},
	{"fakecrime", "https://upload.fakecrime.bio", "Secret", "url|data.url", false},
	{"ez", "https://api.e-z.host/files", "key", "imageUrl", false},
	{"guns", "https://guns.lol/api/upload", "key", "link", true},
	{"pixelvault", "https://pixelvault.co/", "Authorization", "resource", false},
};
static const size_t N_SERVICES = sizeof SERVICES / sizeof SERVICES[0];

static void build_auth_keys(const char *service, char *env_key, size_t env_cap,
							char *cfg_key, size_t cfg_cap) {
	snprintf(env_key, env_cap, "GRABIT_%s_AUTH", service);
	for (char *p = env_key + 7; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	snprintf(cfg_key, cfg_cap, "services.%s.auth", service);
}

const char *gup_resolve_auth(struct config *cfg, const char *service) {
	char env_key[64], cfg_key[64];
	build_auth_keys(service, env_key, sizeof env_key, cfg_key, sizeof cfg_key);
	const char *auth = getenv(env_key);
	if (!auth || !auth[0]) auth = config_get(cfg, cfg_key);
	if (!auth || !auth[0]) {
		log_error("no auth token for %s.", service);
		log_error("  recommended (password-manager-friendly):");
		log_error("    export %s=\"$(pass show grabit/%s)\"", env_key, service);
		log_error("  or fallback (plaintext in config 0600):");
		log_error("    grabit set %s <token>", cfg_key);
		return NULL;
	}
	return auth;
}

const struct service *gup_find_service(const char *name) {
	for (size_t i = 0; i < N_SERVICES; i++) {
		if (strcmp(SERVICES[i].name, name) == 0) return &SERVICES[i];
	}
	return NULL;
}

bool upload_service_known(const char *name) {
	if (!name) return false;
	return gup_find_service(name) != NULL || sxcu_dir_has(name);
}

int upload_suggest_service(const char *input, char *out, size_t cap) {
	if (!input || !out || cap == 0) return -1;
	const char *best = NULL;
	size_t best_dist = (size_t)-1;
	for (size_t i = 0; i < N_SERVICES; i++) {
		size_t d = grabit_edit_distance(input, SERVICES[i].name);
		if (d < best_dist) {
			best_dist = d;
			best = SERVICES[i].name;
		}
	}
	char **names = NULL;
	size_t n = 0;
	int rc = -1;
	if (sxcu_dir_list(&names, &n) == 0) {
		for (size_t i = 0; i < n; i++) {
			size_t d = grabit_edit_distance(input, names[i]);
			if (d < best_dist) {
				best_dist = d;
				best = names[i];
			}
		}
	}
	size_t in_len = strlen(input);
	size_t max_allowed = in_len / 3 + 1;
	if (max_allowed < 2) max_allowed = 2;
	if (best && best_dist <= max_allowed) {
		snprintf(out, cap, "%s", best);
		rc = 0;
	}
	for (size_t i = 0; i < n; i++)
		free(names[i]);
	free(names);
	return rc;
}

int upload_preflight(struct config *cfg, const struct args *a, const char **service_out) {
	const char *service = a->service;
	if (!service) service = config_get(cfg, "service");
	if (!service || !service[0]) {
		log_error("no service: pass --<service> or `grabit set service <name>`");
		notify_send(&(struct notify_opts){
			.summary = "grabit: no upload service",
			.body = "run: grabit set service zipline (or nest, fakecrime, ez, guns, pixelvault)",
		});
		return -1;
	}
	if (!upload_service_known(service)) {
		log_error("unknown service: %s", service);
		notify_send(&(struct notify_opts){
			.summary = "grabit: unknown service",
			.body = "valid services: zipline, nest, fakecrime, ez, guns, pixelvault",
		});
		return -1;
	}

	if (!gup_find_service(service) && sxcu_dir_has(service)) {
		if (service_out) *service_out = service;
		return 0;
	}

	if (!gup_resolve_auth(cfg, service)) {
		char body[160];
		snprintf(body, sizeof body, "run: grabit set services.%s.auth <token>", service);
		notify_send(&(struct notify_opts){
			.summary = "grabit: missing auth token",
			.body = body,
		});
		return -1;
	}

	if (strcmp(service, "zipline") == 0) {
		const char *domain = config_get(cfg, "services.zipline.domain");
		if (!domain || !domain[0]) {
			log_error("zipline requires services.zipline.domain (e.g. https://example.com/api/upload)");
			log_error("    grabit set services.zipline.domain https://<host>/api/upload");
			notify_send(&(struct notify_opts){
				.summary = "grabit: zipline domain not set",
				.body = "run: grabit set services.zipline.domain https://<host>/api/upload",
			});
			return -1;
		}
	}

	*service_out = service;
	return 0;
}
