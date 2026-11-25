// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_H
#define GRABIT_UPLOAD_H

#include <stdbool.h>
#include <stddef.h>

#include <curl/curl.h>

struct config;
struct args;

bool upload_service_known(const char *name);
const char *upload_missing_setting(struct config *cfg, const char *service);
int upload_suggest_service(const char *input, char *out, size_t cap);

int upload_preflight(struct config *cfg, const struct args *a, const char **service_out);

struct upload_result {
	long http_code;
	int curl_code; // CURLcode; 0 if curl succeeded
	char *url;
	char *del_url;
	char *thumb_url;
	char *body;
	char *error;
};

void upload_result_free(struct upload_result *r);

int upload_perform(const char *service_name,
				   const char *file_path,
				   struct config *cfg, bool chunked,
				   struct upload_result *out);

int upload_delete(const char *service_name, struct config *cfg, const struct upload_result *r);
int upload_delete_target(const char *service_name, struct config *cfg,
						 const struct upload_result *r, char **url, bool *needs_auth);
int upload_delete_url(const char *service_name, struct config *cfg, const char *url,
					  bool needs_auth);

void upload_friendly_error(const struct upload_result *r, char *out, size_t cap);

struct curl_slist *upload_header_append(struct curl_slist *list, const char *name,
										const char *value, bool *oom);
size_t upload_curl_buf_write(char *ptr, size_t size, size_t nmemb, void *user);
void upload_curl_common(CURL *curl);
void upload_progress_finish(void);
void upload_progress_span(long long base, long long total);
void upload_log_http_failure(long code, const char *body);
void upload_log_curl_failure(int code);
void upload_log_response_body(const char *body);

enum upload_test_status {
	UPLOAD_TEST_READY,
	UPLOAD_TEST_NO_SERVICE,
	UPLOAD_TEST_UNKNOWN,
	UPLOAD_TEST_MISSING,
};

enum upload_test_delete {
	UPLOAD_TEST_DELETED,
	UPLOAD_TEST_KEPT,
	UPLOAD_TEST_DELETE_FAILED,
};

struct upload_test_result {
	enum upload_test_delete deleted;
	char url[1024];
	char error[256];
};

enum upload_test_status upload_test_check(struct config *cfg, const char *service,
										  const char **missing);
int upload_test_run(const char *service, struct config *cfg, struct upload_test_result *out);

int cmd_sxcu(int argc, char **argv);
int cmd_upload(int argc, char **argv);

#endif
