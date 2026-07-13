# grabit options

everything beyond the at-a-glance README. see `README.md` for install.

## usage

```sh
grabit -c                     # region screenshot → clipboard
grabit -u                     # upload to default service
grabit -o > path.txt          # save and print path
grabit --record               # toggle recording (run again to stop)
grabit --pin                  # pin a region screenshot to the desktop
grabit --tesseract            # ocr a region → clipboard
grabit -e -c                  # annotate before copying
grabit -f file.png -u         # upload an existing file
```

first run writes a default config to `~/.config/grabit/config.toml`.

## features

- region screenshots with native freeze + selector (no `slurp`/`grim` shellouts); uses `zwlr_screencopy_v1` if advertised, else falls back to `ext_image_copy_capture_v1` (KDE Plasma 6)
- on hyprland, the region selector highlights the window under the cursor as a snap target - click to capture it, or drag for a freeform region
- screen recording to mp4/h.264, webm/vp9, or gif with live overlay + sni tray icon
- ocr (capture → text → clipboard) via tesseract
- in-tree annotation editor (`--edit`): pen, rect, ellipse, arrow, blur, text, eraser, hsl color picker + eyedropper, hex input
- pin captures to the desktop (always-on-top, click-through, draggable when grabbed)
- six built-in uploaders: zipline, nest, fakecrime, ez, guns, pixelvault
- import any sharex `.sxcu` uploader
- filename templates
- toml config + `grabit set/get/unset` schema-validated cli
- plugin system (`grabit plugin install <git-url>`)

## build

```sh
make
./build/grabit --version
```

### deps

required:
- json-c
- libcurl
- libmagic
- wayland-client + wayland-cursor
- cairo
- libxkbcommon
- libdbus-1

build-time only (data, no link): `wayland-protocols`, `wayland-scanner`.

optional (auto-detected via pkg-config):
- `libjpeg` (or `libjpeg-turbo`) - enables JPEG output (`format = jpeg`).
- `libwebp` - enables WebP output (`format = webp`).

runtime (no link-time deps, looked up via `$PATH`):
- `ffmpeg` for `--record`
- `tesseract` + the english training data for `--tesseract`

config parser (`tomlc99`) is vendored under `src/vendor/`.

### targets

```sh
make             # release build into build/grabit
make sanitize    # asan + ubsan into build-san/grabit
make install     # to $(DESTDIR)$(PREFIX)/bin/grabit
make clean
make test        # spdx-header lint
make apply-headers
make fmt         # clang-format -i
make fmt-check   # dry-run, errors on diff
```

### development build

generate `compile_commands.json` for clangd / your editor's lsp:

```sh
bear -- make all
```

re-run after adding/removing source files.

## configuration

```sh
grabit set                    # list all settable keys
grabit set <key>              # show example/default for that key
grabit set <key> <value>      # write (validated)
grabit get                    # dump current config
grabit get <key>              # one key
grabit unset <key>
```

config lives at `$XDG_CONFIG_HOME/grabit/config.toml` (else `~/.config/grabit/config.toml`).

### auth tokens

per service, either:

```sh
grabit set services.zipline.auth "<token>"             # plaintext in config (chmod 0600)
```

or via env (preferred, works with password managers):

```sh
export GRABIT_ZIPLINE_AUTH="$(pass show grabit/zipline)"
```

zipline also needs:

```sh
grabit set services.zipline.domain https://your.host
# /api/upload is appended automatically if missing
```

nest accepts an optional `services.nest.folder` (uuid) to upload into a specific folder.

### zipline chunked uploads

