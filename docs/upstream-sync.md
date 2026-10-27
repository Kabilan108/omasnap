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

- 2026-10-27: proposed upstream `v1.24.0` (`a432f40`): take #200, #199, #194,
  `1413492` and the releases; port #197 `--notify` onto `notify-send` with
  `--recent`; decline #165 (our `--output` already does it); fork #4 as an extra.
  Tony deferred it to the next cycle so 1.21.2 can settle. The next survey proposes
  this range again, plus anything newer, as one batch.
- 2026-10-27: maintainer set up at base `v1.21.0-81-g6771b19`. Upstream had moved to
  `v1.24.0` (`a432f40`); the first scheduled cycle surveys it.
