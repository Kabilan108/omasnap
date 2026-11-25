# Upstream sync state

The maintainer thread's record between cycles; the steps live in
[maintaining.md](maintaining.md). The current upstream base is
`OMASNAP_UPSTREAM_BASE` in `CMakeLists.txt`.

## Standing declines

Upstream changes a survey should keep declining, beyond each integration record's
skip list. Remove an entry when its reason no longer holds.

- None yet.

## Survey log

Newest first. One line per cycle that reached a proposal: date, upstream head
examined, outcome.

- 2026-11-24: integrated upstream `v1.26.3` (`c382a92`): took #228, #241, #242 and
  #243 (less their Hyprland pieces), #223 and `8cd4799`; ported #222's icon, #243's
  pin toast timer, part of `5ec4434`, and a 500 ms bound from #207; declined the
  Omarchy build and packaging (#227, #230, `54d63a7`), the Screenshot rename (#229)
  and the release bumps. Fixed fork #10. Shipped as 2026.11.24. Filed fork #11 (pin
  drag-out after an edit, pre-existing).
- 2026-11-10: integrated upstream `v1.25.0` (`008a3d1`), which also covers the
  deferred v1.24.0 batch: took #200, #199, #196, #194, #188, #187, #186, #185, #173 and
  `1413492`; ported #197 `--notify` onto `notify-send` and `b0f97d3` onto `H`; declined
  #165; fixed fork #4. Shipped as 2026.11.10. Filed fork #6 (auto scroll's early end
  on near-identical lines, pre-existing). 2026.11.10 failed `nix build`: upstream's
  #188 clipboard-limit test wrote a `#!/usr/bin/env bash` fake, which the Nix sandbox
  can't run. 2026.11.11 fixes the test and adds a Nix CI job; `nix build` is now a
  release gate. 2026.11.15 fixes a check-phase race where closing a pin
  left its edited document locked.
- 2026-10-27: proposed upstream `v1.24.0` (`a432f40`): take #200, #199, #194,
  `1413492` and the releases; port #197 `--notify` onto `notify-send` with
  `--recent`; decline #165 (our `--output` already does it); fork #4 as an extra.
  Tony deferred it to the next cycle so 1.21.2 can settle. The next survey proposes
  this range again, plus anything newer, as one batch.
- 2026-10-27: maintainer set up at base `v1.21.0-81-g6771b19`. Upstream had moved to
  `v1.24.0` (`a432f40`); the first scheduled cycle surveys it.
