# grabit plugins

plugins are standalone binaries grabit dispatches when you run `grabit <name> [args]`. they're shipped as a git repo containing a `manifest.toml`. the binary itself is either built in-tree (`[build]`) or fetched as a prebuilt release (`[prebuilt]`).

## paths

| path | role |
|---|---|
| `~/.config/grabit/plugins/<name>/` | install dir (cloned repo + manifest + bundled data; `$XDG_CONFIG_HOME` honored) |
| `~/.config/grabit/plugins/.bin/grabit-<name>` | symlink → the plugin binary |
| `~/.cache/grabit/plugins/<name>/` | per-plugin cache (`$XDG_CACHE_HOME` honored) |
| `~/.config/grabit/plugins/.lock` | flock for install/remove/update |
| `<plugin-dir>/.source` | state file: kind / url / sha256 (one per line) |
| `<plugin-dir>/.last_check` | mtime drives auto-update scheduling |
| `<plugin-dir>/.update.log` | last auto-update's stdout+stderr (truncated each run) |

plugin names must match `[a-z0-9_-]+`.

## cli

```sh
grabit plugin install <git-url>          # clone + build/fetch + install (alias: add)
grabit plugin list                       # alias: ls
grabit plugin show <name>                # parsed manifest
grabit plugin update [<name>]            # update one - or every plugin if omitted
grabit plugin remove <name>              # alias: rm
```

`grabit plugin show <name>` prints: `name`, `description`, `homepage`, `kind` (`build`/`prebuilt`), build cmd + binary or prebuilt url + sha256, `auto-update: <hours>`, `branch`, `auto-capture: yes/no`, plus each declared action.

## running a plugin

```sh
grabit <name> [args]                     # exec the plugin
grabit -p <name> [args]                  # run plugin, then pin its last stdout line as a file
```

dispatch looks only at the first argument, and only when it doesn't start with `-` (`grabit --silent <name>` never dispatches). `set`, `get`, `unset`, `sxcu`, `plugin`, and `help` are matched first, so plugins with those names are unreachable. on a match grabit sets the env vars below, optionally captures (`capture.auto`), and exec's the plugin binary. if there's no match, `grabit <name>` falls through to the usual `unknown argument` error; `grabit -p <name>` does print a dedicated `plugin: <name> not installed`.

### `-p <name>` semantics

- runs the plugin (with its forwarded argv) under a pipe. stdout is buffered up to 16 MiB (truncated with a warning beyond that); stderr streams through untouched.
- after the plugin exits, takes the last newline-terminated chunk of stdout (trimmed) as a file path and execs `grabit --pin -f <path>`.
- if the plugin exits non-zero: any captured stdout is flushed to stderr and grabit exits with the plugin's exit code (1 if it died to a signal).
- if stdout is empty: errors with `plugin: <name> produced no output to pin` and exits 1.
- `capture.auto` and `--capture`/`--no-capture` do not apply under `-p`; those args are forwarded to the plugin verbatim.

### capture.auto

with `capture.auto = true`, grabit captures a screenshot in-process (same as `grabit -o`, honoring your full config) and prepends the saved path as the plugin's first positional arg - but only when the user didn't pass a positional themselves. if the capture fails, grabit exits 1 without running the plugin. per-call overrides:

```sh
grabit <name> --capture                  # force capture even if user passed a positional
grabit <name> --no-capture               # never capture
grabit <name> /path/to/file.png          # passing a positional skips auto-capture
```

`--capture` and `--no-capture` are stripped from the argv forwarded to the plugin.

"positional" means any arg not starting with `-`, except the values of `-o`/`--output`/`--save`/`-f`/`--filename`. the value of any other flag counts as a positional, so `grabit <name> --level 3` suppresses auto-capture.

## manifest.toml

required at the repo root. minimum: `name`, plus at least one of `[build]` or `[prebuilt]`. if both are present, `[prebuilt]` wins and `[build]` is ignored.

### build-from-source

```toml
name = "myplugin"
description = "what it does"

[build]
cmd = "make"                             # required, run via /bin/sh -c in the plugin dir
binary = "build/myplugin"                # required, path relative to the plugin dir
```

### prebuilt binary

```toml
name = "myplugin"

[prebuilt]
url = "https://example.com/myplugin-x86_64"
sha256 = "<hex>"                         # required
```

`sha256` is mandatory: a `[prebuilt]` manifest without it is rejected, since the binary is fetched over the network and cannot be trusted without a pinned hash. there is no GPG / signature support. the hex comparison is case-insensitive. urls must be https; downloads follow at most 8 redirects and time out after 300s.

### optional tables

```toml
description = "shown by `grabit plugin show`"
homepage = "https://..."

[update]
check_every_hours = 24                   # default 24; 0 (or negative) disables auto-update
branch = "main"                          # default "main"; passed to `git fetch origin <branch>`

[capture]
auto = true                              # see capture.auto above

[actions.foo]                            # display-only metadata (shown in `grabit plugin show`)
description = "extra subcommand"
```

`description`, `homepage`, and `[actions.*]` are not consumed by dispatch - they're discoverability metadata.

## install

`grabit plugin install <git-url>` accepts anything `git clone` does, including local paths. the flow:

1. acquires the global plugin lock (non-blocking: a concurrent install/remove/update errors out immediately).
2. `git clone --depth 1 <url>` into a temp dir under `~/.config/grabit/plugins/.tmp.<sanitized>.<pid>`. stale `.tmp.*` dirs from crashed installs are garbage-collected first (skipped while their pid is alive).
3. parses `manifest.toml`. **the install directory is named after `manifest.name`, not the URL.**
4. if a plugin with that name is already installed:
   - same git URL → no-op success ("already installed").
   - different URL → error: run `grabit plugin remove <name>` first.
5. renames temp dir to `~/.config/grabit/plugins/<manifest.name>/`.
6. `[build]`: runs `cmd` via `/bin/sh -c` from the plugin dir.
   `[prebuilt]`: fetches `url`, verifies `sha256`, `chmod +x`.
7. creates the `grabit-<name>` symlink in `.bin/`.
8. writes `.source` (kind/url/sha256) and touches `.last_check`.

## update

```sh
grabit plugin update <name>              # one plugin
grabit plugin update                     # every installed plugin
```

acquires the lock (non-blocking, same as install). behavior depends on the source kind in `.source` (missing/corrupt `.source` is treated as git):

- **git**: `git fetch --depth 1 origin <branch>` then `git reset --hard FETCH_HEAD`. if `[build]` is set, re-runs `cmd`.
- **prebuilt**: refetches the url with `If-Modified-Since` (using the binary's mtime). if the server says 304, logs "up to date". on 200, re-verifies sha256 and atomic-renames `<name>.new` into place.

there is no `--force` flag - `If-Modified-Since` is always honored. any foreground `plugin update` also resets `.last_check`, postponing the next auto check.

**prebuilt caveat**: `.source` stores the prebuilt url, not the git url. updates only refetch the binary - the repo (manifest, bundled files) is never git-pulled, and reinstalling from the original git url errors because the url comparison uses the stored prebuilt url. a changed manifest needs `remove` + `install`.

### auto-update

triggered on every `grabit <name>` and `grabit -p <name>` invocation. flow:

1. tries the lock non-blockingly. if held by another op, **silently skips this round** (no log).
2. if `<plugin-dir>/.last_check` is newer than `check_every_hours` ago, skip.
3. **touches `.last_check` first**, then:
   - **prebuilt plugins** double-fork-detach `grabit plugin update <name>` writing stdout+stderr to `<plugin-dir>/.update.log` (truncated each run, hard-killed after 5 minutes). this is safe because the fetched binary is https-only and pinned to the manifest's `sha256`.
   - **git/build plugins** are **not** auto-updated - doing so would `git reset --hard` to the remote's current head and run the manifest's build command unattended, i.e. run whatever the (untrusted) remote serves. instead grabit just prints a one-line "run `grabit plugin update <name>`" hint (rate-limited by `.last_check`), and you update on demand.

note the touch-before-spawn: a *failed* background update (or an ignored git hint) still postpones the next attempt by `check_every_hours`. if you're debugging an auto-update problem, run `grabit plugin update <name>` in the foreground.

set `check_every_hours = 0` (or any value <= 0) to disable the check entirely.

## remove

```sh
grabit plugin remove <name>
```

`rm -rf`'s the plugin dir and removes the `grabit-<name>` symlink. **the cache dir at `~/.cache/grabit/plugins/<name>/` is not touched** - manage that yourself if your plugin caches sensitive data.

## environment

set by grabit before exec'ing the plugin:

| var | meaning |
|---|---|
| `GRABIT_BIN` | absolute path to the running grabit binary |
| `GRABIT_PLUGIN_NAME` | plugin name (matches `grabit-<name>`) |
| `GRABIT_PLUGIN_DIR` | install dir (manifest + bundled data) |
| `GRABIT_CACHE_DIR` | per-plugin cache dir (created if missing) |

treat these as best-effort: `GRABIT_BIN` can be unset if `/proc/self/exe` is unreadable, and `GRABIT_CACHE_DIR` honors `$XDG_CACHE_HOME` only when it's an absolute path.

## helper header (`include/grabit-plugin.h`)

a single-file, header-only helper. POSIX + libc only - no link-time deps; vendor it into your plugin repo and `#include` it.

```c
#include "grabit-plugin.h"

int main(void) {
    char *path = grabit_plugin_capture();   // forks `grabit -o`, returns the saved path
    if (!path) return 1;
    // ... do work with path ...
    grabit_plugin_pin(path);                // execvp's `grabit --pin -f <path>` - does not return on success
    free(path);
    return 1;
}
```

functions:

| function | behavior |
|---|---|
| `grabit_plugin_capture()` | forks `grabit -o`, captures stdout (capped at 4 KiB), returns malloc'd path string (NULL on failure). caller frees. |
| `grabit_plugin_pin(file)` | `execvp`s `grabit --pin -f file`. replaces the current process; only returns -1 on exec failure. |
| `grabit_plugin_bin()` | absolute path to the running grabit (uses `$GRABIT_BIN`, falls back to `readlink /proc/self/exe`'s sibling, then PATH). |
| `grabit_plugin_dir()` | `$GRABIT_PLUGIN_DIR` (or `"."` if unset). |
| `grabit_plugin_cache_dir()` | `$GRABIT_CACHE_DIR` (or `"/tmp"` if unset). |
| `grabit_plugin_name()` | `$GRABIT_PLUGIN_NAME` (or `"plugin"` if unset). |

you don't have to use the header - plugins can be any language, since dispatch is just exec with env vars set. the header is a convenience for C plugins that want to call back into grabit.
