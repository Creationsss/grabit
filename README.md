# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river. kde plasma screenshots via kwin's ScreenShot2 and records via kwin's screencast protocol; gnome is recording-only (mutter's screencast d-bus api). both go through pipewire with no portal dialog. not supported: x11.

## install

### Void

see https://void.creations.works/

### Arch (AUR)

[`grabit`](https://aur.archlinux.org/packages/grabit) builds from the latest tag, [`grabit-bin`](https://aur.archlinux.org/packages/grabit-bin) is the prebuilt release, and [`grabit-git`](https://aur.archlinux.org/packages/grabit-git) tracks main.

### Gentoo

`media-gfx/grabit` in the [roxy-overlay](https://codeberg.org/key/roxy-overlay).

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

`json-c` `libcurl` `libmagic` `wayland-client` `wayland-cursor` `cairo` `libpng` `libxkbcommon` `libdbus-1`

optional: `libjpeg` `libwebp` for jpeg/webp output, `libpipewire-0.3` for recording on kde/gnome.
runtime: `ffmpeg` for `--record`, `tesseract` for `--tesseract`. see [OPTIONS.md](OPTIONS.md) for the rest (`trans`, `git`, `xdg-open`, a sound player).

## demo

<p>
  <img src="https://atums.world/u/08a4b8bb-e855-4905-a2c5-39f35497ff33.png" width="220" alt="region selector">
  <img src="https://atums.world/u/502cfb19-7d8c-4115-bc38-8be272e31b23.png" width="220" alt="annotator">
  <img src="https://atums.world/u/eb03253f-9e88-4703-aed2-afa7eca2377a.png" width="220" alt="color picker">
</p>

- region selector with live freeze; drag, or click a window to snap
- confirm mode: adjust the selection before capturing
- **`-F`/`--fullscreen`** grabs one monitor, or every monitor stitched together
- **`-w`/`--window`** grabs the active window (hyprland, and niri via its own window screenshot)
- **`-L`/`--last`** reuses the last region instead of selecting one, screenshots and `--record` alike
- **`--delay <secs>`** waits before capturing, so menus and tooltips stay open
- annotator (`-e`): pen, marker, line, rect, rounded rect, ellipse, arrow, freehand arrow, blur, pixelate, spotlight, text, counter, callout, eraser
- **`-e -f <file>`** annotates an existing png, jpeg, or webp instead of capturing
- color picker with hex input and an eyedropper that samples the freeze
- six color swatches you can repoint to any colors (`edit.swatches`)
- toolbar that follows the region you select (`edit.toolbar_placement`)
- works with a touchscreen as well as a mouse
- **`--record`** region recording with overlay and tray icon; mp4, webm, or gif ([demo](https://atums.world/u/7598183f-c502-4c4e-9c51-6f167473a8fb.mp4))
- **`--pin`** pins captures to the desktop: click-through, stackable, draggable
- **`--tray`** persistent tray icon with every action in its menu
- uploads to six built-in hosts or any sharex `.sxcu` uploader
- **`--tesseract --translate`** OCRs a region and copies the translation

## docs

- [OPTIONS.md](OPTIONS.md) - configuration keys, auth tokens, sharex uploaders, keybinds, filename templates
- [PLUGINS.md](PLUGINS.md) - plugin cli, manifest format, helper header

## source

- primary: https://heliopolis.live/creations/grabit
- github mirror: https://github.com/Creationsss/grabit

## license

agpl-3.0-or-later. see `LICENSE`.
