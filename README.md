# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on hyprland, sway, niri, umbriel, aqueous and river (window snapping and `-w` need the ipc of the first five). kde plasma screenshots through kwin's ScreenShot2 and records through kwin's screencast protocol; gnome is recording only (mutter's screencast d-bus api). both use pipewire with no portal dialog. no x11.

## install

- **Void**: see https://void.creations.works/
- **Arch (AUR)**: [`grabit`](https://aur.archlinux.org/packages/grabit) (latest tag), [`grabit-bin`](https://aur.archlinux.org/packages/grabit-bin) (prebuilt) or [`grabit-git`](https://aur.archlinux.org/packages/grabit-git) (main)
- **Gentoo**: `media-gfx/grabit` in the [roxy-overlay](https://codeberg.org/key/roxy-overlay)
- **Devario**: `grabit` in [devario-core](https://repo.seafoam-labs.org/package/devario-core/x86_64/grabit)
- **NixOS**: `nix run git+https://heliopolis.live/creations/grabit.git -- --help`

### prebuilt binary

each release ships four x86_64 binaries:

| file | adds | also needs |
|---|---|---|
| `grabit-x86_64-linux` | | glibc 2.36 |
| `grabit-full-x86_64-linux` | jpeg/webp output, kde/gnome recording | glibc 2.36, `libjpeg`, `libwebp`, `libpipewire-0.3` |
| `grabit-hdr-x86_64-linux` | hdr capture | glibc 2.41, `libpng` 1.6.45 |
| `grabit-hdr-full-x86_64-linux` | both of the above | glibc 2.41, `libpng` 1.6.45, `libjpeg`, `libwebp`, `libpipewire-0.3` |

all of them need the base libraries below.

### from source

```sh
make
sudo make install
```

build deps: `json-c` `libcurl` `libmagic` `wayland-client` `wayland-cursor` `cairo` `libpng` `libxkbcommon` `libdbus-1`

optional: `libjpeg` `libwebp` for jpeg/webp output, `libpipewire-0.3` for recording on kde/gnome.
runtime: `ffmpeg` for `--record`, `tesseract` for `--tesseract`; see [OPTIONS.md](OPTIONS.md) for the rest (`trans`, `git`, `xdg-open`, a sound player).

## demo

<p>
  <img src="https://atums.world/u/08a4b8bb-e855-4905-a2c5-39f35497ff33.png" width="220" alt="region selector">
  <img src="https://atums.world/u/502cfb19-7d8c-4115-bc38-8be272e31b23.png" width="220" alt="annotator">
  <img src="https://atums.world/u/eb03253f-9e88-4703-aed2-afa7eca2377a.png" width="220" alt="color picker">
</p>

- region selector on a frozen screen: drag, click a window to snap to it, or adjust before capturing
- **`-F`** a whole monitor or all of them, **`-w`** the active window, **`-L`** the last region, **`--delay`** to open a menu first
- hdr outputs tone mapped to srgb, or kept as 16-bit png with `capture.hdr`
- **`-e`** annotator: pens, shapes, arrows, blur, pixelate, spotlight, text, counters, callouts, an eraser and a color picker; also on an existing file with `-f`
- **`--record`** region recording to mp4, webm or gif, with pause ([demo](https://atums.world/u/7598183f-c502-4c4e-9c51-6f167473a8fb.mp4))
- **`--pin`** pins captures to the desktop
- **`--tesseract`** OCR to the clipboard, with **`--translate`**
- uploads to six built-in services or any sharex `.sxcu` uploader
- **`grabit settings`** for every option, a searchable history of uploads, files and OCR text, and an upload page for any file
- **`--tray`** tray icon with every action in its menu
- mouse and touchscreen

## docs

- [OPTIONS.md](OPTIONS.md): config keys, auth tokens, sharex uploaders, keybinds, filename templates, history
- [PLUGINS.md](PLUGINS.md): plugin cli, manifest format, helper header
- `man grabit`

## source

- primary: https://heliopolis.live/creations/grabit
- github mirror: https://github.com/Creationsss/grabit

## license

agpl-3.0-or-later. see `LICENSE`.
