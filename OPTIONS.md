# grabit options

## usage

```sh
grabit -c                     # region screenshot -> clipboard
grabit -u                     # upload to default service
grabit -o > path.txt          # save and print path
grabit --record               # toggle recording (run again to stop)
grabit --pin                  # pin a region screenshot to the desktop
grabit --tesseract            # ocr a region -> clipboard
grabit -e -c                  # annotate before copying
grabit -f file.png -u         # upload an existing file
```

first run writes a default config to `~/.config/grabit/config.toml`.

## build

```sh
make
./build/grabit --version
```

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
make clangd-check  # parse every source with clangd, fail on errors
```

`clangd-check` generates `compile_commands.json` with `bear` if it is missing, then
runs `clangd --check` over each source. one file at a time with `FILE=`:

```sh
make clangd-check FILE=src/record/record.c
```

### development build

generate `compile_commands.json` for clangd / your editor's lsp:

```sh
bear -- make all
```

re-run after adding/removing source files. `make clangd-check` generates it for you
when it is absent, but not when it is merely stale.

## configuration

```sh
grabit set                    # list all settable keys
grabit set <key>              # show example/default for that key
grabit set <key> <value>      # write (validated)
grabit set <key>=<value>      # same, single-argument form
grabit get                    # dump current config
grabit get <key>              # one key
grabit unset <key>
grabit help [<topic>]         # set/get/unset/sxcu/plugin/filename/env/examples/ocr
```

every subcommand also takes `--help` / `-h` directly, e.g. `grabit set --help`.

### config vs state

config lives at `$XDG_CONFIG_HOME/grabit/config.toml` (else `~/.config/grabit/config.toml`). it is yours: grabit only writes it when you run `set` or `unset`.

anything grabit decides on its own goes to `$XDG_STATE_HOME/grabit/state.toml` (default `~/.local/state/grabit/state.toml`) instead: the last-used `edit.color`, `edit.width` and `edit.tool`, the editor toolbar position (`edit.toolbar_pos`), and the last captured region (`region.last`). state wins over config, so setting those keys in config.toml just picks the starting value. if you already had them in config.toml they are copied over once, with a note; the entries left behind are harmless and can be deleted. set `save_state = false` to turn the whole state file off.

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
| `x-zipline-no-json` | `true` only (see below) |
| `x-zipline-original-name` | `true` only (see below) |
| `x-zipline-folder` | folder id |
| `x-zipline-filename` | string |
| `x-zipline-domain` | string |
| `x-zipline-file-extension` | string |
| `x-zipline-deletes-at` | duration string (e.g. `1d`, `30m`) |

`x-zipline-no-json` and `x-zipline-original-name` are presence flags: zipline enables them whenever the header is sent, whatever the value, so `false` would not disable them and grabit rejects it. `grabit unset services.zipline.headers.<name>` turns them off.

unknown header names are forwarded as-is with a warning.

### sharex (.sxcu) uploaders

import any sharex custom uploader file:

```sh
grabit sxcu add  ~/Downloads/myhost.sxcu     # parse, sanitize name, copy into config dir (alias: install)
grabit sxcu add  myhost.sxcu --force          # replace an uploader that already exists
grabit sxcu list                              # registered names (alias: ls)
grabit sxcu show <name>                       # parsed fields (auth masked; --show-secrets unmasks)
grabit sxcu remove <name>                     # alias: rm
```

added uploaders live at `~/.config/grabit/uploaders/<name>.sxcu` (chmod 0600). once added, use them like a built-in:

```sh
grabit --<name>                               # screenshot + upload
grabit -f file.png --<name>                   # upload an existing file
```

supported sxcu fields: `RequestURL`, `RequestMethod`, `Body` (`MultipartFormData`/`FormURLEncoded`/`JSON`/`XML`/`Binary`/`None`), `FileFormName`, `Headers`, `Parameters`, `Arguments`, `Data`, `URL`, `ErrorMessage`, `RegexList`.

placeholders in url/headers/args/data: `{filename}`, `{base64:...}`, `{random:a|b|c}` and `{select:a|b|c}` (both expand to the first alternative), `{prompt:label|default}` / `{inputbox:label|default}` (expands to the default; grabit never prompts). response placeholders for the `URL`/`ErrorMessage` templates: `{response}`, `{responseurl}`, `{json:path.to[0].field}`, `{regex:pattern|group}`, `{regex:N|group}` (N indexes `RegexList`), `{header:Name}`.

auth lives inside the `.sxcu` `Headers` block - no separate `services.<name>.auth` config needed.

### top-level keys

| key | type | notes |
|---|---|---|
| `default_action` | enum | `copy`/`upload`/`save`/`pin` (default `copy`) |
| `service` | string | default upload target when `default_action=upload` (one of the built-ins or an sxcu name) |
| `notifications` | bool | enable desktop notifications (default `true`); same forced-failure caveat as `--silent` below |
| `log_file` | bool | mirror every log line to `$XDG_RUNTIME_DIR/grabit.log` (default `true`). set `false` for stderr only; `GRABIT_LOG_FILE` overrides this either way |
| `also_save` | bool | also save a copy when copying/uploading (default `false`). Alias: `save_captures` (legacy). |
| `save_state` | bool | read and write `state.toml` (default `true`). set `false` and grabit keeps nothing between runs: the last-used `edit.color`/`edit.width`/`edit.tool`, the toolbar position, and the last region (`-L`/`--last`, `region.repeat_last`) all stop persisting |
| `save_dir` | string | save dir for screenshots and recordings (takes precedence over the XDG dirs; else `XDG_PICTURES_DIR` then `~/Pictures` for screenshots, `XDG_VIDEOS_DIR` then `~/Videos` for recordings) |
| `filename` | string | filename template (see "filename templates" below) |
| `filename_preset` | enum | `date`/`random`/`uuid`/`timestamp` |
| `format` | enum | screenshot output format: `png`/`jpeg`/`webp` (default `png`). per-run override: `--format <name>` |

### capture backend

| key | default | notes |
|---|---|---|
| `capture.backend` | `auto` | `auto` picks `wlr` (wlroots/hyprland/sway/niri/river), then `ext`, then `kwin` (KDE Plasma, via the `org.kde.KWin.ScreenShot2` dbus service). Force one with `wlr`, `ext`, or `kwin`. |
| `capture.delay` | `0` | seconds to wait before capturing, so you can open a menu or tooltip first (`--delay <secs>` overrides per run, max 3600). for screenshots the wait happens before the screen is frozen, so whatever you open during it is captured; for `--record` it happens after the region is picked, right before recording starts |
| `capture.cursor` | `true` | include the mouse pointer in screenshots; set `false` to hide it (recordings use `recording.cursor`) |

### region selector

| key | default | notes |
|---|---|---|
| `region.window_snap` | `true` | hover-highlight visible windows and click to capture one; set `false` to always require a drag. needs window geometry from compositor ipc, which hyprland reports for every window and niri only for floating ones |
| `region.window_radius` | `auto` | round the corners of window captures to match the compositor. `auto` reads hyprland's `decoration:rounding` (and stays square for fullscreen windows); `0`..`100` forces a radius. applies to `-w`/`--window` and to click-to-snap selections, never to a manual drag. png and webp keep the corners transparent; jpeg cannot store alpha so it is left square with a warning; recordings get black corners |
| `region.snap_animation` | `false` | animate the window-snap highlight: it slides and resizes between windows and fades out when the cursor leaves them, instead of jumping. needs `region.window_snap` |
| `region.confirm` | `false` | keep the selection adjustable after releasing the drag: resize with the handles or Shift+arrows, move by dragging inside or with the arrow keys (hold to accelerate), drag outside to start over, then press Enter, Ctrl+C, or double-click inside it to capture; Esc cancels |
| `region.repeat_last` | `false` | reuse the last captured region instead of opening the selector, same as passing `-L`/`--last`. applies to screenshots and `--record`. with `-e` the region is applied and locked, so the editor opens on the last tool instead of in region-select mode. `-F` still wins, and `--no-last` forces the selector for one run |
| `region.last` | | the last captured region as `<x>,<y>,<w>,<h>`; written automatically after each region capture or recording (state, not config) |

in any selector, `ctrl+a` selects the whole monitor under the cursor: with `region.confirm` (or `-e`) it locks for adjustment, otherwise it captures immediately.

### gui

| key | default | notes |
|---|---|---|
| `gui.radius` | `0` | round the corners of grabit's own ui: the region selector and editor, the recording control bar, and the preview and ocr cards. `0` is square, `auto` matches hyprland's `decoration:rounding`, or set a radius in logical pixels (`8` is a good start). one value scales everything; nothing changes size or position. it never affects the captured image (that is `region.window_radius`) |

### keybinds

every key and mouse action in the selector/editor is rebindable under the `keys.*` namespace. each value is a comma-separated list of bindings, and an action fires when any of its bindings match. a binding is either a key name with optional `Ctrl+`/`Shift+`/`Alt+`/`Super+` modifiers (key names are xkb keysyms, e.g. `Return`, `Escape`, `KP_Enter`, `Left`, `space`, `a`, `F1`), or a mouse button. letters match case-insensitively; the left mouse button is always the draw/select button and cannot be rebound.

mouse buttons are written `mouse:<button>`, where `<button>` is a name (`left`, `right`, `middle`, `back`, `forward`, `side`, `extra`) or a raw evdev button code (`mouse:272` = left, `273` = right, `274` = middle, same numbering as hyprland/sway). the names `back`/`forward`/`side`/`extra` are device-dependent: most mice report the two thumb buttons as `side` (physical back) and `extra` (physical forward), not `back`/`forward`. if a named binding does not fire, find the real code with `wev` or `libinput debug-events` and bind that number.

| key | default | action |
|---|---|---|
| `keys.confirm` | `Return, KP_Enter, Ctrl+c` | capture/save the selection (copy/upload path chosen elsewhere) |
| `keys.cancel` | `Escape, mouse:right` | cancel; while dragging/typing it aborts that instead |
| `keys.select_all` | `Ctrl+a` | select the whole monitor under the cursor |
| `keys.undo` | `u, Ctrl+z` | undo the last annotation (edit mode) |
| `keys.edit_mode` | `s` | switch to the annotation select/edit tool |
| `keys.region_mode` | `q` | switch back to region-select |
| `keys.nudge_left` / `_right` / `_up` / `_down` | `Left, KP_Left` etc. | move a locked selection by one pixel (hold to accelerate) |
| `keys.tool.<name>` | `p, 1` ... `e, 9` | pick a tool; `<name>` is one of pen, marker, line, rect, rounded_rect, ellipse, arrow, arrow_pen, blur, pixelate, spotlight, text, counter, callout, eraser (`rounded_rect` and `arrow_pen` have no binding of their own; press `r` or `a` again to cycle their group and reach them) |

example: to make the right mouse button save instead of cancel (so a quick `-e` capture is `left`-drag then `right`-click), swap them:

```sh
grabit set keys.confirm "Return, KP_Enter, mouse:right, Ctrl+c"
grabit set keys.cancel "Escape"
```

instead of typing bindings by hand, `--watch` (or `-w`) captures them live in the terminal: grabit starts listening, you press the keys and mouse buttons you want, and each one is echoed as it is captured. `Enter` saves and `Esc` cancels. the captured set replaces the action's current bindings. this is the reliable way to bind an odd mouse button, since it records the exact code the device emits. the left mouse button is reserved (draw/select) and is ignored; to bind `Enter` or `Esc` themselves, set them by hand.

```sh
grabit set keys.confirm --watch     # press right mouse, then Enter -> keys.confirm = mouse:right
```

`--reset` restores defaults by dropping the config entry (an unset binding falls back to its compiled-in default). reset one action, or the whole `keys` namespace at once:

```sh
grabit set keys.confirm --reset     # keys.confirm back to its default
grabit set keys --reset             # every keybind back to defaults
```

### encoder options

| key | default | notes |
|---|---|---|
| `png.level` | `1` | PNG zlib compression 0-9. `1` is the default because PNG is lossless: higher levels only shrink the file, and level 6 (what cairo used before) costs roughly 2-6x the encode time for ~15-20% less size. Raise it if you care more about upload size than shutter latency. |
| `jpeg.quality` | `90` | JPEG quality 1-100 |
| `webp.quality` | `85` | WebP quality 0-100 (ignored when `webp.lossless = true`) |
| `webp.lossless` | `false` | use WebP lossless mode |

JPEG and WebP support is detected at build time via `pkg-config libjpeg` and `pkg-config libwebp`. If a format wasn't compiled in, picking it at runtime errors out clearly. PNG is always available.

## recording

```sh
grabit --record               # start (region selector, then begins)
grabit --record               # stop
```

### backends

| compositor | source | notes |
|---|---|---|
| wlroots (hyprland, sway, niri, river) | `wlr-screencopy` / `ext-image-copy` | grabit streams frames itself |
| KDE Plasma | `zkde_screencast_unstable_v1` | needs an installed grabit, see below |
| GNOME | `org.gnome.Mutter.ScreenCast` | recording only, no region selector |

KDE and GNOME both hand back a pipewire node, so a `libpipewire-0.3` build is required for either. neither shows a portal dialog.

KWin keeps `zkde_screencast_unstable_v1` on an interface blacklist and only grants it to programs whose installed `.desktop` declares `X-KDE-Wayland-Interfaces`, so recording on KDE needs `make install` rather than a build directory. `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` bypasses it for testing.

GNOME has no `zwlr_layer_shell_v1`, so the region selector, overlay and control bar do not appear. pick the area up front with `-F`/`--fullscreen=<monitor>` or `-L`/`--last`, and stop with a second `grabit --record`. `org.gnome.Mutter.ScreenCast` is a private API, so a GNOME upgrade can break recording there.

while recording you'll see:
- a thin red border around the captured region
- a control bar (start / pause / stop / abort, plus a state dot and elapsed timer) at the top of the current monitor, or the nearest spot that stays out of the recording; drag it by its background to move it, same as the editor toolbar
- a recording icon in your status bar tray (waybar with `tray` module, etc.)

the pause button finishes the current encoder segment; resume (the start button) begins a new one, and stopping stitches the segments together, so paused time never appears in the output (no frozen gap). the timer counts recorded time only. if the region covers every monitor there's nowhere to put the bar, so it's skipped; stop with the tray icon or by re-running `grabit --record`.

pause is also scriptable: sending `SIGUSR1` to the recording process toggles it, so a compositor keybind like `pkill -USR1 -x grabit` pauses/resumes without touching the mouse.

after stopping, encoders may still be flushing buffered frames; grabit shows a "Recording finishing" notification while it waits and stitches, then the usual saved/uploaded notification. heavy CPU load (a game, many pauses) stretches this phase; a faster `recording.preset` like `superfast` shrinks it.

clicking the tray icon also stops the recording.

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
| `recording.tray` | `true` | show the tray icon while recording; `--no-tray` overrides one run |
| `recording.show_dimensions` | `true` | display dimension badge on the active recording region overlay |
| `recording.max_size_mb` | (none) | re-encode if file exceeds this (0-100000) |
| `recording.ffmpeg` | `ffmpeg` | path to ffmpeg binary |

## tray

`grabit --tray` starts a persistent StatusNotifierItem with every action in its menu. running it a second time stops the one already running.

| key | default | notes |
|---|---|---|
| `tray.icon` | `camera-photo` | icon name looked up in your icon theme |

left click captures a region, right click opens the menu. if the icon collides with another app's, point `tray.icon` at any other name your theme ships:

```sh
grabit set tray.icon camera-video
```

the recording tray shown during `--record` is separate and always uses `media-record`.

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

requires `tesseract` on `$PATH` and the training data for the language you OCR in. override the binary with `grabit set ocr.tesseract /custom/path/tesseract`. when `ocr.tesseract` is unset, grabit probes `tesseract-ocr` before `tesseract` on `$PATH`.

| key | default | notes |
|---|---|---|
| `ocr.tesseract` | `tesseract` | path to the tesseract binary |
| `ocr.lang` | `eng` | language passed to tesseract's `-l`. combine with `+` (e.g. `eng+deu`). needs the matching traineddata installed; list what you have with `tesseract --list-langs` |

```sh
grabit set ocr.lang deu                   # ocr german
grabit set ocr.lang eng+deu               # both at once
```

### translate

```sh
grabit --tesseract --translate            # uses `translate.target` (default en)
grabit --tesseract --translate=ja         # per-run target override
grabit set translate.target de            # set a default target
grabit set translate.backend libretranslate
grabit set translate.url http://localhost:5000
```

`--translate` translates the OCR result and copies that to the clipboard instead of the raw OCR text. if the translate call fails or times out, grabit falls back to copying the raw OCR text and fires a notification.

> **privacy:** translating sends your OCR'd screen text to whatever server the backend talks to. `trans` sends it to google. `libretranslate` sends it wherever `translate.url` points - use your own instance to keep it on your machine.

| key | default | notes |
|---|---|---|
| `translate.target` | `en` | iso-639-1 target code; source is auto-detected |
| `translate.backend` | `trans` | `trans`, `libretranslate`, or `deepl` |
| `translate.url` | (unset) | server url. required for `libretranslate`; optional override for `deepl`. the api path is appended if you leave it off |
| `translate.api_key` | (unset) | api key. required for `deepl`, optional for `libretranslate`. `GRABIT_TRANSLATE_KEY` overrides it |

- `trans` pipes the text through [translate-shell](https://github.com/soimort/translate-shell)'s `trans` binary (target via `-t`). needs the `trans` binary on `$PATH`; without it grabit copies the raw OCR text.
- `libretranslate` POSTs to a [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) server (`/translate`, json in, json out). no extra binary needed, and pointing it at a local instance means the text never leaves your machine:

```sh
grabit set translate.backend libretranslate
grabit set translate.url http://localhost:5000
```

- `deepl` POSTs to the [DeepL API](https://developers.deepl.com) with your own key. grabit picks the endpoint from the key: keys ending in `:fx` are Free plan and go to `api-free.deepl.com`, everything else goes to `api.deepl.com`. override with `translate.url` if needed.

```sh
export GRABIT_TRANSLATE_KEY=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx:fx
grabit set translate.backend deepl
grabit set translate.target EN-US            # deepl codes: EN-US, PT-BR, ZH-HANS, DE ...
```

  deepl language codes are not plain iso-639-1 - regional variants like `EN-US`, `EN-GB`, `PT-BR`, `ZH-HANS` and `ES-419` are passed through as you set them (upper-cased). grabit reports quota exhaustion (`http 456`), rate limiting (`429`), and key/endpoint problems (`403`) distinctly.

  **note:** the deepl free tier may retain submitted content; prefer a paid key or a self-hosted libretranslate instance for sensitive screen content.

### show on screen

combine `--tesseract` with `--show` to render the result on screen as a transient text card (dark background, word-wrapped). the text is still copied; add `--no-copy` to show without copying:

```sh
grabit --tesseract --show                 # show the raw OCR
grabit --tesseract --translate --show     # show the translation
grabit --tesseract --show --no-copy       # show only, leave the clipboard alone
grabit set text_card.dismiss_secs 12      # auto-dismiss after 12s (default 8, 0 = stay until replaced)
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
| `preview.enabled` | `false` | after a successful `-c` / `-u` / `-o`, show a preview card |
| `preview.size` | `300` | thumbnail width in pixels (100-800); the height keeps the screenshot's aspect ratio (no padding, no boxy frame) |
| `preview.position` | `bottom-right` | same value set as `text_card.position` |
| `preview.output` | (primary) | same semantics as `text_card.output` |
| `preview.dismiss_secs` | `5` | auto-dismiss after N seconds (0-600; 0 = stay until next capture) |

