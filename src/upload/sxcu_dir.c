// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/sxcu.h"

#include "args.h"
#include "log.h"
#include "paths.h"
#include "upload/internal.h"
#include "util/util.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SXCU_EXT ".sxcu"
#define SXCU_EXT_LEN (sizeof(SXCU_EXT) - 1)
#define SXCU_DIR_MODE 0700
#define SXCU_FILE_MODE 0600
#define SXCU_DIR_RECURSION_MAX 32

static char g_dir[SXCU_PATH_MAX];
static bool g_dir_init;

const char *sxcu_dir_path(void) {
	if (!g_dir_init) {
		const char *cfg = paths_config_dir();
		if (cfg && cfg[0]) {
			snprintf(g_dir, sizeof g_dir, "%s/uploaders", cfg);
		} else {
			g_dir[0] = '\0';
		}
		g_dir_init = true;
	}
	return g_dir;
}

static int ensure_dir_depth(const char *path, int depth) {
	if (depth > SXCU_DIR_RECURSION_MAX) return -1;
	struct stat st;
	if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
	if (mkdir(path, SXCU_DIR_MODE) == 0) return 0;
	if (errno == ENOENT) {
		char tmp[SXCU_PATH_MAX];
		snprintf(tmp, sizeof tmp, "%s", path);
		char *slash = strrchr(tmp, '/');
		if (slash && slash != tmp) {
			*slash = '\0';
			if (ensure_dir_depth(tmp, depth + 1) == 0) return mkdir(path, SXCU_DIR_MODE);
		}
	}
	return -1;
}

static int ensure_dir(const char *path) {
	return ensure_dir_depth(path, 0);
}

static size_t sxcu_stem_len(const char *name) {
	size_t n = strlen(name);
	return (n > SXCU_EXT_LEN && memcmp(name + n - SXCU_EXT_LEN, SXCU_EXT, SXCU_EXT_LEN) == 0)
			   ? n - SXCU_EXT_LEN
			   : n;
}

static bool ends_with_sxcu(const char *name) {
	return sxcu_stem_len(name) != strlen(name);
}

static int cmp_name(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int sxcu_dir_list(char ***names_out, size_t *n_out) {
	*names_out = NULL;
	*n_out = 0;
	const char *dir = sxcu_dir_path();
	if (!dir[0]) return -1;
	DIR *d = opendir(dir);
	if (!d) return 0;

	size_t cap = 0, n = 0;
	char **arr = NULL;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		if (!ends_with_sxcu(e->d_name)) continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			char **p = realloc(arr, cap * sizeof *p);
			if (!p) {
				closedir(d);
				for (size_t i = 0; i < n; i++)
					free(arr[i]);
				free(arr);
				return -1;
			}
			arr = p;
		}
		arr[n] = strndup(e->d_name, sxcu_stem_len(e->d_name));
		if (!arr[n]) {
			closedir(d);
			for (size_t i = 0; i < n; i++)
				free(arr[i]);
			free(arr);
			return -1;
		}
		n++;
	}
	closedir(d);
	if (n > 1) qsort(arr, n, sizeof *arr, cmp_name);
	*names_out = arr;
	*n_out = n;
	return 0;
}

static int write_file(const char *dst, const void *buf, size_t len) {
	char tmpl[SXCU_PATH_MAX];
	int n = snprintf(tmpl, sizeof tmpl, "%s.XXXXXX", dst);
	if (n <= 0 || (size_t)n >= sizeof tmpl) return -1;
	int fd = mkstemp(tmpl);
	if (fd < 0) return -1;
	FILE *out = fdopen(fd, "wb");
	if (!out) {
		close(fd);
		unlink(tmpl);
		return -1;
	}
	int rc = len && fwrite(buf, 1, len, out) != len ? -1 : 0;
	if (fclose(out) != 0) rc = -1;
	if (rc == 0) {
		(void)chmod(tmpl, SXCU_FILE_MODE);
		if (rename(tmpl, dst) != 0) rc = -1;
	}
	if (rc != 0) unlink(tmpl);
	return rc;
}

static int copy_file(const char *src, const char *dst) {
	char *buf = NULL;
	size_t len = 0;
	if (grabit_read_file(src, SXCU_FILE_MAX, &buf, &len) != 0) return -1;
	int rc = write_file(dst, buf, len);
	free(buf);
	return rc;
}

