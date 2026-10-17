# Omasnap

A native Wayland screenshot and annotation overlay for Niri. This is a Niri fork of
[tobi/omasnap](https://github.com/tobi/omasnap); see
[docs/upstream-v1.21-integration.md](docs/upstream-v1.21-integration.md) for what it
takes from upstream and what it changes. Upstream's [changelog](CHANGELOG.md) covers
the shared features.

Omasnap captures the focused output before mapping an exclusive layer-shell surface, so
the editor never appears in its own screenshot. The editor retains annotations as movable,
resizable vector layers and preserves the monitor's native pixels on scaled displays.

Select a capture and it copies straight to the clipboard, saves, and shows a small
preview card in the corner for copying again, dragging into another app, or opening
the editor on demand.
The preview fades after 10 seconds of idle time. Hovering and unfinished actions
pause its countdown. The pin button, `Ctrl+P`, `T` while hovered, or dragging the
preview keeps it on screen.

[![Looping Omasnap demonstration](assets/omasnap.gif)](assets/omasnap.mp4)

## Features

- Smart selection by default: drag a freeform region or click for the full
  output; Space captures the focused window. Explicit region, window,
  fullscreen, and scrolling-region modes remain available.
- Fresh captures save, copy to the clipboard, and show a corner preview for 10 seconds
  without taking keyboard focus. Hover for Pin, Edit, Copy, file drag, and Close
  controls. Use the pin button, `Ctrl+P`, `T` while hovered, or drag the preview to
  keep it on screen.
- A pointer-side readout that turns any drag into a ruler: the pointer position
  while the crosshair is idle, then the frame size in native export pixels while a
  region or a crop handle is being sized. Crosshair guides
  appear without requiring an initial mouse movement.
- Window capture uses Niri's clean `screenshot-window` action for the focused
  regular window, so overlapping windows do not show. The clipboard is preserved
  around Niri's action, and capture aborts if it cannot be preserved first.
- Select/move/resize layers, mouse-wheel scaling, and eight external recropping handles.
- Start arrows, shapes, strokes, markers, spotlights, or text in the unused
  fullscreen workspace around a screenshot, or resize and carry an existing
  layer past its edge, to grow the canvas. Source-based tools (redact, cut,
  OCR, and eyedropper) stay on the screenshot.
  Framed growth is the default, with 15 px of mat kept beyond any layer that
  outgrows the normal frame; `G` cycles to tight Overflow growth (only the
  sides needed by annotations, with no frame), then Image (the original canvas
  size, clipping every outside annotation). `Shift+G` cycles backward without
  changing layer geometry. New framed strips start in window gray with the
  original screenshot's card shadow, and follow a layer live while it is drawn
  or carried past the edge, or back inside it; a label being typed out there
  counts from the moment its caret is placed. `B` cycles through the colorful backdrops,
  shadowed and flat window gray, and Off so a background can always be removed.
  Overflow with no backdrop leaves its added pixels transparent. `Shift+B`
  toggles the current shadow directly, and undo/delete can contract grown strips.
- Standard, pointy, curved, and double-headed arrows; straight lines; smoothed
  freehand strokes; and translucent highlighter
  strokes that automatically match and stay straight across screenshot text (with
  freehand fallback), plus hollow or filled rectangles (optionally rounded) and
  ellipses, numbered markers, editable text in Neucha, JetBrains Mono, or Inter
  Display (plain, outlined, or on a readability pill), and secure redaction with
  opaque or randomized non-spatial mosaic output.
- Per-layer preset or custom colors (including highlighter ink), undo/redo history,
  one-click whole-image or drag-region OCR (the recognized text is shown beside
  the image and copied to the clipboard),
  mesh-gradient backdrops, and rendered drop shadows on standard backdrop cards.
  The editor previews the same background padding, rounding, and shadow as the
  saved image, including when zoomed or fitted to a smaller window.
- Dragging an arrow head, tail, or bend, or a line endpoint, hides the cursor
  and drag handles for precise placement. They return on release or cancellation.
- A subtle dotted image boundary stays visible with every tool. Select mode
  brightens it and shows crop handles when no annotation is selected.
- Cut tool: drag across a band of the image to remove it and collapse the gap, with a
  live preview and dashed seam marker while dragging; annotations shift to follow.
  Hold **Ctrl** (or **Ctrl+X**) to insert a transparent band instead; the toolbar
  icon swaps to a split-plus while Ctrl is down.
  Moving or resizing an existing layer suspends the armed tool's action until
  release, then leaves the tool ready for the next canvas gesture.
- Pin a finished capture as a bottom-right layer-shell card, launched from the
  same `omasnap` executable and visible on every workspace without window rules.
  Pins stack upward from the corner.
- Crash-resistant working documents under `/run/user/<UID>/omasnap/` (falling back to
  a private `/tmp/omasnap-<UID>/`): the original source image plus a sidecar JSON
  operation log. Undo still works after a crash or `--file` reopen. Saving and
  copying write a normal flattened PNG to the clipboard or `~/Pictures/Screenshots`.
- Verified PNG clipboard output through `wl-copy`/`wl-paste`, plus timestamped files
  under `~/Pictures/Screenshots` by default.
- Open an image already on the clipboard directly in the annotation editor.
- A recents shelf: the select overlay stacks small cards of the last five captures
  along the right edge; hover to fan them out, click one to reopen it in the editor
  with its layers still editable instead of taking a new screenshot.
- Correct native-pixel export on fractional or integer-scaled monitors.

## Platform scope

The supported target is **Wayland + Niri**. Output and workspace discovery use Niri's
JSON IPC. The focused output is captured in-process through `ext-image-copy-capture`
before the layer maps; clean window capture uses `niri msg --json focused-window`
followed by Niri's `screenshot-window` action. Scroll capture uses the same capture
protocol and injects auto-scroll wheel events through `zwlr_virtual_pointer_v1`.
See [docs/platform-scope.md](docs/platform-scope.md) for the details. Generic Wayland
compositor support is not claimed.

Runtime commands used by the application:

- `niri`
- `wl-copy` and `wl-paste`
- `tesseract`
- `xdg-open`, plus `xdg-mime` and `busctl` when available, for Show in folder
- `notify-send`; saved captures include an Edit action that reopens them in
  Omasnap. Notification failure does not invalidate output.

## Install with Nix

Run directly from a checkout:

```bash
nix run path:.
```

Or add the flake as an input and install `packages.<system>.omasnap` in a NixOS or
Home Manager configuration. The package puts `notify-send`, `wl-copy`, `wl-paste`,
`xdg-open` and Tesseract on its runtime `PATH`; Niri and a working layer-shell
environment come from the system.

### Niri bindings and window rule

One binding for quick shots and one for shots you know you will annotate:

```kdl
binds {
    Print { spawn "omasnap"; }
    Ctrl+Print { spawn "omasnap" "--editor" "overlay"; }
    Shift+Print { spawn "omasnap" "--capture-window"; }
    Alt+Print { spawn "omasnap" "--capture-fullscreen"; }
}
```

Each capture binding toggles: the first press opens the overlay, the next press
dismisses it.

The windowed editor (`--editor window`, or `W` in the overlay editor) and the Save As
chooser are normal windows with app-id `omasnap`. Niri tiles them unless a window rule
floats them:

```kdl
window-rule {
    match app-id="^omasnap$"
    open-floating true
}
```

The overlay and pins are layer-shell surfaces and are not affected by window rules.

### Manual Arch Linux build

Install the complete build/runtime dependency set:

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf qt6-base layer-shell-qt \
  wayland wayland-protocols libdeflate niri wl-clipboard xdg-utils libnotify \
  tesseract tesseract-data-eng
```

Build and install:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
cmake --install build
```

The install step places:

- `~/.local/bin/omasnap`
- `~/.local/share/applications/omasnap.desktop`
- `~/.local/share/licenses/omasnap/Neucha-OFL.txt`
- `~/.local/share/licenses/omasnap/JetBrainsMono-OFL.txt`
- `~/.local/share/licenses/omasnap/Inter-OFL.txt`
- `~/.local/share/licenses/omasnap/Lucide-ISC.txt`
- `~/.local/share/licenses/omasnap/MIT.txt`

Launch Omasnap from the application launcher by searching for its name, or use
the screenshot keybindings above.

Ensure `~/.local/bin` is on `PATH`, then verify the installed CLI:

```bash
omasnap --version
omasnap --help
```

## CLI capture modes

Running without arguments opens smart selection. Drag for a freeform region,
click to capture the focused output, or press Space to capture the focused
window:

```bash
omasnap
```

Explicit starting modes:

```bash
omasnap --capture-region
omasnap --capture-window
omasnap --capture-fullscreen
```

Scroll capture stitches a region that is taller (or wider) than the screen:

```bash
omasnap --scroll
```

Drag a region, then pick a direction: **Scroll ↓ / →** scrolls the page
yourself while omasnap captures each step, and **Auto ↓ / →** scrolls it for
you, one acknowledged notch at a time, stopping when the page stops moving.
The frames are aligned and stitched into one image, copied, and shown in a timed
preview. Keep it with the pin button, `Ctrl+P`, or `T` while hovered, or open its
editor to annotate it; `Ctrl`+wheel zooms and the wheel scrolls it.

Positional capture modes are also accepted:

```bash
omasnap region
omasnap windows
omasnap fullscreen
omasnap smart
```

These options choose what is initially selected. `--capture-window` captures the
window that had keyboard focus when Omasnap started. Completing a selection saves it,
copies it, and shows a preview that fades after 10 seconds of idle time unless kept.
Existing previews and pins stay on screen and appear in later captures; close or move
them aside when they cover the next capture area.

For annotation before any output, add `--editor overlay` or `--editor window`.
You can also press `E` (edit) or `A` (annotate) in the capture picker to keep
the annotator open after the capture. Press either key again to turn it off.
The editor then controls whether the result is copied, saved, or both.

Fresh captures automatically save as well as copying to the clipboard and
showing a preview. Set `[output] autosave = false` to opt out. Preview expiry leaves the saved PNG intact. Explicit
`--copy` remains copy-only; annotation before output still waits for your chosen
Copy or Save action.

Quick output skips the preview as well as the annotation editor. Add `--copy` to copy
only, `--save` to save only, or both flags to copy and save. Region and window captures output after selection;
fullscreen captures output immediately. Quick output cannot be combined with `--file`,
`--clipboard`, or `--pin`.

### One instance, toggled by the same hotkey

Only one capture overlay runs at a time, guarded by a lock file in the runtime snapshot
directory. Starting omasnap while an overlay is open sends the running instance `SIGTERM`,
which it handles with a clean Qt shutdown; the new process then exits without capturing.
Pressing `PRINT` therefore opens the overlay and pressing it again dismisses it.

Every capture invocation dismisses this way, quick output included: `--copy`/`--save`
while an overlay is open closes the overlay and outputs nothing, rather than screenshotting
the overlay that is still on screen.

Editing an existing image is never cancelled this way: `--file`, `--clipboard`, or an
image path stops the running instance, waits up to 60 seconds for a safe handover (a
window capture finishes restoring the clipboard first), and opens the editor on that
image. That is how a pin's Edit button and a notification click always land
in the editor.

A lock left behind by a crashed instance is removed and reclaimed. A lock file that cannot
be read or written at all is reported on stderr instead of being mistaken for a running
instance.

Exit codes:

| Code | Meaning |
|---|---|
| `0` | Success, including dismissing a running overlay |
| `1` | Capture, image, or single-instance lock failure |
| `2` | Usage error |

### Edit an existing or clipboard image

Point omasnap at any readable image and it opens straight into the annotation editor
with the whole image selected, skipping the screen-capture step:

```bash
omasnap ~/Pictures/Screenshots/screenshot-2026-08-11_10-00-00.png
# or
omasnap --file /path/to/capture.png
```

New PNG exports retain their logical display size, so reopening a 2× capture
from a file browser keeps its original size in the annotator while
preserving every native pixel. This also covers cropped, framed, and scrolling
captures. Images without this metadata use their pixel dimensions; print DPI
alone is not treated as a screenshot's display scale.

To open the image currently on the Wayland clipboard:

```bash
omasnap --clipboard
```

The clipboard must offer readable image data. Text-only clipboard contents return an
error instead of opening an empty editor.

The editor opens on the focused Niri output.

File URLs are accepted too. A saved capture notification's Edit action launches
`omasnap` on the finished screenshot, so it can be reopened and re-annotated.

### Recent captures

Every completed capture keeps its working document, source plus operation log,
on a shelf of the five most recent under
`~/.local/state/omasnap/recent/` (`OMASNAP_RECENT_DIR` overrides). The select
overlay shows them as a small stack of cards on the right; hovering fans them out
and clicking one reopens that capture in the editor, undo history intact, in place
of a new screenshot. No annotation, Copy, Save, or pin action is required: the
shot remains available after its preview card expires or closes. Dismissing
the editor with `Esc` also remembers its current edits. Editing the same shot
updates its existing entry; cancelling before selecting a capture adds nothing.
If retaining a recent fails after Copy or Save, the worker keeps the existing
recovery document and attempts to update it with the final source and history.
The output still completes immediately. Persistence errors are logged.

### Configuration (optional)

Omasnap has no settings UI and runs fine with no config at all. If you want to
change where screenshots land or what they are called, create
`~/.config/omasnap/omasnap.conf` (INI format); every key is optional:

```ini
[editor]
# overlay (default): the editor fills the screen as a fullscreen overlay.
# window: the editor opens as a normal window, tiled or floated by Niri
# (see the window rule above), so a capture can be annotated next to another
# window.
# W switches a live editor between the two either way, and --editor
# window|overlay overrides this per invocation.
# This chooses the on-demand editor's presentation; fresh captures still
# copy and show a timed preview unless --editor is explicitly passed.
mode = overlay
# opaque (default): a windowed editor paints a solid backdrop.
# translucent: it keeps the overlay's see-through dim.
backdrop = opaque

[output]
# Save fresh captures while still copying and showing the timed preview.
# Default: true. Set false to opt out.
autosave = true
# Where saved screenshots go. Default: ~/Pictures/Screenshots
directory = ~/Pictures/Captures
# Filename pattern, without extension (.png is appended).
# Default: screenshot-{date}_{time}-{app}
filename = screenshot-{date}_{time}-{app}

[colors]
# Up to eight preset colors for the palette, and the initial custom color.
palette = #ff375f, #ff9f0a, #ffd60a, #30d158, #0a84ff, #bf5af2, #000000, #ffffff
custom = #ff375f

[background]
# An image used as a "Custom" backdrop. B (or the toolbar button) cycles
# through the four gradients, this image when readable, shadowed/flat window
# gray, and Off.
image = ~/Pictures/backdrops/desk.jpg
# Style a fresh capture starts with: none, off, slate, aurora, sunset, lagoon,
# violet, or custom. `none` still allows Framed canvas growth to add its
# automatic window-gray mat; `off` stays transparent. `custom` only takes
# effect once `image` above loads successfully.
default = custom
```

Filename tokens:

| Token | Expands to |
|---|---|
| `{date}` | `2026-08-23` (yyyy-MM-dd) |
| `{time}` | `14-05-09` (HH-mm-ss) |
| `{app}` | Slug of a captured window's Niri `app_id`, e.g. `firefox`, `alacritty`, `nautilus` (last segment of a reverse-DNS id). Empty for region and fullscreen captures and file edits — the separator before or after it is dropped too, so the default pattern gives `screenshot-2026-08-23_14-05-09.png`. |

The default keeps the date first so the folder always sorts chronologically:
`screenshot-2026-08-23_14-05-09-firefox.png`. Anything else in the pattern is
literal text (`screenshot-` is just a string). A name that already exists
gets `-2`, `-3`, … appended.

Environment overrides (`OMASNAP_SCREENSHOT_DIR` takes precedence over the config):

```bash
OMASNAP_SCREENSHOT_DIR="$HOME/Pictures/Captures" omasnap
OMASNAP_OCR_LANGS="eng+deu" omasnap
# Thai plus English:
OMASNAP_OCR_LANGS="tha+eng" omasnap
```

Install the corresponding Tesseract language data before adding a language to
`OMASNAP_OCR_LANGS`. When unset, Omasnap uses `eng`.

## Controls

The capture picker and fullscreen annotator show a readable shortcuts card in
the lower left. Press `?` or click its **Shortcuts** header to collapse or expand
it. The card scrolls on shorter screens; `?` still types normally in a text
annotation. The windowed editor keeps its guide above the toolbar.

### Capture selection

The default smart picker infers the capture kind from the gesture: drag for a
region or click for the full focused output. Space captures the window that had
keyboard focus when Omasnap started, through Niri's clean window screenshot.
Clicking a window is not supported on Niri: Niri reports positions only for
floating windows.

Press `S` before drawing to select a scrolling region; once drawn, the page
inside it goes live and the scroll controls appear in place. A small **Scroll
capture** button under an image already open in the editor turns that region
into a scrolling capture. Explicit `region`, `windows`, `fullscreen`, and
`scroll` command-line targets remain available for scripts and keybindings.
While scrolling, use the on-screen Done and Cancel buttons, or `Enter` and
`Esc`. The overlay keeps keyboard focus while the wheel scrolls the page under
the region.

| Input | Action |
|---|---|
| Click | In smart mode, capture the full focused output |
| `Space` | Capture the focused window |
| Drag | Select a region, with its native pixel size shown at the pointer |
| `Tab` / `Shift+Tab` | Cycle the region's shape: free (default), square, 3:4, 16:9; works mid-drag |
| `S` | Toggle scrolling-region mode |
| `E` / `A` | Toggle annotation after capture; the capture guide shows on/off, and the choice also applies to scrolling captures |
| `R` | Restore the last drawn region, including from a previous Omasnap launch in this login session (same monitor and overlay size) |
| `Enter` | In smart mode, capture the full focused output |
| `Ctrl+A` | Select the full focused output |
| Hover the right-edge stack | Fan out the five most recent captures; click one to reopen it |
| `Esc` | Dismiss; cancel a selection drag if one is in progress |

Region memory is stored in Omasnap's private runtime directory. It survives
closing and reopening Omasnap and lasts until those runtime files are removed
(normally when the login session ends). A region from another monitor, a
different overlay size, or outside the current screen is ignored; draw a new
region after changing the display layout.

### Annotation editor

| Input | Action |
|---|---|
| `V` | Select/move/resize layers; carrying one past the source grows the canvas; drag empty canvas for a marquee; multi-select outlines each layer without treating the canvas as one layer; wheel scales the selected layer |
| `A` | Arrow; press again to cycle Standard, Pointy, Curved, and Double styles |
| `S` | Spotlight/loupe; press again to cycle ellipse, rectangle, rounded |
| `L` | Straight line |
| `F` | Freehand pen; medium `3/6` smoothing by default, while `0/6` preserves the raw pointer path |
| `H` | Highlighter; Snap mode uses a mouse-following I-beam at the nearby text height, then locks the drag straight to that row. Press `H` again (or click the active toolbar button) for Normal freehand mode, where wheel or `Alt`+wheel changes thickness; Snap keeps detected-row height automatic and wheel sets only its off-text fallback |
| `I` | Eyedropper in the color popover · sample the image as the custom color |
| `C` | Numbered marker |
| `R` | Rectangle; hover the shape button for rectangle, ellipse, and fill controls; `Alt`+wheel rounds corners |
| `E` | Ellipse; shares the shape submenu and filled/hollow toggle |
| `D` | Redact; press again to toggle randomized pixelation or solid redaction |
| `X` | Cut out a band; drag to preview the crossed-out strip, then release to remove and collapse it. **Ctrl** (with Cut armed, or **Ctrl+X**) inserts a band instead; the cut icon becomes a split-plus |
| `T` | Text on a cream readability pill, with Neucha as the default. Click for a one-line label, or drag a box to give it room for several lines: Enter moves to the next line while there is room and commits on the last one; `Shift+Enter` always adds a line; `Esc` commits the text and dismisses the annotator; long text wraps at the current canvas edge by default, while moving it or dragging its width handle beyond that edge expands the canvas; clicking away keeps the text; press T again to toggle the pill |
| `Shift+T` | Cycle the next or selected text through Neucha, JetBrains Mono, and Inter Display |
| `O` | Recognize and copy all text in the current image |
| `B` | Cycle shadowed colors, window gray (shadowed and flat), and Off |
| `Shift+B` | Toggle the screenshot card's drop shadow; on by default |
| `G` / `Shift+G` | Cycle canvas boundaries forward/backward: Framed, Overflow, Image. Framed auto-grows with the normal frame; Overflow grows only the sides needed by annotations with no frame; Image clips at the original screenshot edge |
| `W` | Re-present the editor as a normal window, or back as the fullscreen overlay; selection, layers, and undo history carry over |
| `1`–`8` | Set annotation color; `7` is black and `8` is white |
| Wheel | Scale selected layer, magnify the spotlight under the cursor, or change active tool size (`Alt`+wheel: selected pen smoothing from 0–6, the next pen's smoothing when none is selected, rectangle corner radius, or spotlight border); while just viewing a zoomed capture, scroll it like a document |
| `Shift`+wheel | Scroll a zoomed capture sideways (a wide stitch); never changes the zoom |
| `Ctrl`+wheel · middle-drag | Zoom about the cursor · pan by dragging |
| `+` / `-` / `0` (also with `Ctrl`) | Zoom in / out / fit |
| Hold `Shift` while dragging | Make rectangles, ellipses, and spotlights 1:1; snap line and arrow endpoints to 45°; keep curved-arrow bends centered; while dragging a selected layer's handle, keep a rectangle, redaction or spotlight's aspect ratio |
| Hold `Alt` while dragging | Center rectangles, ellipses, and spotlights on the press point; add `Shift` for a centered square/circle |
| `←` `↑` `→` `↓` | Nudge the selected layer 1 px; hold `Shift` for 10 px (a held key is one undo step). With nothing selected, pan a zoomed capture |
| Double-click text · `Enter` on a selected text | Reopen text editing |
| `Delete` | Delete selected layer |
| `Alt+D` | Duplicate selected layer (offset down-left, or away from a nearby edge); the copy becomes the selection |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z`, `Ctrl+Y` | Redo |
| `Ctrl+C` | Copy PNG only |
| `Ctrl+S` | Save PNG only |
| `Ctrl+Shift+S` | Save As: choose a PNG destination (`.png` is added when omitted; other formats are rejected) |
| `Enter` | Copy and save (with a text layer selected: edit it) |
| `Ctrl+P` / `P` | Keep the capture pinned on screen and close the editor |
| `Esc` | Dismiss the annotator; keep an originating pin in place with its edits and undo history |
| Right-click | Return to Select; cancel active drawing |

Save As opens a file chooser (floated by the window rule above). Saving from the editor with Save, Save As,
or Copy + Save closes it and shows an unpinned preview in the lower-right stack,
replacing an originating preview or pin. The preview has the usual 10-second idle
timeout; its folder and Copy path buttons refer to the exact PNG you saved.
Cancel or a failed save keeps the editor open. Recent captures retain the editable
layers and undo history.

### Capture previews and pins

Normal captures show a preview card that fades after 10 seconds of idle time. Hovering
and in-progress actions pause the countdown; it resumes when the preview is idle again.
Clicking, scrolling, copying, or editing does not pin the preview. The pin button,
`Ctrl+P` on a focused preview, or `T` ("tack") while hovered keeps it until closed.
Dragging the preview also keeps it. `T` remains the Text shortcut in the editor. Kept
shots show a pin icon even when the other controls are hidden. Unpinning starts a fresh
10-second countdown. Opening Edit leaves the preview's expiry policy unchanged; pin it
first to keep the preview available throughout annotation.

In the editor, `Ctrl+P` or `P` renders a capture that stays pinned. It writes a
`pin-<pid>-<n>-<random>.png` under the runtime snapshot directory and launches the same
`omasnap` executable in detached pin mode. Each pin is a layer-shell surface on the
capture's output, visible on every workspace. Pins take the first free slot in a column
growing up from the bottom-right corner, wrapping into further columns.

Captures use fast lossless PNG encoding. The card loads a separate small display image;
the clipboard, saved files, and editor retain every original pixel. The card is 200
logical pixels wide with the display's aspect ratio (height clamped to 50–400 pixels)
and fills that frame with a top-anchored cover crop. Drag the image background to move a
pin; it moves when you release, because Qt cannot know a layer surface's position on
screen while dragging.

Pinning neither touches the clipboard nor writes to the screenshot directory; it is a
fourth output alongside copy, save, and copy-and-save. `Ctrl+P` / `P` closes the editor
and releases the single-instance lock immediately. Pins from separate captures
accumulate as independent processes.

Hover a pin to reveal its controls and use its keyboard shortcuts. **Edit** and
**Copy** sit in the center of the image. The pin button sits beside **×** at the
top-right; the drag handle, file-path button, and folder button sit at the top-left.
Icon buttons show a short hint inside the card. Pins do not take keyboard focus when
they appear; hovering a pin gives it the keyboard, and moving the pointer away hands
the keyboard back to the focused window. Opening a pin for annotation keeps the pin in place and
gives focus to the editor. One `Esc` dismisses the editor and updates that same pin,
including any text being typed. Reopening it restores the editable layers and undo
history. `P` / `Ctrl+P` in that editor returns to the existing pin too.

| Input on a pin | Action |
|---|---|
| Pin button, `Ctrl+P`, `T` while hovered | Keep on screen; press again to unpin and restart the countdown |
| Drag the image background | Move the pin on release and keep it on screen |
| Edit button, `A` / `E` while hovered | Annotate the capture while keeping the same pin |
| Link button, `L` / `F` while hovered | Save the capture if needed and copy its file path |
| Folder button, `R` while hovered | Save the capture if needed and show it in the default file browser |
| Copy button, `C` while hovered, `Ctrl+C` | Copy the full-resolution PNG |
| Top-left six-dot drag handle | Drag the PNG into a file-capable drop target |
| Wheel | Keep the fixed preview size |
| Close button, `X`, `Esc`, middle-click | Close |

Image and path copying use `wl-copy` rather than `QClipboard`, so clipboard data remains
available after the pin is closed. Copying a temporary capture's path or showing it in
its folder first saves its PNG in the configured screenshots directory. Both actions
reuse that file; closing or expiring the preview leaves the saved copy available. A pin
opened from an existing file copies that file's original path. The folder button
follows the default `inode/directory` application. It asks that application to select
the screenshot through `FileManager1.ShowItems` when supported, otherwise opens the
containing folder with `xdg-open`. The controls use the annotation toolbar's vector
icons, including Lucide's pin drawn directly by the renderer.

Canvas boundary changes affect only preview and export clipping. The complete vector
geometry stays in the operation log, so switching back to Grow restores every off-canvas
part of a layer.

Creation tools return to Select after one placement without selecting the new layer. In
Select mode, lines and straight arrows show two endpoint handles; curved and double arrows
add an on-curve handle for bending the arc (hold `Shift` to keep that bend centered). Other
layers show a selection boundary. The eight handles outside the image recrop
its corners or edges. The image stays in place while dragging a crop handle, then
re-centers when released. After the canvas grows, those crop handles remain on the
original source frame.

## Development and verification

```bash
make check
```

Smoke screenshots, fixtures, and saved captures go in `build/smoke-output/`,
including when the executable is launched directly from the repository root.
For a custom build directory, output lives in `smoke-output/` beside the executable.
Use `--output-dir <directory>` to choose another artifact directory; positional
paths and unknown flags are rejected.

The smoke executable exercises mocked Niri output discovery and clean window capture
(including clipboard restoration and failure aborts), smart/region/fullscreen startup
modes, capture selection, pin controls and shortcuts,
working-document persistence (source plus op-log JSON), annotation tools, undo/redo
replay, vector movement and scaling, text editing, OCR, native-DPI output,
endpoint-only line selection, annotation-driven canvas growth and clipping policies,
external crop handles,
and the native-pixel
measurement readout on a scaled monitor.

For live launch profiling, the binary has an opt-in millisecond trace from `main()`
through the first completed overlay paint:

```bash
OMASNAP_PROFILE_STARTUP=1 ./build/omasnap 2>startup.log
```

The trace also breaks native capture into Wayland registry, buffer allocation, frame wait,
and pixel handoff stages, and marks output readiness separately from subsequent
recent-history persistence. It is completely silent by default.

`.github/workflows/build-linux.yml` runs the same `make check` build, interaction smoke,
and available static-analysis checks in an Arch Linux container, stages the CMake installation, and uploads a versioned Linux
artifact. A `v*` tag also attaches that artifact to the corresponding GitHub release.

## Acknowledgements

The capture and annotation workflow is inspired by three excellent screenshot tools:

- [Shottr](https://shottr.cc/) — fast region/window capture, OCR, and polished backdrops.
- [Satty](https://github.com/Satty-org/Satty) — a focused, Wayland-native annotation workflow.
- [Flameshot](https://github.com/flameshot-org/flameshot) — selection-first capture and an
  approachable annotation toolbar.

Thanks to their authors and contributors for establishing the interaction patterns that made
this project possible. Omasnap is an independent implementation and is not
affiliated with those projects.

## Project history

This standalone repository was extracted with `git filter-repo` from the original Omarchy
system-customization repository. The former `omasnap/` directory was promoted to
the repository root while retaining its relevant commit history. The `niri-native`
branch of [Kabilan108/omasnap](https://github.com/Kabilan108/omasnap) carries the Niri
port on top of upstream releases.

The bundled Neucha, JetBrains Mono, and Inter Display fonts are distributed
under the SIL Open Font License. Their provenance and hashes are recorded in
`assets/FONTS.md`; their licenses are installed with the application.