the preview is the scaled screenshot with a thin dark border. hovering over it overlays a translucent centered caption bar at the bottom (`Copied`, `Uploaded`, or the bare filename for saves), and re-arms the auto-dismiss timer (so the card stays as long as you keep mousing over it). clicking the preview:

- after `-u`: runs `xdg-open <url>` (opens the upload link in your browser)
- after `-o`: runs `xdg-open <dir>` (opens the containing folder in your file manager)
- after `-c`: just dismisses (the file may already be gone if it was a temp)

the card is click-through (no input region). running `--show` again kills any previous card via a pid file in `$XDG_RUNTIME_DIR/grabit-show.pid`, so only one card is ever on screen.


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

## window

```sh
grabit -w -o                  # save the active window
grabit --window -c            # copy the active window
grabit -w -e -u               # annotate the active window, then upload
```

`-w`/`--window` captures the focused window instead of dragging a region. it pairs with the same actions `-F` does, and it cannot be combined with `-f`, `-F`, or `--record`.

how the window is captured depends on what the compositor can tell grabit:

- **hyprland** reports the geometry of every window, so `-w` resolves the focused window to a rectangle and crops the screen to it. this behaves exactly like `-F` with a smaller region: `-e` works, and anything drawn on top of the window is included.
- **niri** only reports positions for *floating* windows. for a floating window `-w` takes the same crop path as hyprland. for a tiled window there is no geometry to crop to, so grabit asks niri to render the window itself (`screenshot-window`), which needs **niri 25.11 or newer** and has three consequences: the shot contains only the window (nothing overlapping it); `-e` is unavailable, so grabit logs a warning and captures without the editor; and for `png` output the file niri wrote is used as-is, so `png.level` does not apply (it still does for `jpeg`/`webp`, which are re-encoded). niri also copies its window screenshots to the clipboard unconditionally, so a `-w -o` run on a tiled window replaces the clipboard contents as a side effect.
- **other compositors** have no way to report the active window; `-w` fails with a notification.

