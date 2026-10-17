# Upstream v1.21 integration

This branch starts at upstream Omasnap main `6771b19` (v1.21.0 plus unreleased work,
198 commits after the v1.17.1 base `58cc4c0` of the previous integration) and merges
`niri-native` v1.17.2 into it, resolving every conflict by reapplying the Niri layer
onto upstream's code instead of replaying upstream commits onto the fork. The merge
commit's first parent is upstream main, so `git diff 6771b19 <merge>` shows the Niri
layer as it first landed; later commits on the branch port the remaining
compositor-specific features.

## Included from upstream

Compositor-agnostic features come over unchanged:

1. Drawing tools: arrow styles (standard, pointy, curved with a bend handle, double),
   text wrapping at a dragged width or the canvas edge, outlined text, JetBrains Mono
   and Inter Display alongside Neucha, pen smoothing, highlighter snap to screenshot
   text, Ctrl+Cut transparent bands, zoom out to 10%, and counter placement offset.
2. Canvas: growth for off-canvas annotations, the Framed/Overflow/Image boundary modes,
   custom backdrops, the dotted image boundary, and their follow-up fixes.
3. Editor chrome: the grouped toolbar, the readable shortcuts card, and Save As.
4. Capture overlay: `R` (last region), `Tab` (aspect ratios), crosshair on pointer
   entry, and the `E`/`A` annotate-after-capture toggle.
5. Performance: export off the UI thread, libdeflate PNG encoding, the platform-theme
   bypass, bounded 6K repaints, spotlight lens compositing, and single-threaded
   Tesseract.
6. Correctness: logical size stored in PNGs, snapshot and op-log drain fixes, crop
   anchoring, repaint tearing, Escape after scrolling, the desktop launcher entry, and
   autosave by default.
7. The recents shelf: the last five captures, reopened with their layers.
8. The post-capture flow: a fresh capture copies, autosaves, and shows a timed corner
   preview with Edit; `--editor overlay|window` and the `E`/`A` toggle keep the editor
   first.

## Ported to Niri

- **Output discovery and window capture.** Niri JSON IPC replaces `hyprctl monitors`
  and `hyprctl clients`. Window capture stays the fork's clean focused-window capture
  (`niri msg action screenshot-window`) with clipboard preservation; Space in the
  overlay and `--capture-window` run it. The worker now finishes restoring the
  clipboard before the instance lock is released, since upstream releases the lock
  before the editor is destroyed. Window captures keep a configured default backdrop.
- **Still capture.** Upstream's in-process `ext-image-copy-capture` backend replaces
  `grim`. On Niri a full-output capture matched `grim -o` pixel for pixel, with no
  cursor and no overlay veil; grim is no longer a dependency.
