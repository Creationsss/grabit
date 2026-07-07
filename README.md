# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river, kde plasma 6. not supported: x11, gnome.

## install

### Void

see https://void.creations.works/

### Arch (AUR)

see https://aur.archlinux.org/packages/grabit

### Gentoo

```sh
eselect repository add roxy-overlay git https://codeberg.org/key/roxy-overlay.git
eselect repository enable roxy-overlay
emaint sync -r roxy-overlay
emerge --ask media-gfx/grabit  # masked by ~amd64
```

### NixOS

```sh
nix run git+https://heliopolis.live/creations/grabit.git -- --help
```

### from source

```sh
make
sudo make install
```

deps: `json-c libcurl libmagic wayland-client wayland-cursor cairo libxkbcommon libdbus-1`. optional: `libjpeg` `libwebp` (jpeg/webp output). runtime: `ffmpeg` (for `--record`), `tesseract` (for `--tesseract`).

## demo

<p>
  <img src="https://atums.world/u/08a4b8bb-e855-4905-a2c5-39f35497ff33.png" width="220" alt="region selector">
  <img src="https://atums.world/u/502cfb19-7d8c-4115-bc38-8be272e31b23.png" width="220" alt="annotator">
  <img src="https://atums.world/u/eb03253f-9e88-4703-aed2-afa7eca2377a.png" width="220" alt="color picker">
</p>

- **region selector** with live freeze (drag, or click a window on hyprland to snap); pointer included by default (`capture.cursor`)
- **confirm mode** (`region.confirm`) - flameshot-style: adjust the selection with handles, arrow keys (hold to accelerate), or dragging, then enter / ctrl+c / double-click to capture
- **`--fullscreen`** / `-F` grabs a whole monitor - one monitor grabs directly, multiple opens a monitor picker (`--fullscreen=<n|name>` skips it, `--fullscreen=all` stitches every monitor); works with `--record` too
- **annotator** opened with `-e` - pen, marker, line, rect, ellipse, arrow, blur, text, eraser; scroll wheel sizes strokes and text
- **color picker** with hex input + eyedropper that samples from the freeze
- **`--record`** toggles region recording with a live overlay + tray icon; mp4, webm, or gif via `recording.format` ([mp4](https://atums.world/u/7598183f-c502-4c4e-9c51-6f167473a8fb.mp4))
- **`--pin`** pins captures to the desktop - click-through, stackable, draggable across monitors when grabbed
- **uploads** to zipline, nest, fakecrime, ez, guns, pixelvault, or any sharex `.sxcu` uploader; zipline supports chunked uploads (`--chunked`) with automatic fallback when cloudflare rejects large files
- **`--tesseract --translate[=<lang>]`** OCRs the region and pipes through translate-shell before copying

## docs

- [OPTIONS.md](OPTIONS.md) - usage, configuration, auth tokens, sharex uploaders, recording/pin/ocr/edit, filename templates, env vars, build targets
- [PLUGINS.md](PLUGINS.md) - plugin cli, manifest format, helper header

## source

- primary: https://heliopolis.live/creations/grabit
- github mirror: https://github.com/Creationsss/grabit

## license

agpl-3.0-or-later. see `LICENSE`.
