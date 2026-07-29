// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/fetch.h"

#include "log.h"
#include "vendor/sha256/sha256.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

static size_t curl_to_file(void *ptr, size_t sz, size_t nm, void *ud) {
	FILE *f = ud;
	return fwrite(ptr, sz, nm, f);
}

static bool ip_is_internal(const char *ip) {
	if (!ip) return false;
	if (strncasecmp(ip, "::ffff:", 7) == 0 && strchr(ip + 7, '.')) ip += 7;
	unsigned a, b, c, d;
	if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
		if (a == 127 || a == 0 || a == 10) return true;
		if (a == 169 && b == 254) return true;
		if (a == 172 && b >= 16 && b <= 31) return true;
		if (a == 192 && b == 168) return true;
		if (a == 100 && b >= 64 && b <= 127) return true;
		return false;
	}
	if (strcmp(ip, "::1") == 0 || strncmp(ip, "fe80:", 5) == 0 ||
		strncmp(ip, "fc", 2) == 0 || strncmp(ip, "fd", 2) == 0)
		return true;
	return false;
}

static int prereq_cb(void *clientp, char *conn_ip, char *local_ip,
					 int conn_port, int local_port) {
	(void)clientp;
	(void)local_ip;
	(void)conn_port;
	(void)local_port;
	if (ip_is_internal(conn_ip)) {
		log_error("plugin: refusing to fetch from internal address %s", conn_ip);
		return CURL_PREREQFUNC_ABORT;
	}
	return CURL_PREREQFUNC_OK;
}

enum plugin_fetch_result plugin_fetch_url(const char *url, const char *dst,
										  time_t if_modified_since) {
	CURL *c = curl_easy_init();
	if (!c) return PLUGIN_FETCH_FAIL;
	unlink(dst);
	int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) {
		curl_easy_cleanup(c);
		return PLUGIN_FETCH_FAIL;
	}
	FILE *f = fdopen(fd, "wb");
	if (!f) {
		close(fd);
		unlink(dst);
		curl_easy_cleanup(c);
		return PLUGIN_FETCH_FAIL;
	}
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_to_file);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_FILETIME, 1L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "grabit-plugin-fetch/1");
#if LIBCURL_VERSION_NUM >= 0x075000
	curl_easy_setopt(c, CURLOPT_PREREQFUNCTION, prereq_cb);
#endif
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
	if (if_modified_since > 0) {
		curl_easy_setopt(c, CURLOPT_TIMECONDITION, (long)CURL_TIMECOND_IFMODSINCE);
		curl_easy_setopt(c, CURLOPT_TIMEVALUE, (long)if_modified_since);
	}

	CURLcode rc = curl_easy_perform(c);
	long unmet = 0;
	curl_easy_getinfo(c, CURLINFO_CONDITION_UNMET, &unmet);
	fclose(f);
	curl_easy_cleanup(c);

	if (unmet) {
		unlink(dst);
		return PLUGIN_FETCH_NOT_MODIFIED;
	}
	if (rc != CURLE_OK) {
		log_error("plugin: download %s: %s", url, curl_easy_strerror(rc));
		unlink(dst);
		return PLUGIN_FETCH_FAIL;
	}
	return PLUGIN_FETCH_OK;
}

int plugin_sha256_file(const char *path, char *hex_out) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	struct sha256_ctx ctx;
	sha256_init(&ctx);
	uint8_t buf[8192];
	size_t got;
	while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
		sha256_update(&ctx, buf, got);
	}
	int err = ferror(f);
	fclose(f);
	if (err) return -1;
	uint8_t digest[SHA256_DIGEST_SIZE];
	sha256_final(&ctx, digest);
	sha256_to_hex(digest, hex_out);
	return 0;
}

bool plugin_sha256_equal(const char *expect_hex, const char *actual_hex) {
	if (!expect_hex || !*expect_hex) return true;
	return strcasecmp(expect_hex, actual_hex) == 0;
}

int plugin_verify_sha256(const char *path, const char *expect_hex) {
	if (!expect_hex || !*expect_hex) return 0;
	char actual[SHA256_HEX_SIZE];
	if (plugin_sha256_file(path, actual) != 0) {
		log_error("plugin: cannot hash %s", path);
		return -1;
	}
	if (!plugin_sha256_equal(expect_hex, actual)) {
		log_error("plugin: sha256 mismatch on %s (expected %s, got %s)", path,
				  expect_hex, actual);
		return -1;
	}
	return 0;
}
