# Upstream v1.17.1 integration

This branch starts at upstream Omasnap v1.17.1 (`58cc4c0`) and reapplies the
Niri-specific capture, packaging, and documentation changes. This keeps the upstream
editor intact instead of replaying 52 upstream commits onto the older Niri fork.

## Included feature groups

1. Secure solid and randomized pixelation redaction, including correct stacking and
   OCR/export behavior.
2. Pinned captures with stacking, resize, drag, edit, copy, and path-copy controls.
3. Spotlight/loupe annotations with shape, border, sizing, and wheel controls.
4. Eyedropper sampling and configurable annotation color presets.
5. Cut-out editing with live seam preview and annotation repositioning.
6. Expanded drawing tools and geometry: highlighter, ellipse, rounded/fill shapes,
   centered and constrained creation, text pills, selection handles, duplication,
   nudging, z-order, and multi-select clearing.
7. Zoom, pan, native-pixel measurements, oversized-image navigation, and improved
   capture/editor rendering performance.
8. Whole-image and region OCR with streamed input, language selection, scan animation,
   and recognized-text presentation/copying.
9. Quick copy/save output, clipboard-image and file editing, clickable saved-capture
   notifications, and same-hotkey single-instance dismissal.
10. Crash-resistant source snapshots plus JSON operation logs, including undo/redo
    recovery after reopen.
11. Upstream correctness and lifecycle fixes through v1.17.1, including scaled and
    rotated captures, crop clipping, snapshot hardening, signal handling, pin cleanup,
    and editor interaction fixes.

## Niri-specific implementation boundary

The upstream annotation and output behavior is retained. The Hyprland discovery and
in-process `ext-image-copy-capture` backend are replaced with:

- `niri msg --json focused-output`, `workspaces`, and `outputs` for output metadata;
- streamed `grim -t ppm -s <scale> -o <output> -` for focused-output capture;
- asynchronous `niri msg --json focused-window` plus
  `niri msg action screenshot-window --id ... --path ...` for clean window capture;
- fail-closed clipboard snapshot and primary-type restoration around Niri's window
  screenshot action;
- `notify-send` for saved-capture notifications; and
- a Nix flake carrying build and runtime dependencies.

Focused-window capture stays asynchronous, and shutdown drains an in-flight capture so
clipboard restoration finishes before the instance lock is released. Live compositor
behavior remains an explicit manual test gate; the automated suite uses fake `niri`,
`grim`, `wl-paste`, and `wl-copy` executables and does not mutate the running Niri session.
