# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river, kde plasma 6. not supported: x11, gnome.

## install

### Void

see https://void.creations.works/

### Arch (AUR)

see https://aur.archlinux.org/packages/grabit

### NixOS

```sh
nix run git+https://heliopolis.live/creations/grabit.git -- --help
```

### from source

```sh
make
sudo make install
```

deps: `json-c libcurl libmagic wayland-client wayland-cursor cairo libxkbcommon libdbus-1`. runtime: `ffmpeg` (for `--record`), `tesseract` (for `--tesseract`).

## docs

- [OPTIONS.md](OPTIONS.md) - usage, configuration, auth tokens, sharex uploaders, recording/pin/ocr/edit, filename templates, env vars, build targets
- [PLUGINS.md](PLUGINS.md) - plugin cli, manifest format, helper header

## source

- primary: https://heliopolis.live/creations/grabit
- github mirror: https://github.com/Creationsss/grabit

## license

agpl-3.0-or-later. see `LICENSE`.
