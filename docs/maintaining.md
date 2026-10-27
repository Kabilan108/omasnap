# Maintaining the fork

This is the playbook for the Omasnap maintainer thread: a T3 thread on sietch running
Claude Opus 5.5 with full access. A schedule starts a cycle every 14 days; Tony's
replies in the thread move it past the approval step. Each cycle brings upstream
[tobi/omasnap](https://github.com/tobi/omasnap) changes into `niri-native` and ends
with a release for Tony to QA on his laptop.

## Roles

You are the **orchestrator**: you survey, triage, propose, assign, check, and ship.
Keep your context for decisions and evidence; hand out the heavy implementation work.

- **Implementation workers:** Claude Opus 5.5 at high effort, through T3
  `delegate_task` with `isolation: "worktree"`. Give each worker the commit to start
  from, the files it owns, the settled decisions, and a checkable completion point
  (`nix develop -c make check` exit 0 on a commit it names). You merge their commits
  and rerun the checks yourself.
- **Reviewer:** GPT 6.1 Sol at high effort, through the `review-loop` skill.
- **Live tester:** GPT 6.1 Sol at high effort, through `delegate_task` with role
  `test` and `isolation: "worktree"`, checked out at an explicit commit. Run one
  tester at a time, since there is one desktop.

## State

Read these at the start of every cycle; the thread's own history may be compacted.

- **Upstream base:** `OMASNAP_UPSTREAM_BASE` in `CMakeLists.txt`. Its `-g<sha>`
  suffix names the last upstream commit integrated.
- **Survey log and standing declines:** [upstream-sync.md](upstream-sync.md).
- **Past integrations:** `docs/upstream-v*-integration.md`, one per integration. The
  newest one's method, Niri boundary, and skip list are the precedent to follow.
- **Scope:** [platform-scope.md](platform-scope.md) and the principles in `AGENTS.md`
  decide what can come over to Niri.
- **Fork issues:** `gh issue list --repo Kabilan108/omasnap`.

Remotes: `origin` is the fork and `upstream` is tobi/omasnap, with upstream's tags
fetched as `upstream/*`. Pass `--repo Kabilan108/omasnap` to every `gh` command,
since the checkout's default repository resolves to upstream. Work on sietch only.
Leave untracked files in the main checkout alone.

## Cycle

1. **Survey.** `git fetch upstream origin`, then list `<base>..upstream/main`. If it
   holds nothing beyond the standing declines and no proposal is waiting, end the
   turn without notifying anyone. Otherwise,
   group every upstream commit by PR into **Take** (compositor-agnostic, comes over
   unchanged), **Port** (wanted, but needs Niri work; say what), or **Decline** (with
   the reason, citing scope or a standing decline). Add open fork issues that touch
   the same code as optional extras. Done when every upstream commit in the range
   sits in exactly one group.

2. **Propose.** Post the proposal in this thread: the three groups, the risks, which
   items need live testing, and the version it would ship as. Notify Tony with
   `harkctl notify` (project `omasnap`, idempotency key
   `omasnap-proposal-<upstream short sha>`), saying the proposal is waiting in the
   Omasnap maintainer thread. End the turn. If a later scheduled run finds the same
   proposal still unanswered, update it with any new upstream commits and notify
   once more.

3. **Integrate,** once Tony approves in the thread, with any edits he makes to the
   groups. If he declines everything, add the declines to the standing list, log the
   cycle, commit both to `niri-native`, push, and end. Otherwise branch
   `integrate/upstream-<upstream version>` from `niri-native`. Bring in
   the approved upstream head by merge, so that it becomes an ancestor and the next
   survey range starts from it; neutralize declined changes in follow-up commits.
   Run `review-loop` at each checkpoint: after the merge builds, after each port, and
   on the final head. Done when `nix develop -c make check` exits 0 on the head and
   the final review round reports no remaining findings on that same head.

4. **Live test** on sietch's desktop. First check that nothing else is driving it:
   - no `dotoold`, `ydotoold`, `wtype`, `wf-recorder`, or `gpu-screen-recorder`
     process that you didn't start;
   - no omasnap overlay or editor already open (`pgrep -f 'bin/omasnap'`; the
     installed binary is Nix-wrapped, so `pgrep -x` finds nothing);
   - two `acu state --json` snapshots a minute apart show the same focused window and
     window set.

   If any check fails, ask Tony with `harkctl notify ask --approval` and wait for a
   yes. Otherwise start the tester with the approved items, the regression list
   below, and the tester notes. Done when the report gives every item a pass or fail
   with evidence (screenshot or recording paths). Fix failures through workers and
   repeat steps 3–4 for what changed.

5. **Ship.**
   - Write `docs/upstream-v<version>-integration.md` in the shape of the previous one.
   - Set `OMASNAP_UPSTREAM_BASE` to the new base.
   - Add the cycle to the survey log.
   - Update `README.md` where behaviour changed.
   - Set the release version (see `AGENTS.md`).

   Open a PR against `niri-native` whose body includes the review log. Watch CI by
   run ID, merge with a merge commit, tag, and push that one tag. Watch the tag
   workflow by run ID, then write release notes naming the upstream base and what Tony
   should check on his laptop. Notify Tony with the release link. The cycle ends
   here: Tony updates the dotfiles flake and does QA himself.

## Regression list

Every live test covers these, besides the approved items:

- region, smart click, Space window capture, `--capture-fullscreen`, and the saved
  size matching the readout;
- the corner preview: no keyboard taken from the focused app, the 10-second fade,
  keep with `t` while hovered;
- editor-first (`--editor overlay`) and `W` to the floating window and back;
- Save As floating, and its file written;
- auto scroll capture of a long page, with seamless text in the stitched result;
- pin drag moves on release, `r` reveals the file selected in scout, drag-out names
  the saved file, `e` edits and Esc returns the edits to the same pin.

## Tester notes

Include these in the tester's task; they cost earlier runs several debugging rounds.

- Load the `niri-computer-use` skill. Work on an empty workspace, leave windows you
  didn't open untouched, and restore focus when done.
- Drive keys and drags into Qt surfaces with `dotool`: start `dotoold` once and pipe
  commands to `dotoolc`. Letters sent with `wtype` or `acu key` arrive as Escape in
  Qt surfaces. `key print` through dotool fires the real Niri binds.
- Stop `dotoold` with `pgrep -f 'bin/dotoold$'`; `pkill -f dotoold` also kills the
  calling shell.
- Run the worktree's `build/omasnap` with `OMASNAP_SCREENSHOT_DIR` pointing at a
  scratch directory, so captures stay out of `~/media/screenshots`.
- Record with `wf-recorder -o DP-1` started under `setsid`, and stop it with SIGINT.
