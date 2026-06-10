# kludge sweep over the vat writeup

## Work done

Worked the easily-fixable kludges confessed in `2026-06-10-vat-integration-wave-1.md`;
everything verified on the macos host.

- `modules/port/readme.md` rewritten to the vat-era surface (was the stale
  reactor doc, flagged in wave 4): `.vat` create, `mel_vat_is_owner` affinity,
  per-op four-entry vat sources, `mel_vat_post` marshaling, win32 as the
  `unavailable` stub pending the IOCP waiter, tests section describing the
  on-thread vat run-helper, and a corrected Known-gap section (see below).
- `modules/port/test/tsan_build.sh` repaired: it still compiled the deleted
  `modules/reactor`; now builds the vat TUs (vat core, fair driver, timers,
  kqueue + cocoa waiters, debug/assert stack). All four drivers build; ran them:
  - `tsan_loop_cancel`: 64-op on-loop cancel-vs-completion — passes, tsan-clean.
  - `tsan_kqprobe`: confirms raw kqueue `EVFILT_WRITE` starvation after peer
    `shutdown(SHUT_RDWR)`.
  - `tsan_pollhup`: **reproduces the pure-HUP write hang through the port on the
    kqueue waiter** (peer half-close, full send buffer, fd kept open). The
    port-loop closed-peer test passes because it `close()`s (EV_EOF fires);
    `shutdown()` without close starves. Readme's Known-gap section now states
    this precisely, citing both drivers; the fix belongs to the waiter.
  - `tsan_cancel_race`: cancels from foreign threads — predates the affinity
    contract and now trips `mel_vat_is_owner` by design; documented in the
    readme as the regression probe for that assert.
- melody-showcase smoke: the wave-11 sanctioned revert — process step back on
  `mel_process_run` pipes (`.vat`, `.deliver`), file-redirect workaround
  deleted. Verified live: stdout captured over the cocoa waiter's fd bridge,
  smoke exits 0.
- melody-showcase smoke honesty: `Smoke.failed` tracks every failure path
  (fs/storage/clip write fails, read statuses, process launch/exit), and
  `smoke_finish` sets exit code 1 when set — a smoke that passed on a failed
  write no longer lies.
- `modules/await`: exclusivity assert on `Mel_Await_Step` (exactly one of
  `future`/`channel`/`after_ns>0`/`reschedule`); the two-fields-set misuse that
  silently took the first branch now fires loud.
- cocoa waiter: the runloop is captured at construction and `arm`/`disarm`
  assert `CFRunLoopGetCurrent()` matches — the wave-11 "contractual, not
  asserted" constraint is now enforced; `CFRunLoopAddSource`/`RemoveSource` use
  the stored runloop.
- `docs/verification.md` (new): durable home for the gpu/ObjC fork-per-test
  knowledge (`MEL_TEST_NOFORK=1`), the MoltenVK `DYLD_LIBRARY_PATH` incantation,
  the tsan driver harness, and the smoke gates — was tribal knowledge in two
  writeup waves.

Gates: vat-core 10/10, await-bridge 6/6, port-loop 17/17, process-spawn 11/11,
melody-showcase builds and `--smoke` exits 0. Touched C files clang-formatted.

## Kludges (MEL-ENGINE-VIII)

- This work was developed in a mirrored worktree
  (`.claude/worktrees/kludge-sweep`), not the live checkout: the harness's
  background-edit isolation rejected in-place edits and the vat work is
  uncommitted, so a normal worktree lacked it. The live tree was cloned in
  (APFS `cp -c`), fixed, and verified there; the changes ship as a patch to
  apply to the live checkout.
- The smoke failure path (exit 1) was not exercised by a forced failure; only
  the success path ran. The logic is a boolean and an emit, but untested is
  untested.
- `tsan_cancel_race` was left asserting-by-design and documented, not
  redesigned or deleted; whether it should survive at all is Gabbo's call.
- `tsan_build.sh` keeps its pre-existing hardcoded
  `/opt/homebrew/opt/llvm/bin/clang` and hand-listed TU set; it will drift
  again when vat sources move (it now also builds the cocoa waiter because
  `mel_vat_waiter_io` lives in that TU).
- The cocoa `disarm` assert also runs via `close`; vat close off the opening
  thread would now assert where it silently misbehaved before — believed
  contract-conforming everywhere, but no test pins waiter-close affinity.

## CLAUDE.md suggestions

- None.

## Suggestions

- Delete `tsan_cancel_race.c` (git keeps it) or repoint it at
  `mel_vat_post`-marshaled cancels; its scenario is illegal under the affinity
  contract.
- The pure-HUP starvation now has a one-command repro
  (`tsan_build.sh tsan_pollhup.c`); when the waiter grows HUP synthesis
  (EVFILT_READ companion filter on OUT-only wakeables), flip the driver into a
  regression gate.
- The remaining writeup kludges and proposed shapes for them are catalogued in
  the session's report; the heavier ones (KQ_BATCH option, vat teardown hook,
  timer cancel handles, boot waiter-selection seam, vsync close race) each need
  a Gabbo decision before code.
