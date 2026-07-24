# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river. kde plasma 6 is screenshots-only. not supported: x11, gnome.

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

build deps:

`json-c` `libcurl` `libmagic` `wayland-client` `wayland-cursor` `cairo` `libxkbcommon` `libdbus-1`

optional: `libjpeg` `libwebp` for jpeg/webp output.
runtime: `ffmpeg` for `--record`, `tesseract` for `--tesseract`.

## demo

<p>
  <img src="https://atums.world/u/08a4b8bb-e855-4905-a2c5-39f35497ff33.png" width="220" alt="region selector">
  <img src="https://atums.world/u/502cfb19-7d8c-4115-bc38-8be272e31b23.png" width="220" alt="annotator">
  <img src="https://atums.world/u/eb03253f-9e88-4703-aed2-afa7eca2377a.png" width="220" alt="color picker">
</p>

- **region selector** with live freeze; drag, or click a window to snap on hyprland
- **confirm mode** flameshot-style: adjust the selection before capturing
- **`-F`/`--fullscreen`** grabs one monitor, or every monitor stitched together
- **annotator** (`-e`): pen, marker, line, rect, ellipse, arrow, blur, text, eraser
- **color picker** with hex input and an eyedropper that samples the freeze
- **`--record`** region recording with overlay and tray icon; mp4, webm, or gif ([demo](https://atums.world/u/7598183f-c502-4c4e-9c51-6f167473a8fb.mp4))
- **`--pin`** pins captures to the desktop: click-through, stackable, draggable
- **uploads** to six built-in hosts or any sharex `.sxcu` uploader
- **`--tesseract --translate`** OCRs a region and copies the translation

## docs

- [OPTIONS.md](OPTIONS.md) - configuration keys, auth tokens, sharex uploaders, keybinds, filename templates
- [PLUGINS.md](PLUGINS.md) - plugin cli, manifest format, helper header

## source

- primary: https://heliopolis.live/creations/grabit
- github mirror: https://github.com/Creationsss/grabit

## license

agpl-3.0-or-later. see `LICENSE`.