zipline v4 supports splitting large uploads into chunks (useful behind cloudflare's 100MB post limit). grabit sends them to `/api/upload/partial` the same way the zipline web client does:

```sh
grabit --record --zipline --chunked      # chunk this one upload
grabit set services.zipline.chunked true # always chunk zipline uploads
grabit set services.zipline.chunk_size 50  # MiB per chunk (default 25, 1-95)
```

the final chunk returns the file URL immediately while the server assembles the chunks in the background, so the link may 404 for a moment on very large files.

grabit also recovers from proxy size limits automatically (cloudflare's free plan caps request bodies at 100MB): a plain zipline upload rejected with HTTP 413 is retried in chunks, and a chunk that still gets 413'd is retried with progressively halved chunks (down to 1 MiB).

### zipline custom headers

zipline supports per-upload metadata via headers. set them with `services.zipline.headers.<name>`:

| header | accepted values |
|---|---|
| `x-zipline-format` | `random`, `date`, `uuid`, `name`, `gfycat` - **defaults to `name`** so the uploaded URL preserves grabit's filename template (e.g. `%w`, `%Y-%m-%d`). Set it explicitly to override. |
| `x-zipline-image-compression-percent` | 0-100 |
| `x-zipline-image-compression-type` | `jpg`, `png`, `webp`, `jxl` |
| `x-zipline-password` | string |
| `x-zipline-max-views` | non-negative integer |
| `x-zipline-no-json` | `true` |
| `x-zipline-original-name` | `true` |
| `x-zipline-folder` | folder id |
| `x-zipline-filename` | string |
| `x-zipline-domain` | string |
| `x-zipline-file-extension` | string |
| `x-zipline-deletes-at` | duration string (e.g. `1d`, `30m`) |

unknown header names are forwarded as-is with a warning.

### sharex (.sxcu) uploaders

import any sharex custom uploader file:

```sh
grabit sxcu add  ~/Downloads/myhost.sxcu     # parse, sanitize name, copy into config dir
grabit sxcu list                              # registered names (alias: ls)
grabit sxcu show <name>                       # parsed fields (url, method, headers, ...)
grabit sxcu remove <name>                     # alias: rm
```

added uploaders live at `~/.config/grabit/uploaders/<name>.sxcu` (chmod 0600). once added, use them like a built-in:

```sh
grabit --<name>                               # screenshot + upload
grabit -f file.png --<name>                   # upload an existing file
```

supported sxcu fields: `RequestURL`, `RequestMethod`, `Body` (`MultipartFormData`/`FormURLEncoded`/`JSON`/`XML`/`Binary`/`None`), `FileFormName`, `Headers`, `Parameters`, `Arguments`, `Data`, `URL`, `ErrorMessage`, `RegexList`, `DestinationType`.

placeholders in url/headers/args/data: `{filename}`, `{base64:...}`, `{random:a|b|c}`, `{select:a|b|c}`, `{prompt:label|default}` (alias `{inputbox:label|default}`). response placeholders for the `URL`/`ErrorMessage` templates: `{response}`, `{responseurl}`, `{json:path.to[0].field}`, `{regex:pattern|group}`, `{regex:N|group}` (N indexes `RegexList`), `{header:Name}`.

auth lives inside the `.sxcu` `Headers` block - no separate `services.<name>.auth` config needed.

### default action

if no `-c`/`-u`/`-o`/`--<service>` is passed, falls back to:

```sh
grabit set default_action copy        # one of: copy, upload, save, pin
```

### top-level keys

| key | type | notes |
|---|---|---|
| `default_action` | enum | `copy`/`upload`/`save`/`pin` (default `copy`) |
| `service` | string | default upload target when `default_action=upload` (one of the built-ins or an sxcu name) |
| `notifications` | bool | enable desktop notifications (default `true`) |
| `also_save` | bool | also save a copy when copying/uploading (default `false`). Alias: `save_captures` (legacy). |
| `save_dir` | string | save dir for screenshots and recordings (takes precedence over `XDG_VIDEOS_DIR`; default `~/Pictures` for screenshots, `XDG_VIDEOS_DIR` else `~/Videos` for videos) |
| `editor` | string | external editor binary for `-e` text tool (optional) |
| `filename` | string | filename template (see "filename templates" below) |
| `filename_preset` | enum | `date`/`random`/`uuid`/`timestamp` |
| `format` | enum | screenshot output format: `png`/`jpeg`/`webp` (default `png`). per-run override: `--format <name>` |

### capture backend

| key | default | notes |
|---|---|---|
| `capture.backend` | `auto` | `auto` picks `wlr` (wlroots/hyprland/sway/niri/river) and falls back to `ext` (KDE Plasma 6). Force one with `wlr` or `ext`. |
| `capture.cursor` | `true` | include the mouse pointer in screenshots; set `false` to hide it (recordings use `recording.cursor`) |

### region selector

| key | default | notes |
|---|---|---|
| `region.window_snap` | `true` | on hyprland, hover-highlight visible windows and click to capture one; set `false` to always require a drag |
| `region.confirm` | `false` | keep the selection adjustable after releasing the drag (flameshot-style): resize with the handles or Shift+arrows, move by dragging inside or with the arrow keys (hold to accelerate), drag outside to start over, then press Enter, Ctrl+C, or double-click inside it to capture; Esc cancels |

in any selector, `ctrl+a` selects the whole monitor under the cursor: with `region.confirm` (or `-e`) it locks for adjustment, otherwise it captures immediately.

### encoder options

| key | default | notes |
|---|---|---|
| `jpeg.quality` | `90` | JPEG quality 1–100 |
| `webp.quality` | `85` | WebP quality 0–100 (ignored when `webp.lossless = true`) |
| `webp.lossless` | `false` | use WebP lossless mode |

JPEG and WebP support is detected at build time via `pkg-config libjpeg` and `pkg-config libwebp`. If a format wasn't compiled in, picking it at runtime errors out clearly. PNG is always available.

## recording

```sh
grabit --record               # start (region selector, then begins)
grabit --record               # stop
```

while recording you'll see:
- a thin red border around the captured region
- a recording icon in your status bar tray (waybar with `tray` module, etc.)

clicking the tray icon stops the recording.

per-recording overrides:
- `grabit --record --no-upload`: skip auto-upload even if `default_action=upload`
- `grabit --record --zipline`: upload to a specific service after recording

when uploading, the recording follows the same `also_save` rule as screenshots: if `also_save = false` (the default) the mp4 is written to a temp file and deleted after a successful upload. set `also_save = true` to keep a local copy in your videos dir.

config keys (all optional):

| key | default | notes |
|---|---|---|
| `recording.fps` | 30 | 1-120 |
| `recording.format` | `mp4` | `mp4` (h.264), `webm` (vp9), or `gif`. `crf` applies to mp4/webm; `preset`/`tune`/`max_size_mb` are mp4-only; gif ignores all encoder keys |
| `recording.crf` | 23 | 0-51 (lower = higher quality) |
| `recording.preset` | `fast` | one of: `ultrafast`, `superfast`, `veryfast`, `faster`, `fast`, `medium`, `slow`, `slower`, `veryslow` |
| `recording.tune` | (none) | one of: `film`, `animation`, `grain`, `stillimage`, `psnr`, `ssim`, `fastdecode`, `zerolatency` |
| `recording.pix_fmt` | `yuv420p` | one of: `yuv420p`, `yuv422p`, `yuv444p`, `yuv420p10le` |
| `recording.cursor` | `true` | record the cursor |
| `recording.max_size_mb` | (none) | re-encode if file exceeds this (0-100000) |
| `recording.ffmpeg` | `ffmpeg` | path to ffmpeg binary |

## sound

play a shutter sound on capture (off by default):

| key | default | notes |
|---|---|---|
| `sound.enabled` | `false` | toggle |
| `sound.player` | (auto) | path to player binary (auto-detects `pw-play`, `paplay`, `play`, `aplay`) |
| `sound.file` | (auto) | path to audio file (auto-detects standard freedesktop camera-shutter sounds) |

## pin

```sh
grabit --pin                  # capture a region; pins it to the desktop where it was grabbed
grabit --grab                 # all pins become interactive (X close button + draggable)
grabit --release              # pins go back to click-through
grabit --close-all            # dismiss every pin
```

each pin is a long-lived process holding a wlr-layer-shell overlay surface. they stack as you create them, are click-through by default, and ignore other layers' exclusive zones (so the position matches exactly where the region was selected, even with status bars).

interactive mode is meant to be wired to a hold-bind in your compositor. example for hyprland:

```
bindrn = SUPER SHIFT, mouse:272, exec, grabit --grab
bindrn = SUPER SHIFT, mouse:272, release, exec, grabit --release
```

while grabbed, click anywhere to drag, click the X in the top-right to close that pin.

requires `zwp_relative_pointer_manager_v1` for drag (universal in modern wlroots compositors).

## ocr

```sh
grabit --tesseract            # select a region; text lands in clipboard
```

requires `tesseract` on `$PATH` and `eng.traineddata` (typically in `/usr/share/tessdata/` or `$TESSDATA_PREFIX`). override the binary with `grabit set ocr.tesseract /custom/path/tesseract`.

### translate

```sh
grabit --tesseract --translate            # uses `translate.target` (default en)
grabit --tesseract --translate=ja         # per-run target override
grabit set translate.target de            # set a default target
```

`--translate` pipes the OCR result through [translate-shell](https://github.com/soimort/translate-shell)'s `trans` binary (target via `-t`, source auto-detected) and copies the translation to the clipboard instead of the raw OCR text. install `translate-shell` from your distro to enable it. if `trans` is missing or the translate call fails/times out (20s cap), grabit falls back to copying the raw OCR text and fires a notification.

### show on screen

combine `--tesseract` with `--show` to render the result on screen as a transient text card (dark background, word-wrapped) instead of copying:

```sh
grabit --tesseract --show                 # show the raw OCR
grabit --tesseract --translate --show     # show the translation
grabit set text_card.dismiss_secs 12      # auto-dismiss after 12s (default 8, 0 = stay until clicked)
```

**`text_card.*`** (configures the text card shown by `--tesseract --show`):

| key | default | notes |
|---|---|---|
| `text_card.dismiss_secs` | `8` | auto-dismiss after N seconds (0-600; 0 = stay until replaced) |
| `text_card.position` | `top-right` | `top-left`/`top-center`/`top-right`/`bottom-left`/`bottom-center`/`bottom-right`/`center` |
| `text_card.output` | (primary) | output name (e.g. `DP-1`, `HDMI-A-1`). if the named output isn't connected, falls back to the primary output |

**`preview.*`** (post-capture thumbnail, independent of `text_card.*`):

| key | default | notes |
|---|---|---|
| `preview.enabled` | `false` | after a successful `-c` / `-u` / `-o`, show a sharex-style preview card |
| `preview.size` | `300` | thumbnail width in pixels (100-800); the height keeps the screenshot's aspect ratio (no padding, no boxy frame) |
| `preview.position` | `bottom-right` | same value set as `text_card.position` |
| `preview.output` | (primary) | same semantics as `text_card.output` |
| `preview.dismiss_secs` | `5` | auto-dismiss after N seconds (0-600; 0 = stay until next capture) |

the preview is the scaled screenshot with a thin dark border. hovering over it overlays a translucent centered caption bar at the bottom (`Copied`, `Uploaded`, or the bare filename for saves), and re-arms the auto-dismiss timer (so the card stays as long as you keep mousing over it). clicking the preview:

- after `-u`: runs `xdg-open <url>` (opens the upload link in your browser)
- after `-o`: runs `xdg-open <dir>` (opens the containing folder in your file manager)
- after `-c`: just dismisses (the file may already be gone if it was a temp)

only one preview card is on screen at a time.

the card is click-through (no input region). running `--show` again kills any previous card via a pid file in `$XDG_RUNTIME_DIR/grabit-show.pid`, so only one card is ever on screen. if no monitor is connected at all, grabit fires a "show failed: no monitor is connected" notification and exits.

note: tesseract is currently invoked with `-l eng`, so non-latin source scripts (japanese, chinese, cyrillic, etc.) won't OCR cleanly and the translation will reflect that. results are best when the source is a latin-script language that `tesseract-data-eng` can still read passably (spanish, french, german, etc.).

## fullscreen

```sh
grabit -F -o                  # whole-monitor screenshot (picker if multi-monitor)
grabit --fullscreen -c        # copy a whole monitor to the clipboard
grabit --fullscreen=2 -u      # upload monitor 2 directly (no picker)
grabit --fullscreen=DP-1 -e -o   # annotate the whole monitor, then save
grabit --record -F            # record a whole monitor
```

`-F`/`--fullscreen` captures a whole monitor instead of dragging a region. it pairs with any capture action (`-c`, `-u`, `-o`, `--<service>`, `--pin`, `--tesseract`, `--record`) and with `-e`; it cannot be combined with `-f`.

monitor selection:

- one monitor connected: that monitor is grabbed directly, no UI.
- multiple monitors, no target: the region selector opens dimmed and snaps to whole monitors. hover a monitor to highlight it, click to grab it (same as window-snapping, but for monitors). dragging still works if you want a custom region.
- `--fullscreen=<n>`: pick by 1-based number directly, no picker (the order shown in the printed monitor list).
- `--fullscreen=<name>`: pick by output name directly, e.g. `--fullscreen=DP-1`.
- `--fullscreen=all`: capture every monitor stitched into one image (gaps in the layout come out black).

an unknown number or name prints the available monitors and exits.

the mouse pointer is included in screenshots by default (`capture.cursor`, default `true`); `--cursor` forces it on for one run even when the config disables it. recordings have their own `recording.cursor`.

with `-e`/`--edit`, once a monitor is chosen it opens as the locked region with the annotation toolbar (no drag step). with `--record`, the chosen monitor becomes the recording region.

## edit

```sh
grabit -e -c                  # annotate, then copy
grabit -e -u                  # annotate, then upload
grabit -e -o                  # annotate, then save
```

`-e`/`--edit` pairs with any action. a flameshot-style toolbar sits at the top of the monitor the mouse is on from the moment the overlay opens; drag it by its background to park it anywhere and it stays put for the rest of that invocation. the overlay starts in region-select mode, but picking any tool (click or `1`–`9`) switches to drawing immediately: you can annotate anywhere on the frozen screen before a region exists, then click the **select region** button to drag out the capture area (save stays disabled until one is set). tools:

- **select region** - drag out or replace the capture area
- **move/resize** (`s`) - click an annotation to select it, drag to move it, drag the corner handles of shapes/lines/arrows to resize them (strokes and text are move-only)
- **pen, marker, line, rect, ellipse, arrow, blur, text, eraser** - keyboard shortcuts `1`–`9`, or by letter: `p` pen, `m` marker, `l` line, `r` rect, `o` ellipse, `a` arrow, `b` blur, `t` text, `e` eraser
- **6 preset color swatches** + a current-color square (click to open the picker)
- **hsl picker panel**: drag in the gradient, type a hex value (`#rrggbb` or `#rgb`), or click the eyedropper to sample a pixel from the screen
- **width slider** (1–12 in the toolbar, or scroll the mouse wheel anywhere; the persisted `edit.width` accepts up to 20 if you set it via the cli). with the text tool active, the wheel sizes the text (8–72) instead
- **undo** (`u` or `ctrl+z`, hold to repeat) - steps back through annotations, annotation moves/resizes, and region changes (move, resize, re-select) alike / **save** (`enter`) / **cancel** (`esc` or right-click)
- **resize handles** on the locked region; **ctrl+drag** inside to move the whole region
- **shift** while drawing constrains rect/ellipse/blur to squares and arrows/lines to 45° angles

last-picked color, width, and tool persist via:

| key | default | notes |
|---|---|---|
| `edit.color` | `#ff3030` | `#rrggbb`, `#rgb`, or one of red/yellow/green/blue/black/white |
| `edit.width` | `4` | integer 1-20 |
| `edit.tool` | `pen` | one of: `pen`, `marker`, `line`, `rect`, `ellipse`, `arrow`, `blur`, `text`, `eraser` - the editor reopens with your last-used tool |
| `edit.default` | `false` | when `true`, every capture opens the editor (same as passing `-e` to every run; applies to copy/upload/save/pin, ignored for `-f`/record/OCR) |

## filename templates

`grabit --filename '<tpl>'` (per-run) overrides `filename` config (per-user). tokens:

| token | expands to |
|---|---|
| `%Y %m %d %H %M %S` | date/time fields |
| `%s` | unix timestamp |
| `%r` | 12-char random alnum (`%r8` etc. picks length) |
| `%u` | uuid v4 |
| `%w` | active window class (hyprland ipc) |
| `%t` | active window title (hyprland ipc) |
| `%%` | literal `%` |

presets via `filename_preset`:
- `date`: `%Y-%m-%d-%H-%M-%S` (default)
- `random`: `%r12`
- `uuid`: `%u`
- `timestamp`: `%s`

`%w`/`%t` resolve to empty on non-hyprland compositors.

## flags

action flags:

| flag | meaning |
|---|---|
| `-c` | copy to clipboard |
| `-u` | upload to default service |
| `-o` / `--output` / `--save` | save and print path |
| `--<service>` | upload to a specific built-in or sxcu service |
| `--record` | toggle recording |
| `--pin` | pin a region to the desktop |
| `--grab` / `--release` / `--close-all` | manage existing pins |
| `--tesseract` | ocr a region into the clipboard |
| `--translate[=<lang>]` | with `--tesseract`: translate the ocr text and copy that instead (see ocr/translate section) |
| `-e` / `--edit` | annotate before the action |

modifiers:

| flag | meaning |
|---|---|
| `-F` / `--fullscreen` | capture a whole monitor; multi-monitor opens a monitor picker (see fullscreen section). works with `--record` too |
| `--fullscreen=<n\|name>` | capture a specific monitor directly by 1-based number or output name (e.g. `--fullscreen=DP-1`), no picker |
| `-f <file>` | use an existing file instead of capturing |
| `--filename <tpl>` (or `--filename=<tpl>`) | per-run filename template |
| `--format <png\|jpeg\|webp>` (or `--format=<name>`) | per-run output format |
| `--no-tray` | suppress the recording tray icon |
| `--no-upload` | with `--record`, skip the auto-upload after recording |
| `--silent` / `-q` / `--quiet` | suppress info logging (errors still print) |
| `-d` / `--debug` | enable debug logging |

## environment

| var | effect |
|---|---|
| `GRABIT_DEBUG=1` | enable debug logging (same as `-d`) |
| `GRABIT_<SERVICE>_AUTH` | per-service auth token (overrides config) |
| `XDG_RUNTIME_DIR` | recording pid file + per-pin ipc sockets live here if set, else `/tmp` |
| `XDG_VIDEOS_DIR` | recording save dir (`save_dir` config takes precedence; else this, else `~/Videos`). Screenshots use `save_dir` else `~/Pictures` |
| `TESSDATA_PREFIX` | tesseract language-data dir |