`%w`/`%t` in `--filename` are independent of this and work anywhere `wlr-foreign-toplevel-management-v1` is available.

## edit

```sh
grabit -e -c                  # annotate, then copy
grabit -e -u                  # annotate, then upload
grabit -e -o                  # annotate, then save
```

`-e`/`--edit` pairs with any action. an annotation toolbar sits at the top of the primary monitor (or the output named in `edit.toolbar_output`) from the moment the overlay opens, or stays hidden until you select a region with `edit.toolbar_placement = attach`; drag it by its background to park it anywhere, including onto another monitor (same as the recording control bar and pinned screenshots). the parked position is remembered across invocations (via `edit.toolbar_pos`) as long as that monitor is still connected; if it's gone, the toolbar falls back to the default placement. `grabit unset edit.toolbar_pos` forgets the parked spot. the overlay starts in region-select mode, but picking any tool (click or `1`-`9`) switches to drawing immediately: you can annotate anywhere on the frozen screen before a region exists, then click the **select region** button (or press `q`) to drag out the capture area (save stays disabled until one is set). tools:

- **select region** (`q`) - drag out or replace the capture area
- **move/resize** (`s`) - click an annotation to select it, drag to move it, drag the corner handles of shapes/lines/arrows to resize them (strokes and text are move-only)
- **pen, marker, line, rect, rounded rect, ellipse, arrow, freehand arrow, blur, pixelate, text, counter, callout, eraser** - keyboard shortcuts `1`-`9`, or by letter: `p` pen, `m` marker, `l` line, `r` rect, `o` ellipse, `a` arrow, `b` blur, `x` pixelate, `t` text, `c` counter, `k` callout, `e` eraser. the freehand arrow draws like the pen but always ends in an arrow head, so it can curve around whatever it points at; press `a` again to reach it
- **spotlight** (`h`) dims everything outside the rect you drag, to draw the eye to one area. the width slider sets how dark the surround goes. each spotlight dims everything outside *itself*, so a second one will also dim the first one's bright area
- **callout** (`k`) draws a speech bubble: click the thing it should point at, type the text, press Enter. the bubble appears offset from that point with a tail leading back to it; in the move/resize tool (`s`) both the bubble and the tail tip are draggable handles, so you can re-aim it
- **6 preset color swatches** + a current-color square (click to open the picker)
- **hsl picker panel**: drag in the gradient, type a hex value (`#rrggbb` or `#rgb`), or click the eyedropper to sample a pixel from the screen
- **width slider** (1-12 in the toolbar, or scroll the mouse wheel anywhere; the persisted `edit.width` accepts up to 20 if you set it via the cli). with the text tool active, the wheel sizes the text (8-72) instead
- **undo** (`u` or `ctrl+z`, hold to repeat) - steps back through annotations, annotation moves/resizes, and region changes (move, resize, re-select) alike / **save** (`enter`) / **cancel** (`esc` or right-click)
- **resize handles** on the locked region; **ctrl+drag** inside to move the whole region
- **shift** while drawing constrains rect/ellipse/blur to squares and arrows/lines to 45° angles
- **arrow keys / shift+arrows** still move and resize the capture region while the editor is open
- **text tool**: `enter` commits the annotation, clicking anywhere else commits it too, `esc` discards the typed text without leaving the editor
- **hex field**: the typed value applies on `enter` or when you click elsewhere in the picker; `esc` abandons it
- **right-click** cancels the selector in every mode, aborting an in-progress drag or text entry first

