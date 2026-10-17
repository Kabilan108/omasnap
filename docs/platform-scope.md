# Platform scope: Wayland + Niri

This fork targets one platform: **Wayland on Niri**. Upstream Omasnap targets
Hyprland on Omarchy; this fork replaces the Hyprland-specific parts with Niri
equivalents and drops the Omarchy integrations. It is not a general Linux
screenshot tool, and it does not try to keep the Hyprland path working.

## What "Niri-only" means concretely

- Output and workspace discovery go through Niri's JSON IPC (`niri msg --json
  focused-output`, `workspaces`, `outputs`) in `src/capture.cpp`.
- Window capture is Niri's own clean window screenshot: `main.cpp` asks for
  `niri msg --json focused-window` before the overlay maps (the overlay takes
  keyboard focus), and Space or `--capture-window` runs `niri msg action
  screenshot-window --id N --path P`. Niri also puts that image on the
  clipboard, so the worker snapshots the clipboard first, refuses to capture if
  it cannot, and restores it afterward; shutdown waits for the restore before
  releasing the instance lock.
- Clicking a window in the smart picker is not supported. Niri reports
  on-screen positions only for floating windows (`tile_pos_in_workspace_view`
  is None for tiled windows), so there is no rectangle to highlight or crop. A
  smart click selects the full output instead.
- Window captures name files with Niri's `app_id` for the `{app}` token.
  Region and fullscreen captures have no app name.
- The windowed editor and the Save As chooser are ordinary xdg-shell windows
  with app-id `omasnap`. Omasnap does not place them; a Niri window rule does
  (see the README). Without the rule Niri tiles them.
- Pins are layer-shell surfaces on the overlay layer, anchored bottom-right in
  per-process slots, visible on every workspace without window rules. Upstream's
  floating-window deck and its compositor placement are not used. A pin moves
  on release because Qt cannot report a layer surface's global position.
- Scroll capture keeps the overlay's exclusive keyboard focus while the page
  under the region is live. Niri routes pointer input through the input-region
  hole by position alone, so the wheel reaches the page while Enter and Escape
  reach the overlay. (Upstream drops the grab over the page because Hyprland
  pins pointer focus to an exclusive layer.)
- Auto-scroll injects wheel events through a `zwlr_virtual_pointer_v1` bound to
  the capture output. Positive axis values scroll the page down on Niri.
- Notifications use `notify-send`; saved captures get an Edit action through a
  detached `omasnap --saved-notification` helper.

## Generic Wayland underneath

Capture, injection and surfaces stand on standard protocols:
`ext-image-copy-capture` for pixels (`src/surface-capture.cpp`),
`zwlr_virtual_pointer_v1` for injected scrolling (`src/scroll-inject.cpp`,
protocol vendored in `protocols/`), and `layer-shell` for the overlay and pins.
Another compositor implementing the same protocols might run parts of the tool,
but only Niri is tested, and nothing here adds fallbacks or compositor
detection for others.

## Not supported

X11, macOS, Windows, Hyprland, and generic-compositor fallbacks.
