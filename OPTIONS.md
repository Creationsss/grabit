# grabit options

every config key is here; the command-line flags are in `man grabit`, and `grabit set <key>` shows a key's values and default.

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
make             # release build into build/grabit
make sanitize    # asan + ubsan into build-san/grabit
make install     # binary, man page and grabit.desktop to $(DESTDIR)$(PREFIX)
make test        # spdx headers, doc sync, self-contained headers
make fmt         # clang-format -i (fmt-check: dry run)
make clangd-check  # clangd --check every source (FILE=src/x.c for one)
make apply-headers
make clean
```

`bear -- make all` writes `compile_commands.json` for your editor; re-run it after adding or removing sources.

## configuration

```sh
grabit settings               # settings ui (also in the --tray menu)
grabit settings recording     # open one page; any other word is searched
grabit set                    # list all settable keys
grabit set <key>              # show example/default for that key
grabit set <key> <value>      # write (validated); <key>=<value> works too
grabit get [<key>]            # dump the config, or one key
grabit unset <key>            # also removes stale keys grabit no longer knows
grabit help [<topic>]         # settings/set/get/unset/sxcu/upload/history/plugin/filename/env/examples/ocr
```

every subcommand takes `--help` / `-h`.

### config, overrides and state

config lives at `$XDG_CONFIG_HOME/grabit/config.toml` (else `~/.config/grabit/config.toml`). grabit only writes it from `set`, `unset` and the settings window.

after it, grabit reads every `~/.config/grabit/config.d/*.toml` in name order, then each file in `GRABIT_CONFIG` (colon-separated, `~` allowed). a later file wins, so `GRABIT_CONFIG=~/demo.toml grabit ...` beats everything for one run. a file that does not parse is skipped with a warning. override files are never written: `grabit set` warns when an override hides the change, and the settings window names the file a row comes from.

what grabit decides on its own goes to `$XDG_STATE_HOME/grabit/state.toml` (default `~/.local/state/grabit/state.toml`): the last `edit.color`, `edit.width` and `edit.tool`, `edit.swatches`, `edit.toolbar_pos`, `region.last`, `gui.settings_size` and `gui.settings_page`. state wins over config, so those keys in config.toml only set the starting value (old copies are moved over once, with a note). `save_state = false` turns the state file off.

### auth tokens

per service, in the config (chmod 0600) or, preferably, from the environment:

```sh
grabit set services.zipline.auth "<token>"
export GRABIT_ZIPLINE_AUTH="$(pass show grabit/zipline)"
```

zipline also needs `grabit set services.zipline.domain https://your.host` (`/api/upload` is appended if missing). nest takes an optional `services.nest.folder` (uuid).

`grabit upload test [<service>]` uploads a small test image, prints the link and deletes it again where the service allows, so you can check a token.

### zipline chunked uploads

zipline v4 can take large uploads in chunks through `/api/upload/partial`, like its web client (useful behind cloudflare's 100MB limit):

```sh
grabit --record --zipline --chunked        # chunk this one upload
grabit set services.zipline.chunked true   # always chunk
grabit set services.zipline.chunk_size 50  # MiB per chunk (default 25, 1-95)
```

the link may 404 for a moment on very large files while the server assembles them. a plain upload rejected with HTTP 413 is retried in chunks, and a chunk that still gets a 413 is retried at half the size, down to 1 MiB.

### zipline custom headers

set per-upload metadata with `services.zipline.headers.<name>`:

| header | accepted values |
|---|---|
| `x-zipline-format` | `random`, `date`, `uuid`, `name`, `gfycat`; **defaults to `name`** so the link keeps grabit's filename |
| `x-zipline-image-compression-percent` | 0-100 |
| `x-zipline-image-compression-type` | `jpg`, `png`, `webp`, `jxl` |
| `x-zipline-password` | string |
| `x-zipline-max-views` | non-negative integer |
| `x-zipline-no-json` | `true` only |
| `x-zipline-original-name` | `true` only |
| `x-zipline-folder` | folder id |
| `x-zipline-filename` | string |
| `x-zipline-domain` | string |
| `x-zipline-file-extension` | string |
| `x-zipline-deletes-at` | duration (e.g. `1d`, `30m`) |

zipline turns `no-json` and `original-name` on whenever they are sent, so `false` is rejected; `grabit unset` them instead. unknown header names are sent as-is with a warning.

### sharex (.sxcu) uploaders

```sh
grabit sxcu add ~/Downloads/myhost.sxcu   # copy into the config dir (alias: install; --force replaces)
grabit sxcu list                          # alias: ls
grabit sxcu show <name>                   # parsed fields, auth masked (--show-secrets unmasks)
grabit sxcu remove <name> [--yes]         # alias: rm
grabit --<name>                           # screenshot + upload, like a built-in
```

uploaders live at `~/.config/grabit/uploaders/<name>.sxcu` (chmod 0600), and their auth stays in the file's `Headers`.

supported fields: `Name`, `RequestURL`, `RequestMethod` (or `RequestType`), `Body` (`MultipartFormData`/`FormURLEncoded`/`JSON`/`XML`/`Binary`/`None`), `FileFormName`, `Headers`, `Parameters`, `Arguments`, `Data`, `URL`, `DeletionURL`, `ThumbnailURL`, `ErrorMessage`, `RegexList`.

request placeholders: `{filename}`, `{base64:...}`, `{random:a|b|c}`, `{select:a|b|c}` (the first), `{input}` (empty), `{prompt:label|default}` / `{inputbox:label|default}` (the default; grabit never prompts). a backslash escapes `{`. response placeholders: `{response}`, `{responseurl}`, `{json:path.to[0].field}`, `{regex:pattern|group}` (POSIX ERE), `{regex:N|group}` (N indexes `RegexList`), `{header:Name}`.

### top-level keys

| key | type | notes |
|---|---|---|
| `default_action` | enum | `copy`/`upload`/`save`/`pin` (default `copy`) |
| `service` | string | upload target for `-u` and `default_action=upload`: a built-in or an sxcu name |
| `notifications` | bool | desktop notifications (default `true`); failures are still shown |
| `log.enabled` | bool | mirror log lines to `$XDG_RUNTIME_DIR/grabit.log` (default `true`; `GRABIT_LOG_FILE` overrides). old name: `log_file` |
| `also_save` | bool | also keep a file when copying or uploading (default `false`). old name: `save_captures` |
| `save_state` | bool | read and write `state.toml` (default `true`) |
| `save_dir` | string | where screenshots and recordings go; else `XDG_PICTURES_DIR`/`XDG_VIDEOS_DIR`, else `~/Pictures`/`~/Videos` |
| `filename` | string | filename template (see below) |
| `filename_preset` | enum | `date`/`random`/`uuid`/`timestamp` |
| `format` | enum | `png`/`jpeg` (alias `jpg`)/`webp` (default `png`); `--format` per run |

### capture

| key | default | notes |
|---|---|---|
| `capture.backend` | `auto` | `wlr`, then `ext`, then `kwin` (KDE's `org.kde.KWin.ScreenShot2`); force one by name |
| `capture.hdr` | `false` | keep an hdr output's full range as a 16-bit png with `cICP`, `mDCV` and `cLLI` instead of tone mapping to srgb. only for a plain png `-F`/`-w`/`-L` on one monitor; files are ~2.5x larger; needs `color-management-v1` |
| `capture.delay_secs` | `0` | wait before capturing (`--delay` per run, max 3600). screenshots wait before the freeze; `--record` waits after the region is picked. old name: `capture.delay` |
| `capture.cursor` | `true` | include the pointer in screenshots (`--cursor` forces it for one run; recordings use `recording.cursor`) |

### region selector

| key | default | notes |
|---|---|---|
| `region.window_snap` | `true` | hover-highlight windows and click to capture one. needs compositor ipc (niri only reports floating windows) |
| `region.window_radius` | `auto` | round window captures like the compositor: `auto` reads hyprland's rounding plus border, `0`..`100` forces it. jpeg stays square |
| `region.window_borders` | `false` | grow window captures by the border width (hyprland's `general:border_size`, or per window on aqueous). no-op on sway and niri |
| `region.snap_ignore` | `""` | comma-separated window classes or app_ids that snapping looks through, e.g. a game overlay. on hyprland it also matches layer namespaces and windows tagged `grabit-ignore` (`tag = "+grabit-ignore"`) |
| `region.snap_animation` | `false` | slide the snap highlight between windows instead of jumping |
| `region.show_coords` | `false` | show the cursor position while selecting |
| `region.magnifier_edits` | `true` | show your annotations inside the magnifier and its color readout |
| `region.confirm` | `false` | keep the selection adjustable after the drag (handles, arrows, Shift+arrows), then Enter, Ctrl+C or double-click captures |
| `region.repeat_last` | `false` | always reuse the last region, like `-L`; `--no-last` overrides one run |
| `region.last` | | the last region as `<x>,<y>,<w>,<h>` (state) |

`ctrl+a` selects the whole monitor under the cursor.

### gui

| key | default | notes |
|---|---|---|
| `gui.radius` | `0` | round grabit's own windows and cards: `0` square, `auto` follows hyprland, or a radius in logical pixels. never touches the capture |
| `gui.settings_page` | | the settings page last open, reopened by a bare `grabit settings` (state) |
| `gui.settings_size` | | the settings window size as `<width>x<height>` (state) |

### keybinds

every selector and editor action is rebindable under `keys.*`. a value is a comma-separated list of bindings: an xkb key name with optional `Ctrl+`/`Shift+`/`Alt+`/`Super+` (keypad keys match their main twin), or `mouse:<button>` with a name (`left`, `right`, `middle`, `back`, `forward`, `side`, `extra`) or an evdev code (`mouse:273` is right). most mice report the thumb buttons as `side` and `extra`. left click always draws and cannot be rebound.

| key | default | action |
|---|---|---|
| `keys.confirm` | `Enter, Ctrl+s` | capture the selection |
| `keys.cancel` | `Escape, Ctrl+w, Ctrl+q, mouse:right` | cancel, or abort a drag or text entry |
| `keys.select_all` | `Ctrl+a` | select the monitor under the cursor |
| `keys.undo` | `u, Ctrl+z` | undo |
| `keys.redo` | `Ctrl+y, Ctrl+Shift+z` | redo |
| `keys.delete` | `Delete, BackSpace` | delete the selected annotations |
| `keys.magnifier` | `Alt_L, Alt_R` | hold for a pixel loupe |
| `keys.pop_out` | `w` | move the overlay edit into a window |
| `keys.zoom_in` / `keys.zoom_out` | `Ctrl+plus, Ctrl+equal` / `Ctrl+minus` | editor window zoom |
| `keys.zoom_fit` / `keys.zoom_actual` | `Ctrl+0` / `Ctrl+1` | fit the image to the editor window, or show it at 100% |
| `keys.edit_mode` | `s` | move/resize tool |
| `keys.region_mode` | `q` | back to region select |
| `keys.nudge_left` / `keys.nudge_right` / `keys.nudge_up` / `keys.nudge_down` | `Left` etc. | move a locked selection one pixel |
| `keys.tool.<name>` | `p, 1` ... `e, 9` | pick a tool (names under edit below) |

```sh
grabit set keys.confirm "Enter, mouse:right, Ctrl+s"   # right click saves
grabit set keys.confirm --watch   # press the bindings live, Enter saves, Esc cancels
grabit set keys.confirm --reset   # back to default; `grabit set keys --reset` resets all
```

`--watch` records the exact code a device sends, so use it for a mouse button that a name does not catch.

### encoders

| key | default | notes |
|---|---|---|
| `png.level` | `1` | zlib level 0-9; higher only shrinks the file, at several times the encode time |
| `jpeg.quality` | `90` | 1-100 |
| `webp.quality` | `85` | 0-100 (ignored when lossless) |
| `webp.lossless` | `false` | webp lossless mode |

jpeg and webp exist only when built with `libjpeg` / `libwebp`; png is always there.

## recording

```sh
grabit --record               # start; run again to stop
grabit --pause                # pause or resume (also SIGUSR1, e.g. pkill -USR1 -x grabit)
grabit --record --no-upload   # skip the auto-upload
grabit --record --zipline     # upload this one to a given service
```

| compositor | source | notes |
|---|---|---|
| wlroots (hyprland, sway, niri, river) | `wlr-screencopy` / `ext-image-copy` | |
| KDE Plasma | `zkde_screencast_unstable_v1` | needs `make install`, see below |
| GNOME | `org.gnome.Mutter.ScreenCast` | no region selector; private api that may break on upgrade |

KDE and GNOME need a `libpipewire-0.3` build; neither shows a portal dialog. KWin only grants screenshots and recording to programs whose installed `.desktop` declares `X-KDE-DBUS-Restricted-Interfaces` and `X-KDE-Wayland-Interfaces`, so use `make install` (`KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` bypasses it for testing). GNOME has no `zwlr_layer_shell_v1`, so pick the area with `-F` or `-L` and stop with a second `grabit --record`.

while recording there is a red border, a control bar (start, pause, stop, abort and a timer) placed outside the recording, and a tray icon that stops it on click. pausing ends an encoder segment and stopping stitches them, so paused time is cut and the timer counts recorded time only. a "Recording finishing" notification covers the stitch; a faster `recording.preset` shortens it. like screenshots, the file is kept after an upload only with `also_save = true`.

| key | default | notes |
|---|---|---|
| `recording.fps` | 30 | 1-120 |
| `recording.format` | `mp4` | `mp4` (h.264), `webm` (vp9) or `gif`. `crf` is mp4/webm; `preset`, `tune`, `max_size_mb` are mp4 only |
| `recording.crf` | 23 | 0-51, lower is better |
| `recording.preset` | `fast` | `ultrafast` ... `veryslow` |
| `recording.tune` | (none) | `film`, `animation`, `grain`, `stillimage`, `psnr`, `ssim`, `fastdecode`, `zerolatency` |
| `recording.pix_fmt` | `yuv420p` | `yuv420p`, `yuv422p`, `yuv444p`, `yuv420p10le` |
| `recording.cursor` | `true` | record the cursor |
| `recording.tray` | `true` | tray icon while recording; `--no-tray` for one run |
| `recording.show_dimensions` | `true` | size badge on the region border |
| `recording.max_size_mb` | (none) | re-encode when the file is larger (0-100000) |
| `recording.ffmpeg` | `ffmpeg` | ffmpeg binary |

## tray

`grabit --tray` starts a persistent tray icon; run it again to stop it. left click captures a region. the menu has screenshot (region, window, monitor, all monitors, last region, a 3 or 5 second delay, and copy/upload/save/edit), record (with pause), OCR, pin, upload files, history (copy or open the last link, file or OCR text) and settings.

| key | default | notes |
|---|---|---|
| `tray.icon` | `hand` | an icon theme name, or a `.png` / `.svg` path (sent as pixels, so it works with any tray). a running tray picks up changes, and dropping an image on the settings window sets it |

the recording tray is separate and always uses `media-record`.

## sound

| key | default | notes |
|---|---|---|
| `sound.enabled` | `false` | play a shutter sound on capture |
| `sound.player` | (auto) | `pw-play`, `paplay`, `play` or `aplay` |
| `sound.file` | (auto) | the freedesktop camera-shutter sound |

## pin

```sh
grabit --pin                  # pin a region where it was grabbed
grabit --grab                 # make pins draggable, with a close button
grabit --release              # back to click-through
grabit --close-all
```

pins stack, are click-through, and sit exactly where the region was, even over bars. a hold-bind makes them easy to move, e.g. on hyprland:

```
bindrn = SUPER SHIFT, mouse:272, exec, grabit --grab
bindrn = SUPER SHIFT, mouse:272, release, exec, grabit --release
```

## ocr

`grabit --tesseract` copies the text of a region. it needs `tesseract` (`tesseract-ocr` is tried first) and the traineddata for your language (`tesseract --list-langs`).

| key | default | notes |
|---|---|---|
| `ocr.tesseract` | `tesseract-ocr`, then `tesseract` | tesseract binary |
| `ocr.lang` | `eng` | tesseract `-l`; combine with `+`, e.g. `eng+deu` |

### translate

```sh
grabit --tesseract --translate       # to translate.target
grabit --tesseract --translate=ja    # per-run target
```

the translation is copied instead of the raw text; if it fails, the raw text is copied and a notification says so.

> **privacy:** translating sends your screen text to the backend's server: google for `trans`, wherever `translate.url` points for `libretranslate`. the deepl free tier may keep what you send.

| key | default | notes |
|---|---|---|
| `translate.target` | `en` | target code; the source is detected |
| `translate.backend` | `trans` | `trans` ([translate-shell](https://github.com/soimort/translate-shell)), `libretranslate` ([LibreTranslate](https://github.com/LibreTranslate/LibreTranslate), run your own to stay local) or `deepl` ([DeepL API](https://developers.deepl.com)) |
| `translate.url` | (unset) | server url: required for `libretranslate`, optional for `deepl` |
| `translate.api_key` | (unset) | required for `deepl`; `GRABIT_TRANSLATE_KEY` overrides |

deepl keys ending in `:fx` go to `api-free.deepl.com`, others to `api.deepl.com`. deepl targets are passed as given (`EN-US`, `PT-BR`, `ZH-HANS`), and quota (456), rate limit (429) and key (403) errors are reported as such.

### on-screen cards

`--tesseract --show` also shows the text on a card (`--no-copy` leaves the clipboard alone; the text is on stdout either way). `preview.enabled` shows a thumbnail after `-c`, `-u` or `-o`: hovering keeps it up, and a click opens the link (`-u`) or folder (`-o`). only one card is on screen at a time.

| key | default | notes |
|---|---|---|
| `text_card.dismiss_secs` | `8` | 0-600; 0 stays until replaced |
| `text_card.position` | `top-right` | `top-left`/`top-center`/`top-right`/`bottom-left`/`bottom-center`/`bottom-right`/`center` |
| `text_card.output` | (primary) | output name, e.g. `DP-1`; falls back to the primary |
| `preview.enabled` | `false` | show the preview card |
| `preview.size` | `300` | width in pixels (100-800) |
| `preview.position` | `bottom-right` | as `text_card.position` |
| `preview.output` | (primary) | as `text_card.output` |
| `preview.dismiss_secs` | `5` | 0-600; 0 stays until the next capture |

## fullscreen

```sh
grabit -F -o                  # pick a monitor (or the only one) and save it
grabit --fullscreen=2 -u      # monitor 2, no picker
grabit --fullscreen=DP-1 -e -o
grabit --fullscreen=all -c    # every monitor stitched together (gaps are black)
grabit --record -F
```

`-F` works with every capture action and `-e`, but not `-f`. with several monitors and no target, the selector snaps to whole monitors; dragging still works. an unknown target lists the monitors.

## window

```sh
grabit -w -c                  # copy the active window
grabit -w -e -u               # annotate it, then upload
```

`-w` works with the same actions as `-F`, but not with `-f`, `-F` or `--record`.

- **hyprland, sway, umbriel, aqueous**: the window is cropped from the screen, so `-e` works and anything on top is included. on sway that is the window and border without the title bar, like grimshot.
- **niri**: floating windows are cropped the same way. tiled ones are rendered by niri's `screenshot-window` (niri 25.11+): only the window, no `-e`, `png.level` is not applied, and niri also puts it on the clipboard.
- **elsewhere**: `-w` fails with a notification.

## edit

`-e` opens the editor before `-c`, `-u`, `-o`, `--pin` or `--tesseract` (`edit.always` makes it the default). it starts in region select; pick a tool to draw anywhere first, then `q` to drag out the region. the toolbar can be dragged anywhere, including to another monitor, and remembers the spot (`grabit unset edit.toolbar_pos` forgets it).

tools, by key: `q` region, `s` move/resize, `p` pen, `m` marker, `l` line, `r` rect, `o` ellipse, `a` arrow, `b` blur, `x` pixelate, `h` spotlight, `t` text, `c` counter, `k` callout, `e` eraser, or `0`-`9`. pressing `p`, `r`, `a` or `b` again cycles its group, which is the way to the rounded rect and freehand arrow.

- **move/resize**: drag annotations and their handles; ctrl+click selects more (`edit.multi_select`), `Delete` removes, the wheel changes width or font size, and the color and width controls apply to the selection
- **other tools**: spotlight dims everything outside the rect, callout draws a speech bubble pointing at where you click, counter numbers badges in order, and the eraser rubs out pixels rather than whole annotations
- **color**: six swatches (click a selected one to repoint it), an hsl picker with hex input and an eyedropper
- **width**: the slider, or the wheel anywhere
- **shapes and region**: Shift keeps shapes square and lines at 45°, Ctrl+drag moves the region, and the arrows and Shift+arrows move and resize it
- **text**: Enter or a click elsewhere commits, Esc discards, Ctrl+V pastes
- **undo**: undo and redo cover annotations, moves and region changes. hold Alt for a magnifier; touch works as a pointer

### editor window

`--edit-window` (or `edit.mode = window`) picks the region on the overlay as usual, then opens the editor in a normal window instead of keeping the screen frozen. the window can be moved, resized, tiled and left in the background while you use other apps, and it stays open until you save or cancel. `--edit-overlay` forces the overlay for one run. the pop-out button in the overlay toolbar (`w`) moves an edit that is already in progress, annotations and all, into a window.

the image starts fitted to the window. Ctrl+wheel zooms at the pointer, a middle-button drag pans, and `Ctrl+plus`/`Ctrl+minus`/`Ctrl+0`/`Ctrl+1` zoom in, zoom out, fit and show actual size; the pop-out button turns into fit-to-window. the selection handles crop the image. with annotations on the canvas, cancel asks to be pressed a second time before it throws them away. the same window opens for `-e -f <file>`.

| key | default | notes |
|---|---|---|
| `edit.color` | `#ff3030` | `#rrggbb`, `#rgb`, or red/yellow/green/blue/black/white |
| `edit.width` | `4` | 1-20 |
| `edit.tool` | `pen` | `pen`, `marker`, `line`, `rect`, `rounded_rect`, `ellipse`, `arrow`, `arrow_pen`, `blur`, `pixelate`, `spotlight`, `text`, `counter`, `callout`, `eraser` |
| `edit.always` | `false` | open the editor for every copy, upload, save and pin. old name: `edit.default` |
| `edit.mode` | `overlay` | `overlay` edits on the frozen screen, `window` in a normal window (see editor window above); `--edit-window`/`--edit-overlay` per run |
| `edit.instant_capture` | `false` | capture as soon as the region is picked (also for snap clicks and `ctrl+a`); `region.confirm` wins |
| `edit.start_with_tool` | `false` | open in the last `edit.tool` instead of region select |
| `edit.smooth` | `false` | smooth pen, marker and eraser strokes |
| `edit.eyedropper_edits` | `true` | the eyedropper sees your annotations |
| `edit.line_style` | `solid` | `solid`, `dashed` or `dotted` |
| `edit.multi_select` | `ctrl` | `ctrl`, `shift`, `alt` or `super` |
| `edit.swatches` | (built-in) | six colors, comma-separated, or `grabit set edit.swatches <1-6> <color|default>` for one |
| `edit.toolbar_placement` | `top` | `top` of the monitor, or `attach` below the selection once there is one |
| `edit.toolbar_output` | (empty) | keep the toolbar on one output, e.g. `DP-1` |
| `edit.toolbar_pos` | (empty) | the parked toolbar spot as `<output>:<x>,<y>` (state) |

## filename templates

`--filename '<tpl>'` overrides `filename` for one run.

| token | expands to |
|---|---|
| `%Y %m %d %H %M %S` | date/time fields |
| `%s` | unix timestamp |
| `%L` | milliseconds (`000`-`999`) |
| `%r` | 12 random alnum chars (`%r8` for 8) |
| `%u` | uuid v4 |
| `%w` | window class / app id |
| `%t` | window title |
| `%%` | literal `%` |

`filename_preset`: `date` (`%Y-%m-%d-%H-%M-%S`, the default), `random` (`%r12`), `uuid` (`%u`), `timestamp` (`%s`).

`%w`/`%t` name the snapped window, or else the focused one (compositor ipc or `wlr-foreign-toplevel-management-unstable-v1`, so river too), and are empty when neither is known. all tokens use the moment the capture started.

## history

grabit keeps every upload (with how to delete it), saved screenshot and recording, and OCR text in `$XDG_STATE_HOME/grabit/history.jsonl` (mode 0600, no size limit).

in `grabit settings`:

- **History** is a thumbnail grid by day, with search, type, service, date and size filters. click for a viewer (copy, open, show in folder, upload to, delete), right-click for the same as a menu. forgets and clears can be undone. recordings and GIFs get a thumbnail and their length, and play in the viewer on a loop (click or Space pauses); this uses `recording.ffmpeg`, and without ffmpeg they keep a plain tile.
- **OCR history** lists past OCR text.
- **Upload** works like zipline's upload page: drop, paste or type files, review them, and upload with progress. its Options (service, zipline headers, chunking, history and notifications) apply to that upload only and never touch config.toml.

```sh
grabit history                    # newest first (alias: list, ls); --ocr for OCR text, --json for scripts
grabit history copy <n> [--ocr]   # the link, path or text again
grabit history open <n>           # the file, or the link when the file is gone
grabit history delete <n> [-y]    # from the host and disk, then forget (--host-only / --file-only)
grabit history remove <n> [--ocr] # just forget it (alias: rm)
grabit history clear [--ocr|--all] [-y]
```

`<n>` is the list number (1 is newest) or an id from `--json`. deleting from the host works on zipline (the token is read from your config, never stored) and on .sxcu uploaders with a `DeletionURL`. thumbnails of uploads without a local file are fetched once into `$XDG_CACHE_HOME/grabit/history`.

| key | default | notes |
|---|---|---|
| `history.enabled` | `true` | record uploads, saved screenshots and recordings |
| `history.ocr` | `true` | also record OCR text |
| `history.confirm_delete` | `true` | ask before deleting, in settings and `grabit history delete` |

## plugins

```sh
grabit plugin install <git-url>   # clone, build, install (alias: add)
grabit plugin list                # alias: ls
grabit plugin show <name>
grabit plugin update [<name>]
grabit plugin remove <name> [--yes]  # alias: rm
grabit plugin disable|enable <name>
grabit <name> [args]              # run it (execs grabit-<name>)
grabit <name> --pin [args]        # run it and pin its last stdout line
```

a plugin with `capture.auto` gets a fresh screenshot as its first argument; `--capture` / `--no-capture` force it either way. see `PLUGINS.md`.

| key | default | notes |
|---|---|---|
| `plugins.confirm_remove` | `true` | ask before removing a plugin in `grabit settings` |

## environment

| var | effect |
|---|---|
| `GRABIT_DEBUG=1` | debug logging, like `-d` |
| `GRABIT_CONFIG` | extra config files applied last, colon-separated |
| `GRABIT_<SERVICE>_AUTH` | per-service auth token, over the config |
| `GRABIT_LOG_FILE` | `0` turns the log file off, anything else on; over `log.enabled` |
| `GRABIT_CAPTURE_BACKEND` | `auto`/`wlr`/`ext`/`kwin`, over `capture.backend` |
| `GRABIT_CLIPBOARD_BACKEND` | `auto`/`ext`/`wlr`; `auto` prefers `ext-data-control-v1` |
| `GRABIT_TRANSLATE_KEY` | translate api key, over `translate.api_key` |
| `GRABIT_RECORD_ENC_DELAY_US` | debug: slow the recording encoder by N µs per frame |
| `HYPRLAND_INSTANCE_SIGNATURE`, `SWAYSOCK`, `NIRI_SOCKET`, `UMBRIEL_SOCKET`, `AQUEOUS_SOCKET` | compositor ipc for `-w` and snapping (umbriel falls back to `$XDG_RUNTIME_DIR/umbriel-$WAYLAND_DISPLAY.sock`) |
| `WAYLAND_DISPLAY` | the wayland socket |
| `HOME` | needed when the XDG dirs are unset |
| `XDG_RUNTIME_DIR` | runtime files, else `/tmp` |
| `XDG_STATE_HOME` | `state.toml` and history, default `~/.local/state` |
| `XDG_CACHE_HOME` | history thumbnails and plugin caches, default `~/.cache` |
| `XDG_PICTURES_DIR` / `XDG_VIDEOS_DIR` | save dirs when `save_dir` is unset |
| `XCURSOR_THEME` / `XCURSOR_SIZE` | cursor for the selector and overlays (8-256, default 24) |
| `TESSDATA_PREFIX` | tesseract language data |
| `NO_COLOR` | no colour in log output |

plugins get `GRABIT_BIN`, `GRABIT_PLUGIN_NAME`, `GRABIT_PLUGIN_DIR` and `GRABIT_CACHE_DIR`.

## files

| path | purpose |
|---|---|
| `~/.config/grabit/config.toml` | config (0600) |
| `~/.config/grabit/config.d/*.toml` | overrides, in name order |
| `~/.config/grabit/uploaders/<name>.sxcu` | sharex uploaders |
| `~/.config/grabit/plugins/` | plugins (`.bin/` symlinks, `.lock`, per-plugin `.source`, `.last_check`, `.update.log`) |
| `~/.local/state/grabit/state.toml` | state; see `save_state` |
| `~/.local/state/grabit/history.jsonl` | history; see `history.enabled` |
| `~/.cache/grabit/history/` | thumbnails fetched from upload links, and video and GIF thumbnails with their length |
| `~/.cache/grabit/plugins/<name>/` | per-plugin cache |
| `$XDG_RUNTIME_DIR/grabit.log` | the log that "check the log file" means (else `/tmp/grabit-<uid>/grabit.log`); truncated past 1 MiB |
| `$XDG_RUNTIME_DIR/grabit/` | temp captures (grabit refuses a world-writable dir) |
| `$XDG_RUNTIME_DIR/grabit_recording.pid`, `grabit-show.pid`, `grabit-tray.pid` | recording, card and tray pid files |