the editor opens in region-select mode; drag out a region, then pick a tool to annotate (or press a tool key). two toggles change this:

- `edit.instant_capture true` - selecting the region captures immediately instead of leaving it adjustable. to annotate first, pick a tool and draw, then switch back with the **select region** button or `q` and drag out the region.
- `edit.start_with_tool true` - the editor opens already in your last-used `edit.tool` (drawing mode) instead of region-select. press `q` (or click the select-region button) when you're ready to define the capture area.

last-picked color, width, and tool persist via:

| key | default | notes |
|---|---|---|
| `edit.color` | `#ff3030` | `#rrggbb`, `#rgb`, or one of red/yellow/green/blue/black/white |
| `edit.width` | `4` | integer 1-20 |
| `edit.tool` | `pen` | one of: `pen`, `marker`, `line`, `rect`, `rounded_rect`, `ellipse`, `arrow`, `arrow_pen`, `blur`, `pixelate`, `spotlight`, `text`, `counter`, `callout`, `eraser` - the editor reopens with your last-used tool |
| `edit.default` | `false` | when `true`, every capture opens the editor (same as passing `-e` to every run; applies to copy/upload/save/pin, ignored for `-f`/record/OCR) |
| `edit.instant_capture` | `false` | when `true`, picking the region in the editor captures straight away instead of leaving it adjustable (also applies to window-snap click and `ctrl+a`). `region.confirm` takes precedence if both are set |
| `edit.start_with_tool` | `false` | when `true`, the editor opens in your last-used `edit.tool` instead of region-select mode. press `q` for region-select when ready |
| `edit.smooth` | `false` | smooth pen/marker/eraser strokes into a curve instead of tracing every sampled pixel |
| `edit.toolbar_placement` | `top` | where the toolbar opens. `top` centers it at the top of the monitor; `attach` hides it until you select a region, then puts it below the selection (above when there is no room), which saves crossing screens on a multi-monitor setup. it hides again while you move or resize the region and settles at the new spot on release. keys still work while it is hidden, and dragging the toolbar parks it for the rest of that run without remembering the spot (`edit.toolbar_pos` is ignored) |
| `edit.toolbar_output` | (empty) | pin the toolbar to one output, e.g. `DP-1`; it opens there and dragging cannot leave it. empty opens on the primary monitor and lets you drag the toolbar across any monitor |
| `edit.toolbar_pos` | (empty) | last parked toolbar spot as `<output>:<x>,<y>`, written automatically when you drag the toolbar; ignored if that output is gone |