static bool sxcu_name_ok(const char *name) {
	if (!name || !name[0]) return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
	for (const char *p = name; *p; p++) {
		if (*p == '/' || *p == '\\') return false;
	}
	return true;
}

static int sxcu_path_for(const char *name, char *buf, size_t cap) {
	const char *dir = sxcu_dir_path();
	if (!sxcu_name_ok(name) || !dir[0]) return -1;
	int n = snprintf(buf, cap, "%s/%s" SXCU_EXT, dir, name);
	return (n <= 0 || (size_t)n >= cap) ? -1 : 0;
}

static char *sanitize(const char *src) {
	if (!src || !*src) return NULL;
	char *out = strdup(src);
	if (!out) return NULL;
	for (char *p = out; *p; p++) {
		if (*p == '/' || *p == '\\' || *p == ':' || *p == ' ' || *p == '\t') *p = '_';
	}
	return out;
}

int sxcu_dir_name(const char *file_path, char **name_out) {
	*name_out = NULL;
	if (!file_path) return -1;

	struct sxcu_uploader probe = {0};
	if (sxcu_parse_file(file_path, &probe) != 0) return -1;

	const char *fallback = strrchr(file_path, '/');
	fallback = fallback ? fallback + 1 : file_path;
	char *fb_clean = strndup(fallback, sxcu_stem_len(fallback));

	char *name = (probe.name && probe.name[0]) ? sanitize(probe.name) : sanitize(fb_clean);
	free(fb_clean);
	sxcu_free(&probe);
	if (!name) return -1;
	if (args_flag_name_taken(name)) {
		log_error("sxcu: `%s` is a built-in grabit flag, so `--%s` could never "
				  "select this uploader; rename it in the .sxcu file",
				  name, name);
		free(name);
		return -1;
	}
	if (gup_find_service(name)) {
		log_error("sxcu: `%s` is a built-in uploader, so `--%s` would never "
				  "select this one; rename it in the .sxcu file",
				  name, name);
		free(name);
		return -1;
	}
	*name_out = name;
	return 0;
}

static int dest_for(const char *name, char *dst, size_t cap) {
	const char *dir = sxcu_dir_path();
	if (!dir[0] || ensure_dir(dir) != 0) {
		log_error("sxcu: cannot create %s", dir);
		return -1;
	}
	return sxcu_path_for(name, dst, cap);
}

int sxcu_dir_read(const char *name, char **out, size_t *len) {
	char path[SXCU_PATH_MAX];
	if (sxcu_path_for(name, path, sizeof path) != 0) return -1;
	return grabit_read_file(path, SXCU_FILE_MAX, out, len);
}

int sxcu_dir_write(const char *name, const void *buf, size_t len) {
	char dst[SXCU_PATH_MAX];
	if (dest_for(name, dst, sizeof dst) != 0) return -1;
	if (write_file(dst, buf, len) != 0) {
		log_error("sxcu: write %s failed", dst);
		return -1;
	}
	return 0;
}

int sxcu_dir_add(const char *file_path, bool force) {
	char *name = NULL;
	if (sxcu_dir_name(file_path, &name) != 0) return -1;
	char dst[SXCU_PATH_MAX];
	int rc = dest_for(name, dst, sizeof dst);
	if (rc == 0 && !force && access(dst, F_OK) == 0) {
		log_error("sxcu: `%s` already exists; `grabit sxcu add %s --force` replaces "
				  "it, or remove it first",
				  name, file_path);
		rc = -1;
	}
	free(name);
	if (rc != 0) return -1;
	if (copy_file(file_path, dst) != 0) {
		log_error("sxcu: copy %s -> %s failed", file_path, dst);
		return -1;
	}
	return 0;
}

int sxcu_dir_remove(const char *name) {
	char path[SXCU_PATH_MAX];
	if (sxcu_path_for(name, path, sizeof path) != 0) {
		log_error("sxcu: `%s` is not a valid uploader name", name ? name : "");
		return -1;
	}
	if (unlink(path) != 0) {
		log_error("sxcu: remove %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int sxcu_dir_lookup(const char *name, struct sxcu_uploader *out) {
	char path[SXCU_PATH_MAX];
	if (sxcu_path_for(name, path, sizeof path) != 0) return -1;
	if (access(path, R_OK) != 0) return -1;
	return sxcu_parse_file(path, out);
}

bool sxcu_dir_has(const char *name) {
	char path[SXCU_PATH_MAX];
	if (sxcu_path_for(name, path, sizeof path) != 0) return false;
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
