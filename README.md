# Omasnap for Niri

A native Wayland screenshot and annotation overlay for the Niri compositor.
Omasnap captures the focused output before mapping its layer-shell surface, so
the editor never appears in its own screenshot. Window selection delegates to
Niri's compositor-native picker and window capture actions.

[![Looping Omasnap demonstration](assets/omasnap.gif)](assets/omasnap.mp4)

## Features

- Freeform region, Niri-native window, and full-output capture modes.
- Clean window capture by Niri window ID, including tiled windows in the
  scrolling layout without guessing their on-screen geometry.
- Select/move/resize layers, mouse-wheel scaling, and eight external recropping
  handles.
- Arrows, straight lines, smoothed freehand strokes, translucent highlighter
  strokes, rectangles, numbered markers, and editable Neucha text.
- Per-layer preset or custom colors, undo/redo history, OCR-region capture,
  mesh-gradient backdrops, rendered drop shadows, and crop-boundary clipping.
- Private runtime state under `$XDG_RUNTIME_DIR/omasnap` (with a
  `/tmp/omasnap-<uid>` fallback), including mode-`0600` working snapshots with
  unpredictable names and cleanup on graceful exit.
- Verified PNG clipboard output through `wl-copy`/`wl-paste`, plus timestamped
  files under `~/Pictures/Screenshots` by default.
- Existing-image editing with `--file` or a positional image path, plus an
  **Edit screenshot** action on saved-image notifications.
- Correct native-pixel export on fractional or integer-scaled outputs.

## Niri integration

The supported target is **Niri on Wayland**. Monitor, workspace, and window
selection use `niri msg`; `grim` captures the focused output before the overlay
maps. The annotation editor is an exclusive layer-shell overlay.

Window mode temporarily hides the editor and starts `niri msg --json
pick-window`. Once a window is clicked, Omasnap asks Niri to capture that window
by IPC ID and makes a best-effort restoration of the previous clipboard payload
before opening the editor. This works with Niri's scrolling layout, where tiled
window viewport coordinates are intentionally not exposed through IPC.

Runtime commands used by the application:

- `niri`
- `grim`
- `wl-copy` and `wl-paste`
- `tesseract`
- `notify-send`

### Niri binding

For the screenshot layout used by Jacurutu, replace the existing
`Shift+Print` annotation binding in `config.kdl` with:

```kdl
Shift+Print { spawn-sh "OMASNAP_SCREENSHOT_DIR=$HOME/media/screenshots exec omasnap"; }
```

Plain `Print`, `Ctrl+Print`, and `Alt+Print` can remain bound to Niri's built-in
region, output, and focused-window screenshot actions. Omasnap sets up its own
overlay-layer namespace and does not need a Niri layer rule.

Niri reloads its configuration automatically. Check the installed executable
and visible layer surface with:

```bash
omasnap --version
niri msg --json layers | jq -c '.[] | select(.namespace == "omasnap")'
```

The second command prints the layer only while the editor is open.

## Run, develop, and install

The checked-in flake is the supported run and development interface:

```bash
# Run this checkout
nix run .

# Pass Omasnap arguments
nix run . -- --capture-window
nix run . -- --file /path/to/image.png

# Run the maintained Niri branch without cloning
nix run github:Kabilan108/omasnap/niri-native

# Development and verification
nix develop
nix build .#omasnap
nix flake check
```

On Jacurutu, the persistent installation is declarative: the dotfiles flake
provides `github:Kabilan108/omasnap/niri-native` to the Niri-only screenshot
module and adds its `omasnap` package to `home.packages`. Update and activate
that input through the normal dotfiles workflow after a tested branch update:

```bash
cd ~/dotfiles
nix flake update omasnap
nh os switch
```

During development, use `nix run .` from this checkout instead of rebuilding
the full host configuration. Niri itself comes from the active host so its IPC
client remains aligned with the running compositor.

## CLI capture modes

Running without arguments opens freeform region selection:

```bash
omasnap
```

Explicit starting modes:

```bash
omasnap --capture-region
omasnap --capture-window
omasnap --capture-fullscreen
```

Compatibility positional names remain accepted:

```bash
omasnap region
omasnap windows
omasnap fullscreen
omasnap smart       # maps to region selection
```

These options choose the initial capture flow; the editor still controls
whether the final image is copied, saved, or both.

### Edit an existing image

Open any readable image directly in the annotation editor, skipping compositor
capture:

```bash
omasnap --file /path/to/image.png
omasnap "/path/with spaces/image.png"
```

The path must resolve to a regular readable image and decode successfully. A
saved screenshot notification includes an **Edit screenshot** action; selecting
it reopens that exact file in Omasnap. Dismissal and timeout do nothing.

Environment overrides:

```bash
OMASNAP_SCREENSHOT_DIR="$HOME/media/screenshots" omasnap
OMASNAP_OCR_LANGS="eng+deu" omasnap
```

Install the corresponding Tesseract language data before adding a language to
`OMASNAP_OCR_LANGS`.

## Controls

### Capture selection

| Input | Action |
|---|---|
| Drag | Select a region |
| `Space` | Hide the overlay and open Niri's window picker |
| Click in Niri picker | Capture that window and open the editor |
| `Ctrl+A` | Select the full focused output |
| `Esc`, `Esc` | Dismiss |

### Annotation editor

| Input | Action |
|---|---|
| `V` | Select/move/resize layers; wheel scales the selected layer |
| `A` | Arrow |
| `L` | Straight line |
| `F` | Freehand stroke |
| `H` | Translucent highlighter stroke |
| `C` | Numbered marker |
| `R` | Rectangle |
| `T` | Neucha text |
| `O` | Drag an OCR region and copy recognized text |
| `B` | Cycle backdrop |
| `1`–`6` | Set annotation color |
| Wheel | Scale selected layer or change active tool size |
| Double-click text | Reopen text editing |
| `Delete` | Delete selected layer |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z`, `Ctrl+Y` | Redo |
| `Ctrl+C` | Copy PNG only |
| `Ctrl+S` | Save PNG only |
| `Enter` | Copy and save |
| `Esc` | Return to Select; press again to close |

Creation tools return to Select after one placement without selecting the new
layer. In Select mode, arrows and lines show only their two endpoint handles;
other layers show a selection boundary. The eight blue/white handles outside
the image recrop its corners or edges.

## Development and verification

```bash
nix flake check
```

The package build compiles the release target, runs the interaction smoke suite,
wraps Qt's runtime plugin paths, and starts the installed wrapper on the
offscreen platform. The smoke executable exercises private runtime permissions,
snapshot lifecycle, region/full-output/file startup modes, annotation tools,
highlighter rendering and history, crop clipping, text editing, OCR, native-DPI
output, endpoint-only line selection, and external crop handles. Niri IPC
capture and notification actions are verified separately in a live compositor
session.

`.github/workflows/build-linux.yml` performs the same release build and
interaction smoke in an Arch Linux container, stages the CMake installation,
and uploads a versioned Linux artifact. A `v*` tag also attaches that artifact
to the corresponding GitHub release.

## Acknowledgements

The capture and annotation workflow is inspired by Shottr, Satty, and
Flameshot. Omasnap is an independent implementation and is not affiliated with
those projects.

The bundled Neucha font is distributed under the SIL Open Font License; its
license is in `assets/OFL.txt` and is installed with the application.
Omasnap itself is distributed under the MIT License in `LICENSE`.
