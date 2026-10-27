# Omasnap — Agent Guide

Omasnap is a fast, native Wayland screenshot and annotation overlay for Niri, forked
from upstream [tobi/omasnap](https://github.com/tobi/omasnap) (Hyprland/Omarchy). It
captures a region, the focused window, the full output, or a scrolling region, then
copies and saves it and shows a timed corner preview with an Edit button. The editor
keeps vector layers (arrows, lines, freehand, highlighter, rectangles, ellipses,
numbered markers, text, spotlight, redaction, OCR). Finished captures go to the
clipboard, `~/Pictures/Screenshots`, or a pinned always-on-top layer surface.

## Project principles

Each principle has a longer writeup under `docs/`; read it before changing behavior it
covers.

- **A specialized tool, not a general app.** Capture, annotate, output. A feature that
  isn't in service of "screenshot, mark it up, send it somewhere" doesn't belong here.
- **The main thread never blocks.** Disk I/O on full-resolution images, process
  spawning, and PNG encoding run on workers via `QtConcurrent`/`QFutureWatcher`. See
  [docs/threading.md](docs/threading.md).
- **Every operation is undoable.** The operation log is the source of truth; rendering
  is a pure function of it. Output is produced only on Copy, Save, pin, or a fresh
  capture's automatic copy and preview. Redaction destroys pixels at render time. See
  [docs/editing-model.md](docs/editing-model.md).
- **Minimally configurable.** No settings UI. The optional `omasnap.conf` keys are
  narrow escape hatches; a new key needs the same justification.
- **Speed first.** Instant capture, annotate, copy. No startup bloat.
- **Niri only.** Output discovery uses Niri JSON IPC, monitor and scroll capture use
  the in-process `ext-image-copy-capture` backend, and clean focused-window capture
  uses Niri's screenshot action. Pins are layer-shell surfaces. No X11,
  macOS/Windows, Hyprland, or generic-compositor support. See
  [docs/platform-scope.md](docs/platform-scope.md).
- **Lean dependencies.** Qt6, LayerShellQt, wayland-client and libdeflate, plus a few
  external processes (`niri`, `wl-copy`/`wl-paste`, `tesseract`, `notify-send`,
  `xdg-open`). Know this list before adding to it. See
  [docs/dependencies.md](docs/dependencies.md).
- **No backwards compatibility.** Break keybindings, CLI flags, file formats, or
  internals whenever it keeps the code simpler or the tool faster. Do not add
  compatibility shims, deprecation aliases, or migration code.
- **Minimal native UI.** Vector-drawn icons (no icon-theme dependency), the bundled
  fonts, and pinned chrome fonts and colors (`chromeFont()`, `chromeTheme()`); external
  Qt platform themes are bypassed at startup.
- **Single binary.** Capture, editor, pins, and scroll capture run from the one
  `omasnap` executable.

## Repository layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | CLI dispatch, single-instance lock, overlay vs windowed editor setup, saved-notification helper |
| `src/cli-path.cpp/.hpp` | Command-line options and image target resolution |
| `src/instance-lock.cpp/.hpp` | Single-instance handover: cancel a running overlay, or stop it and take over for `--file` |
| `src/capture.cpp/.hpp` | Niri discovery, capture, clean window capture with clipboard preservation, render pipeline, output, operation-log persistence |
| `src/surface-capture.cpp` | In-process output capture via `ext-image-copy-capture` |
| `src/editor.cpp/.hpp`, `src/editor-save-as.cpp` | Annotation editor: tools, layers, undo/redo, select↔edit phases, handoff, export, Save As |
| `src/overlay-chrome.cpp/.hpp`, `src/shortcut-guide.cpp/.hpp`, `src/chrome-theme.cpp/.hpp` | Shared overlay chrome, the shortcuts card, and the built-in chrome palette |
| `src/scroll-capture.cpp/.hpp` | Scroll-capture panel: live region, manual/auto modes, stitched result |
| `src/scroll-inject.cpp/.hpp` | Auto-scroll wheel injection through `zwlr_virtual_pointer_v1` |
| `src/auto-capture.cpp/.hpp`, `src/stitch.cpp/.hpp`, `src/stitch-replay.cpp` | Frame classification and stitching; offline replay tool |
| `src/recent-snaps.cpp/.hpp` | The recents shelf of editable earlier captures |
| `src/output-config.cpp/.hpp`, `src/palette-config.cpp/.hpp`, `src/background-config.cpp/.hpp` | The optional `omasnap.conf` sections |
| `src/pin.cpp/.hpp`, `src/pin-file.cpp/.hpp`, `src/pin-layout.cpp/.hpp`, `src/pin-expiry.cpp/.hpp` | Layer-shell pins, their files and slots, card geometry, preview countdown |
| `src/png.cpp/.hpp` | Fast libdeflate PNG writer with logical-size metadata |
| `src/cut.cpp/.hpp`, `src/text-band.cpp/.hpp`, `src/stroke-smoothing.cpp/.hpp`, `src/eyedropper.cpp/.hpp`, `src/icons.cpp/.hpp` | Editing helpers and the vector icon renderer |
| `tests/*-smoke.cpp/.hpp` | Headless Qt Test coverage, including fake `niri`/`wl-copy`/`wl-paste` commands |
| `docs/` | Principles in depth, the upstream integration records (`upstream-v*-integration.md`), and the maintainer playbook and sync state (`maintaining.md`, `upstream-sync.md`) |
| `flake.nix` | Nix package, runtime dependencies, development shell, and package check |
| `CMakeLists.txt` | Build definition; **the version lives here** (`project(omasnap VERSION ...)`) |

## Build and verify

```bash
make check
```

`make check` configures and builds the project, runs the complete headless offscreen
Qt smoke suite, runs `clang-tidy`, and runs `clazy-standalone`/`qmllint` when those
tools and source types are available. Use `make build` for a build-only pass, `make
smoke` for the behavioral smoke suite, and `make install` to install to `~/.local`.
Run project commands through `nix develop -c ...`.

The smoke suite writes artifacts under the build directory and private runtime
files; do not run two smoke suites against the same checkout at once. If it exits
with a bare code and no message, rerun with `QT_FORCE_STDERR_LOGGING=1` so Qt's
warnings reach stderr instead of the journal.

Always run `make check` after behavioral changes. CI
(`.github/workflows/build-linux.yml`) runs the same build and smoke in an Arch
container on every pull request and release tag. Live compositor behavior (capture, scrolling,
pins, focus) needs a manual check on Niri.

Dependencies (Arch): `base-devel cmake ninja pkgconf qt6-base layer-shell-qt
wayland wayland-protocols libdeflate libnotify niri wl-clipboard xdg-utils
tesseract tesseract-data-eng`.

## Release process

Versions are release dates, independent of upstream's numbers: `2026.11.03`, then
`2026.11.03.1` for a second release that day. The last semver release was 1.21.2.
`omasnap --version` also prints `OMASNAP_UPSTREAM_BASE`, the upstream commit the
release integrates; name it in the release notes too.

1. Set `project(omasnap VERSION ...)` in `CMakeLists.txt` and `version` in
   `flake.nix` to the release version. When the release integrates upstream work,
   also set `OMASNAP_UPSTREAM_BASE`.
2. Build and run the smoke test (above).
3. Commit, tag `v<version>`, push `niri-native` and that one tag. The GitHub workflow
   attaches the build artifact to the release automatically.

Upstream integration cycles, run by the maintainer thread, follow
[docs/maintaining.md](docs/maintaining.md).

See `README.md` for user-facing features, keybindings, and install instructions —
keep it in sync when behavior changes.