## filename templates

`grabit --filename '<tpl>'` (per-run) overrides `filename` config (per-user). tokens:

| token | expands to |
|---|---|
| `%Y %m %d %H %M %S` | date/time fields |
| `%s` | unix timestamp |
| `%r` | 12-char random alnum (`%r8` etc. picks length) |
| `%u` | uuid v4 |
| `%w` | active window class / app id |
| `%t` | active window title |
| `%%` | literal `%` |

presets via `filename_preset`:
- `date`: `%Y-%m-%d-%H-%M-%S` (default)
- `random`: `%r12`
- `uuid`: `%u`
- `timestamp`: `%s`

`%w`/`%t` read the focused toplevel over `wlr-foreign-toplevel-management-v1`, so they work on hyprland, sway, niri, and river. they resolve to empty where that protocol is missing.

## plugins

```sh
grabit plugin install <git-url>   # clone, build, install (alias: add)
grabit plugin list                # installed plugins (alias: ls)
grabit plugin show <name>         # parsed manifest
grabit plugin update [<name>]     # update one, or all
grabit plugin remove <name>       # uninstall (alias: rm)
```

run one by name - a non-flag first argument resolving to an installed plugin execs `grabit-<name>`:

```sh
grabit <name> [args]              # run the plugin
grabit -p <name> [args]           # run it and pin its last stdout line as a file
```

