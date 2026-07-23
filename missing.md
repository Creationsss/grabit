# Missing features / roadmap

Gap analysis against ShareX, Flameshot, Satty, Swappy, ksnip, Spectacle, and Shottr,
cross-referenced with commonly requested features. Check items off as they land.

Effort is rough: S = small, M = medium, L = large.

## Already have (don't re-add)

region select + window snap (hyprland), confirm mode, per-monitor / all-monitor
fullscreen, annotator (rect, ellipse, arrow, line, pen, marker, blur, text, eraser),
undo, color picker + eyedropper, live magnifier (hold key), always-on coords,
recording (mp4/webm/gif, pause/stop, overlay, tray), pin-to-desktop, OCR + translate,
uploads (6 hosts + any `.sxcu`), plugins, configurable keybinds, sound, tray.

Differentiators others lack: pin-to-desktop, OCR **+ translate**, `.sxcu` compat,
plugin system, live magnifier.

## Annotation editor

- [x] **Step / number counter** (done) — `TOOL_COUNTER`, click-to-stamp numbered badge
      (key `c`); auto-numbers from the count of existing counters, contrast-aware text.
- [x] **Pixelate** (done) — the old "blur" tool was already a mosaic; it is now `TOOL_PIXELATE`
      (default `x, 0`), and `TOOL_BLUR` is a real gaussian blur. Two distinct redaction styles.
- [x] **Redo** (done) — `keys.redo` (default `Ctrl+y, Ctrl+Shift+z`); inverse-item redo stack
      cleared on any new action. Undo's `UNDO_ANNO_ADD` gains a paired `UNDO_ANNO_READD`.
- [ ] **Spotlight / dim-outside** (M) — darken everything but a region for emphasis.
      Has it: ShareX.
- [ ] **Callout / speech bubble** (M) — text bubble with a pointer tail. Has it: ShareX, ksnip.
- [ ] **Line/arrow styles** (S) — dashed lines, thickness presets, rounded rect. Has it: most.

## Capture workflow

- [ ] **Delay / timer capture** (S) — `--delay N`, capture after a countdown (menus,
      tooltips). Has it: Spectacle, ShareX, all.
- [ ] **Repeat last region** (S) — `--last` re-capture the previous region. A preset rect
      is already threaded through `region_select`. Has it: ShareX, Shottr.
- [ ] **Generic active-window capture on non-Hyprland** (L) — needs a compositor window
      list or portal; wlroots has none. Has it: Spectacle, Flameshot (portal).

## Recording

- [ ] **Audio capture** (M) — mic / system audio via PipeWire into the existing ffmpeg
      pipe. Has it: wf-recorder, OBS, ShareX.
- [ ] **Webcam overlay** (L) — likely out of lane. Has it: ShareX, OBS.

## Post-capture / misc

- [ ] **Image effects** (M) — border, drop-shadow, watermark, resize. Has it: ShareX, CleanShot.
- [ ] **QR generate / scan** (S–M) — niche but low-effort with a lib. Has it: ShareX, ApexShot.

## Not planned / infeasible

- **Scrolling capture** — most-wanted "big" feature elsewhere (ShareX, CleanShot, Shottr),
  but effectively infeasible on Wayland: no reliable synthetic-scroll + frame-stitch
  without compositor support.
- **KDE recording** — needs a PipeWire ScreenCast-portal backend, which reverses the
  no-portal / no-popup design decision. Screenshots on KDE work via the ScreenShot2
  desktop-file authorization.
