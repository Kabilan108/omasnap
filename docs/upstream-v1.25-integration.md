# Upstream v1.25 integration

This integration brings upstream Omasnap v1.25.0 (`008a3d1`) into `niri-native`. The
previous base was `6771b19` (v1.21.0 plus unreleased work); the range holds upstream
v1.22.0 through v1.25.0. Unlike the v1.21 integration, upstream is merged into the fork
(`git merge 008a3d1` on `niri-native`), so the merge commit's first parent is the fork
and `git diff 008a3d1 <merge>` shows the Niri layer kept on top of upstream. Later
commits on the branch port what needed Niri work.

## Included from upstream

Compositor-agnostic changes come over unchanged:

1. Previews fade after 7 seconds of idle time instead of 10 (#199). The fork's saved
   notification expires after 7 seconds too.
2. Closing the shortcuts card hides it until `?`, and later captures keep it closed
   (#200), through upstream's state-marker helper.
3. Unedited exports keep the captured native pixels, with no resampling (#196).
4. Another program's JSON beside an image (`report.json` next to `report.png`) no
   longer blocks `--file` or pin actions; a damaged omasnap log is still an error (#185).
5. A stale instance lock never signals an unrelated process that reused its pid (#187).
6. `--clipboard` stops reading at 256 MiB and skips an oversized format in favour of
   another offered one (#188).
7. A PNG's logical-size tag is believed only for a display scale of 1 to 4 (#186).
8. Input methods get a caret rectangle under the text caret (#173).
9. The theme initializes after command-line parsing (`1413492`), and the smoke-test
   race fix (#194).

## Ported to Niri

- **`--notify`** (#197). Upstream announces saved captures with
  `omarchy-notification-send` buttons worded through `omarchy-i18n`. The fork extends
  its `notify-send` helper instead: under `--notify`, a saved capture shows a
  notification with the image, its size, and **Open** and **Edit** actions in place of
  the corner preview. Copy-only captures keep their preview. English only. Edit runs
  `omasnap --recent <id> --file <png>`, which reopens the capture from the recents shelf
  with its layers and undo history and falls back to the PNG once the shelf has dropped
  it. The plain saved notification's Edit uses the same reopen, so it now restores
  layers too. The notification daemon must support actions.
- **Hiding the recents shelf** (`b0f97d3`). Upstream toggles it with Space, which the
  fork uses for the clean focused-window capture, so the fork binds `H` in the capture
  overlay. The choice is remembered like upstream's.

## Fork fixes in this release

- Pins stack per output (fork #4): slot locks are keyed by output name, so each
  output's pins start at the top slot. Closing a lower pin still leaves its slot empty
  until a new pin takes it.

## Skipped, and why

- Opening the editor on the preview's monitor (#165): the fork has done this since
  v1.17.2 through `--output`; upstream's `--handoff-monitor` stays out.
- `omarchy-notification-send`, `omarchy-i18n`, and upstream's omarchy-pkgs release step.
- Everything in the v1.21 integration's skip list still applies.

## Known limitations

The v1.21 integration's known limitations still hold. In addition:

- With `--notify`, an Edit clicked while the capture's recents entry is still being
  written waits up to 30 seconds, then reports an error rather than opening flat
  pixels.
- Auto scroll capture ends after one frame on a page whose lines differ only in a
  narrow column, such as numbered but otherwise identical lines (fork #6). The
  shipped 1.21.2 does the same; ordinary pages capture fully.

## Niri boundary

Unchanged from the v1.21 integration, except that the saved-notification helper now
lives in `src/capture.cpp` (with `--recent` resolution in `src/recent-snaps.cpp`) so
the smoke suite can drive it.

## Live testing

Tested on Niri with one 3440x1440 output at scale 1, recorded end to end:
- the 7-second preview fade, hover pause, and `T` to keep;
- `H` hiding the recents shelf across captures, with Space still capturing the focused
  window;
- the shortcuts card staying closed;
- `--notify` with quickshell's notifications: the image, the size, Open in the image
  viewer, and Edit reopening an editable arrow layer;
- an unedited region export identical, pixel for pixel, to the same region of a
  full-output capture;
- region, smart click, Space window and fullscreen capture sizes;
- the preview leaving the keyboard with the focused app;
- editor-first and the `W` round trip;
- floating Save As writing its file;
- pin drag, reveal in Scout, drag-out, and edit return.

Auto scroll capture of the README completed (68 frames) with continuous text at the
inspected seams. The early end on near-identical lines (fork #6) reproduced the same
way on 1.21.2.

Not tested: per-output pin stacking (one output), multiple outputs in general,
fractional and 2x scale, and input methods (no fcitx5 on the test machine).