plugins whose manifest sets `capture.auto` get a fresh screenshot path as their first argument; `--capture` forces that per call and `--no-capture` suppresses it. see `PLUGINS.md` for the manifest format.

## environment

| var | effect |
|---|---|
| `GRABIT_DEBUG=1` | enable debug logging (same as `-d`) |
| `GRABIT_<SERVICE>_AUTH` | per-service auth token (overrides config) |
| `GRABIT_LOG_FILE` | `0` disables the log file (stderr only), anything else forces it on; takes precedence over the `log_file` config key |
| `GRABIT_CAPTURE_BACKEND` | force the capture backend (`auto`/`wlr`/`ext`/`kwin`); takes precedence over the `capture.backend` config key |
| `GRABIT_CLIPBOARD_BACKEND` | force the clipboard protocol (`auto`/`ext`/`wlr`); `auto` prefers `ext-data-control-v1` and falls back to the deprecated `wlr-data-control` |
| `WAYLAND_DISPLAY` | wayland socket to connect to; named in the connection-failure message |
| `HOME` | required when `XDG_CONFIG_HOME` is unset (grabit exits with "HOME is not set"); also backs the `~/Pictures`, `~/Videos`, `~/.cache` fallbacks |
| `HYPRLAND_INSTANCE_SIGNATURE` | set by hyprland; with `XDG_RUNTIME_DIR` locates the hyprland ipc socket behind window snapping and `-w`/`--window` |
| `NIRI_SOCKET` | set by niri; locates the niri ipc socket behind `-w`/`--window` and window snapping |
| `XCURSOR_THEME` / `XCURSOR_SIZE` | cursor theme and size for the selector/overlays (size clamped to 8..256, default 24) |
| `XDG_RUNTIME_DIR` | base for grabit's runtime files if set, else `/tmp` (see files below) |
| `XDG_VIDEOS_DIR` | recording save dir (`save_dir` config takes precedence; else this, else `~/Videos`) |
| `XDG_PICTURES_DIR` | screenshot save dir (`save_dir` config takes precedence; else this, else `~/Pictures`) |
| `TESSDATA_PREFIX` | tesseract language-data dir |
| `GRABIT_TRANSLATE_KEY` | api key for the `deepl` and `libretranslate` backends; overrides `translate.api_key` |
| `NO_COLOR` | if set to any value, disable ansi color in log output |
| `GRABIT_RECORD_ENC_DELAY_US` | debug knob: throttle the recording encoder drain loop, in microseconds per frame |

