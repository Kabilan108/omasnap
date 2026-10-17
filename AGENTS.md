# Omasnap — Agent Guide

Omasnap is a fast, native Wayland screenshot and annotation overlay for Niri. It captures
region, window, or full monitor, then opens an annotation editor with vector
layers (arrows, lines, freehand, highlighter, rectangles, ellipses, numbered
markers, text, OCR). Finished captures go to clipboard,
`~/Pictures/Screenshots`, or a pinned always-on-top layer surface.

## Project principles

- **Speed first.** The tool must feel instant: capture, annotate, copy. No
  startup bloat, no settings UI, no wizards.
- **Niri only.** Output discovery uses Niri JSON IPC, monitor capture uses `grim`,
  and clean focused-window capture uses Niri's screenshot action. No X11,
  macOS/Windows, Hyprland, or generic-compositor support.
- **No backwards compatibility.** Break keybindings, CLI flags, file formats,
  or internals whenever it keeps the code simpler or the tool faster. Do not
  add compatibility shims, deprecation aliases, or migration code.
- **Minimal native UI.** Use `notify-send`, `OMASNAP_OCR_LANGS`, vector-drawn icons
  (no icon-theme dependency), and the bundled Neucha font.
- **Single binary.** Everything (capture, editor, pin mode) runs from the one
  `omasnap` executable.

## Repository layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | CLI parsing, single-instance lock, mode dispatch (capture / edit file / pin) |
| `src/instance-lock.cpp/.hpp` | Single-instance handover: cancel a running overlay, or stop it and take over for `--file` |
| `src/capture.cpp/.hpp` | Capture, rendering, output, and source+JSON operation-log persistence |
| `src/editor.cpp/.hpp` | Annotation editor: tools, vector layers, operation-log undo/redo, export |
| `src/pin.cpp/.hpp` | Pinned-capture layer-shell surfaces (bottom-right, all workspaces) |
| `src/icons.cpp/.hpp` | Vector icon renderer for toolbar and pin controls |
| `src/cli-path.cpp/.hpp` | Command-line image target resolution |
| `src/eyedropper.cpp/.hpp` | Display-to-source color sampling |
| `src/pin-file.cpp/.hpp`, `src/pin-layout.cpp/.hpp` | Pin file lifecycle and layout helpers |
| `tests/*-smoke.cpp/.hpp` | Headless Qt Test coverage, including offscreen region-click, async-capture, and single-instance handover checks |
| `flake.nix` | Nix package, runtime dependencies, development shell, and package check |
| `docs/upstream-v1.17.1-integration.md` | Upstream feature inventory and Niri port boundary |
| `CMakeLists.txt` | Build definition; **the version lives here** (`project(omasnap VERSION ...)`) |

## Build and verify

```bash
make check
```

`make check` configures and builds the project, runs the complete headless
offscreen Qt smoke suite (including simulated region clicks and asynchronous
capture), runs `clang-tidy`, and runs `clazy-standalone`/`qmllint` when those
tools and source types are available. Use `make build` for a build-only pass,
`make smoke` for the behavioral smoke suite, and `make install` to install to
`~/.local`.

Always run `make check` after behavioral changes. CI
(`.github/workflows/build-linux.yml`) runs the same build and smoke on every
push and PR.

Dependencies (Arch): `base-devel cmake ninja qt6-base layer-shell-qt grim
libnotify niri wl-clipboard tesseract tesseract-data-eng`.

## Release process

1. Bump `project(omasnap VERSION ...)` in `CMakeLists.txt`.
2. Build and run the smoke test (above).
3. Commit, tag `v<version>`, push main and the tag. The GitHub workflow
   attaches the build artifact to the release automatically.
See `README.md` for user-facing features, keybindings, and install
instructions — keep it in sync when behavior changes.
