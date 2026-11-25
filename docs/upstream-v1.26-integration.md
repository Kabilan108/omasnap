# Upstream v1.26 integration

This integration brings upstream Omasnap v1.26.3 (`c382a92`) into `niri-native`. The
previous base was `008a3d1` (v1.25.0); the range holds upstream v1.25.1 through
v1.26.3. As with v1.25, upstream is merged into the fork (`git merge c382a92`), so the
merge commit's first parent is the fork. A follow-up commit undoes the declined
changes that merged without a conflict, and later commits port what needed fork work.

## Included from upstream

Compositor-agnostic changes come over unchanged:

1. Scroll capture stitches the page on a worker thread, and Cancel works while it
   stitches (#228).
2. Editor fixes (#241): vertical cuts at display scales above 1, rounded clipping on
   framed exports and in the live view, spotlight over a custom backdrop, a failed Save
   As keeps the capture's recents entry, and OCR and eyedropper clipboard copies run off
   the UI thread, one at a time, ahead of OCR and export.
3. Scroll and capture fixes (#242): auto scroll's notch clamp (up to 12 per tick) and
   its cadence tie-breaker, Continue after a stall restarts the worker and reapplies
   the input region, the virtual pointer's park clamp, a stitch allocation check, and
   `ext-image-copy-capture` handling of re-announced constraints and of `poll`
   interruptions.
4. Process and file fixes (#243): `runProcess` stops busy-waiting, waits after a kill
   and treats a crash as unfinished; operation-log and handoff write checks; a pin
   snapshot keeps its sidecar whenever it has a recents id or a saved path; OCR input
   through `writePng`; PNG conversion in strips.
5. Images list Omasnap under Open With (#223).
6. The smoke suite bypasses the desktop's Qt theme, as `main()` does (`8cd4799`).

## Ported to the fork

- **App icon** (#222): `omasnap.svg` is also listed in `flake.nix`'s source set, so the
  Nix package installs it.
- **Pin toast timer** (from #243): applied by hand to the fork's layer-shell pin, so
  repeated toasts restart one timer.
- **Checked config write in a smoke test** (from `5ec4434`); its window-target
  initializers don't apply to the fork.
- **Focused-output lookup bound** (from #207): opening a file or the clipboard without
  `--output` waits at most 500 ms for Niri's focused output before using the default.
  The fork already opened that editor on the focused output; upstream's #207 itself is
  not needed.

## Fork fixes in this release

- The recents shelf opens only on pointer motion (fork #10). A pointer resting where
  the stack appears no longer fans it out when the list happens to load before the
  overlay's enter event, and the smoke check no longer depends on timing or on cursor
  state left by earlier checks.

## Skipped, and why

- Upstream's Omarchy build (#227, #230): `bin/` scripts, PKGBUILD and omarchy-pkgs
  packaging, `test.yml` and `release.yml`, the ctest lint and `.clang-tidy`, and the
  Makefile's removal. The fork keeps `make check`, its Arch and Nix CI, and its date
  versions.
- Versions from the git tag and the CHANGELOG's removal (`54d63a7`), and the 1.25.1 and
  1.26.0 release bumps.
- Renaming the app "Screenshot" (#229): Omarchy naming; its window rules are Hyprland's.
- Hyprland-only pieces of #242 and #243: the natural-scroll probe, the uinput pointer,
  hyprctl client reaping, and the evdev pin teardown.
- Everything in the earlier integrations' skip lists still applies.

## Known limitations

The v1.25 integration's known limitations still hold, including fork #6. Pin drag-out
after an edit hands over the runtime preview file (fork #11).

## Niri boundary

Unchanged from the v1.25 integration.

## Live testing

Tested on Niri with one 3440x1440 output at scale 1, recorded end to end:
- auto scroll capture of the README to its end, with continuous text at the inspected
  seams; the panel kept updating while stitching, Cancel while stitching wrote no
  file, and Continue after the end restarted and stopped cleanly;
- eyedropper then OCR, and eyedropper then Copy, each leaving the right clipboard item;
- spotlight over a custom backdrop, and a framed export's rounded corners;
- Save As into an unwritable directory failing with the capture still on the shelf;
- a pointer resting where the shelf appears leaves it folded until the pointer moves
  (fork #10);
- `omasnap <png>` opening the editor, including handover from an open overlay;
- a repeated pin toast, pin Edit, and `r` reveal in Scout;
- `--notify` Open and Edit with quickshell's notifications, and Space window capture;
- the regression list: region, smart click, Space and fullscreen capture sizes, the
  preview's keyboard behaviour, 7-second fade and `t`, editor-first and the `W` round
  trip, floating Save As, and pin drag, reveal, drag-out and edit return.

Fork #6 still ends early on near-identical lines, as it does on 2026.11.15. Dragging
out a pin after editing it hands over the runtime preview file rather than the saved
file, also as on 2026.11.15 (fork #11).

Not tested: vertical cuts at 2x scale, multiple outputs, and Open With through an
installed desktop entry (the positional launch it runs was tested).