- **Scrolling capture.** Auto-scroll injects wheel events through a
  `zwlr_virtual_pointer_v1` bound to the output; positive axis values scroll the page
  down on Niri. Upstream's uinput path was used only when `hyprctl getoption
  input:natural_scroll` answered, so it is removed. Upstream's keyboard-grab workaround
  for Hyprland is removed too: the overlay keeps exclusive keyboard focus, and Niri
  still routes the wheel through the input-region hole to the page, so Enter and Escape
  work while the region is live.
- **Windowed editor and Save As.** Upstream floated and sized these with runtime
  `hyprctl` rules and an `[editor] window = floating|tiled` key. Both are removed; a
  Niri window rule on app-id `omasnap` with `open-floating true` floats them. The
  hidden `--output` flag replaces upstream's `--handoff-monitor` for editor handoff
  and pin placement.
- **Filenames.** The `{app}` token uses a window capture's Niri `app_id`. Region and
  fullscreen captures have no app name.
- **Pins.** Pins stay the fork's layer-shell surfaces and take upstream's card UI:
  display-shaped 200 px card with a cover crop from a small display image, hover
  controls and hints, `T`/`A`/`E`/`C`/`L`/`F`/`R`/`X` while hovered and `Ctrl+P`, timed
  expiry with explicit keep, reveal in folder, and edits returning to the same pin with
  their layers. A pin moves when a drag is released, because Qt cannot report a layer
  surface's global position. Niri focuses a newly mapped on-demand layer and never
  focuses layers on hover, so a pin maps without keyboard interactivity and takes
  exclusive keyboard focus only while the pointer is over it.
- **Notifications** use `notify-send`, with the saved-capture Edit action handled by a
  detached `omasnap --saved-notification` helper.

## Skipped, and why

- Upstream's floating-window pins and their Hyprland placement: the deck/fan stacking,
  drag insertion, Super-drag, and compositor packing. Niri cannot keep a floating window
  on every workspace; layer-shell pins can.
- Focusing the next pin when one closes: a layer surface cannot give keyboard focus to
  another process's surface.
- Clicking a window in the smart picker: Niri reports on-screen positions only for
  floating windows (`tile_pos_in_workspace_view` is None for tiled ones), so there is
  no rectangle to select. A smart click captures the full output.
- The overlay dismissal socket that let Hyprland's Super+W on a pin under the overlay
  dismiss the overlay: layer-shell pins receive no compositor close requests.
- The Omarchy live theme: `chrome-theme` keeps upstream's built-in palette only.
- `OMARCHY_OCR_LANGS`, `omarchy-notification-send`, `install-omarchy`, Hyprland docs,
  and the CodeQL workflow.

## Known limitations

- Rotated outputs: scroll capture clamps its region against the untransformed buffer
  size, and auto-scroll parks the virtual pointer without the output transform. Both
  come from upstream's scroll code and are untested here; manual scroll capture is the
  fallback on a rotated output.
- The clipboard restored after a clean window capture keeps one representation (the
  preferred of PNG and plain-text types). `wl-copy` serves one type per offer.
- Pin shortcuts on an inactive output need Niri's `focus-follows-mouse`, or a click on
  the card first: Niri gives the keyboard only to layers on its active output, and a
  client cannot activate an output.
- A pin takes the keyboard when the pointer moves onto it. Enters Niri sends without
  motion (the pin mapping under a resting pointer, or a covering overlay closing) wait
  for motion, but releasing a capture-overlay drag over an older pin gives that pin the
  keyboard; move the pointer off it to hand the keyboard back.
- A timed preview can expire while its editor is open; pin it first to keep it, as
  upstream documents. Dismissing the editor still keeps the capture in recents.
- Upstream's stitched-scroll adoption replaces the operation log, so a configured
  default backdrop does not survive into a scroll capture (same on Hyprland).

## Niri boundary

Everything compositor-specific lives in a few places:

- `src/capture.cpp`: Niri IPC for outputs, workspaces, and the focused window; the
  clean window screenshot with clipboard snapshot and restore; `notify-send`.
- `src/main.cpp`: the focused-window query before the layer maps, `--output`, and the
  saved-notification helper.
- `src/scroll-inject.cpp` and `src/scroll-capture.cpp`: virtual-pointer injection and
  the exclusive keyboard focus during scroll capture.
- `src/pin.cpp`: layer-shell pins.
- `flake.nix`: packaging and runtime `PATH`.

Live compositor behaviour stays a manual test gate. The automated suite uses fake
`niri`, `wl-paste`, `wl-copy`, `xdg-mime`, `busctl`, and `xdg-open` commands and never
touches the running session.

## Live testing

Tested on Niri with one 3440x1440 output at scale 1: region, smart click, Space and
`--capture-window` clean window capture with the app_id filename, quick copy and save,
editor-first and the `E`/`A` toggle, preview fade and keep, recents, saved
notifications with Edit, in-process capture fidelity against grim, manual vertical and
horizontal and automatic vertical scroll capture, scroll-capture keyboard handling,
overlay↔window editor handoff, Save As, the `open-floating` window rule (loaded from a
temporary Niri config), and pin controls, shortcuts, drag, and edit return.

Not tested: multiple outputs (pin and editor placement through `--output`, capture on
a non-primary output), fractional and 2x scale, and drag-out from a pin into a file
manager.