set by grabit when dispatching a plugin (read by the plugin, not by you):

| var | effect |
|---|---|
| `GRABIT_BIN` | absolute path to the grabit binary |
| `GRABIT_PLUGIN_NAME` | the plugin's name |
| `GRABIT_PLUGIN_DIR` | the plugin's install directory |
| `GRABIT_CACHE_DIR` | per-plugin cache directory |

## files

| path | purpose |
|---|---|
| `~/.config/grabit/config.toml` | user config (mode 0600) |
| `~/.config/grabit/uploaders/<name>.sxcu` | registered sharex uploaders |
| `~/.config/grabit/plugins/<name>/` | installed plugins (binaries symlinked from `plugins/.bin/`) |
| `~/.config/grabit/plugins/.lock` | plugin install/update lock |
| `~/.config/grabit/plugins/<name>/.source`, `.last_check`, `.update.log` | per-plugin bookkeeping |
| `~/.cache/grabit/plugins/<name>/` | per-plugin cache |
| `$XDG_RUNTIME_DIR/grabit.log` | every message grabit prints, including info and debug (else `/tmp/grabit-<uid>.log`). notifications that say "check the log file" mean this one. truncated once it passes 1 MiB; turn it off with `grabit set log_file false` or `GRABIT_LOG_FILE=0` |
| `$XDG_RUNTIME_DIR/grabit/` | temp captures for the clipboard/upload flows (else `/tmp`) |
| `$XDG_RUNTIME_DIR/grabit_recording.pid` | active recording pid file (else `/tmp/grabit_recording.pid`) |
| `$XDG_RUNTIME_DIR/grabit-show.pid` | on-screen text/preview card pid file |

