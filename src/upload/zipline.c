// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/zipline.h"

#include "config/config.h"
#include "log.h"
#include "mime.h"
#include "upload/upload.h"
#include "util/json_path.h"
#include "util/util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <json-c/json.h>

struct curl_slist *zipline_option_headers(struct config *cfg,
										  struct curl_slist *headers, bool *oom) {
	const char *prefix = "services.zipline.headers.";
	size_t pl = strlen(prefix);
	bool has_format = false;
	for (size_t i = 0; i < cfg->n; i++) {
		const char *k = cfg->kvs[i].key;
		if (strncmp(k, prefix, pl) != 0) continue;
		const char *header_name = k + pl;
		const char *val = cfg->kvs[i].val;
		if (strcmp(header_name, "x-zipline-format") == 0) has_format = true;
		if (val && val[0]) headers = upload_header_append(headers, header_name, val, oom);
	}
	if (!has_format) {
		headers = upload_header_append(headers, "x-zipline-format", "name", oom);
	}
	return headers;
}

static long long zipline_chunk_bytes(struct config *cfg) {
	const char *v = config_get(cfg, "services.zipline.chunk_size");
	long long mb = 25;
	if (v && v[0]) {
		char *end = NULL;
		long n = strtol(v, &end, 10);
		if (end && *end == '\0' && n >= 1 && n <= 95) mb = n;
	}
	return mb * 1024 * 1024;
}

int zipline_upload_partial(const char *base_url, const char *auth,
						   struct config *cfg, const char *file_path,
						   struct upload_result *out) {
	struct stat st;
	FILE *f = fopen(file_path, "rb");
	if (!f) {
		log_error("upload: open %s: %s", file_path, strerror(errno));
		return -1;
	}
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) {
		log_error("upload: cannot chunk %s (empty or not a regular file)", file_path);
		fclose(f);
		return -1;
	}
	long long total = (long long)st.st_size;
	long long alloc = zipline_chunk_bytes(cfg);
	long long chunk = alloc;

	int ret = -1;
	char *purl = NULL;
	char *buf = NULL;
	char *ctype = NULL;
	char *enc = NULL;
	char *identifier = NULL;
	struct grabit_buf resp = {0};
	struct json_object *root = NULL;
	CURL *c = curl_easy_init();
	if (!c) goto done;
	if (grabit_xasprintf(&purl, "%s/partial", base_url) != 0) goto done;
	buf = malloc((size_t)alloc);
	if (!buf) {
		log_error("upload: out of memory for %lld MiB chunk buffer", alloc >> 20);
		goto done;
	}
	ctype = mime_for_file(file_path);
	enc = curl_easy_escape(c, grabit_basename(file_path), 0);

	char total_s[32], range[96];
	snprintf(total_s, sizeof total_s, "%lld", total);
	log_info("uploading %s to zipline in %lld chunk(s) of %lld MiB ...",
			 grabit_basename(file_path), (total + chunk - 1) / chunk, chunk >> 20);

	curl_easy_setopt(c, CURLOPT_URL, purl);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, upload_curl_buf_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
	upload_curl_common(c);

	long long off = 0;
	while (off < total) {
		size_t have = fread(buf, 1, (size_t)alloc, f);
		if (have == 0) {
			log_error("upload: short read at offset %lld", off);
			goto done;
		}
		size_t pos = 0;
		while (pos < have) {
			size_t send = have - pos;
			if ((long long)send > chunk) send = (size_t)chunk;
			long long start = off + (long long)pos;
			bool last = start + (long long)send >= total;

			bool oom = false;
			struct curl_slist *headers = NULL;
			headers = upload_header_append(headers, "authorization", auth, &oom);
			headers = zipline_option_headers(cfg, headers, &oom);
			headers = upload_header_append(headers, "x-zipline-p-filename",
										   enc ? enc : grabit_basename(file_path), &oom);
			headers = upload_header_append(headers, "x-zipline-p-content-type",
										   ctype ? ctype : "application/octet-stream", &oom);
			headers = upload_header_append(headers, "x-zipline-p-content-length", total_s, &oom);
			headers = upload_header_append(headers, "x-zipline-p-lastchunk",
										   last ? "true" : "false", &oom);
			if (identifier)
				headers = upload_header_append(headers, "x-zipline-p-identifier",
											   identifier, &oom);
			snprintf(range, sizeof range, "bytes %lld-%lld/%lld",
					 start, start + (long long)send - 1, total);
			headers = upload_header_append(headers, "content-range", range, &oom);

			curl_mime *mime = curl_mime_init(c);
			curl_mimepart *part = curl_mime_addpart(mime);
			curl_mime_name(part, "file");
			curl_mime_data(part, buf + pos, send);
			curl_mime_filename(part, "blob");

			curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
			curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);

			log_debug("POST %s (offset %lld, %zu bytes)", purl, start, send);
			CURLcode rc = oom ? CURLE_OUT_OF_MEMORY : curl_easy_perform(c);
			long http_code = 0;
			curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
			curl_slist_free_all(headers);
			curl_mime_free(mime);

			if (rc != CURLE_OK) {
				out->curl_code = (int)rc;
				upload_log_curl_failure((int)rc);
				goto done;
			}
			if (http_code == 413 && chunk > (1 << 20)) {
				chunk /= 2;
				if (chunk < (1 << 20)) chunk = 1 << 20;
				log_debug("zipline: chunk rejected (HTTP 413); retrying with "
						  "%lld MiB chunks",
						  chunk >> 20);
				grabit_buf_free(&resp);
				continue;
			}
			if (http_code < 200 || http_code >= 300) {
				out->http_code = http_code;
				out->body = resp.data;
				resp.data = NULL;
				upload_log_http_failure(http_code, out->body);
				goto done;
			}

			root = resp.data ? json_tokener_parse(resp.data) : NULL;
			if (!root) {
				log_error("zipline: invalid JSON response at offset %lld", start);
				upload_log_response_body(resp.data);
				goto done;
			}
			if (start == 0) {
				identifier = grabit_json_path_string(root, "partialIdentifier");
				if (!identifier) {
					log_error("zipline: server did not return a partial identifier "
							  "(chunked uploads need zipline v4)");
					upload_log_response_body(resp.data);
					goto done;
				}
			}
			if (last) {
				out->url = grabit_json_path_string(root, "files[0].url");
				out->http_code = http_code;
				out->body = resp.data;
				resp.data = NULL;
				if (!out->url) {
					log_error("zipline: could not find files[0].url in final chunk response");
					upload_log_response_body(out->body);
					goto done;
				}
				ret = 0;
			}
			json_object_put(root);
			root = NULL;
			grabit_buf_free(&resp);
			pos += send;
		}
		off += (long long)have;
	}

done:
	if (root) json_object_put(root);
	grabit_buf_free(&resp);
	if (c) curl_easy_cleanup(c);
	curl_free(enc);
	free(identifier);
	free(ctype);
	free(buf);
	free(purl);
	fclose(f);
	return ret;
}
